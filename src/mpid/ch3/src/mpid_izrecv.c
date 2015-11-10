/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *
 *      See COPYRIGHT in top-level directory.
 */
#if defined(FINEGRAIN_MPI)
#include "mpidimpl.h"

#undef FUNCNAME
#define FUNCNAME MPID_Izrecv
#undef FCNAME
#define FCNAME MPL_QUOTE(FUNCNAME)
int MPID_Izrecv(void ** buf_handle,  MPI_Aint count, MPI_Datatype datatype, int rank, int tag,
	       MPID_Comm * comm, int context_offset,
               MPID_Request ** request)
{
    MPID_Request * rreq;
    int found;
    void * buf = (*buf_handle);
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPID_IZRECV);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_IZRECV);

    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
			"rank=%d, tag=%d, context=%d",
			rank, tag, comm->recvcontext_id + context_offset));

    if (rank == MPI_PROC_NULL)
    {
        MPIDI_Request_create_null_rreq(rreq, mpi_errno, goto fn_fail);
        goto fn_exit;
    }

    /* Check to make sure the communicator hasn't already been revoked */
    if (comm->revoked &&
            MPIR_AGREE_TAG != MPIR_TAG_MASK_ERROR_BITS(tag & ~MPIR_Process.tagged_coll_mask) &&
            MPIR_SHRINK_TAG != MPIR_TAG_MASK_ERROR_BITS(tag & ~MPIR_Process.tagged_coll_mask)) {
        MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"Comm has been revoked. Returning from MPID_IZRECV.");
        MPIR_ERR_SETANDJUMP(mpi_errno,MPIX_ERR_REVOKED,"**revoked");
    }

    MPID_THREAD_CS_ENTER(POBJ, MPIR_THREAD_POBJ_MSGQ_MUTEX);
    rreq = MPIDI_CH3U_Recvq_FDU_or_AEP(rank, tag,
				       comm->recvcontext_id + context_offset,
                                       comm, buf, count, datatype, &found);
    if (rreq == NULL)
    {
	MPID_THREAD_CS_EXIT(POBJ, MPIR_THREAD_POBJ_MSGQ_MUTEX);
	MPIR_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**nomemreq");
    }

    rreq->dev.user_buf_handle	 = buf_handle;
    rreq->dev.user_buf	 = NULL;
    MPIDI_Request_set_self_zerocopy_flag(rreq, TRUE);

    if (found) {
        if ( !(Is_within_same_HWP(rreq->dev.match.parts.rank, comm, NULL)) )
        {
            int rdt_contig;
            MPI_Aint rdt_true_lb;
            MPIDI_msg_sz_t rdata_sz;
            MPID_Datatype * rdt_ptr;
            /* MPIX_Izrecv is receiving from Non-Collocated Sender. Allocating buffer in MPID_Izrecv */
            MPIU_Assert ( MPIDI_Request_get_msg_type(rreq) != MPIDI_REQUEST_SELF_MSG );
            MPIU_Assert ( my_fgrank == rreq->dev.match.parts.dest_rank );

            /* Allocate buffer for MPIX_Izrecv */
            MPIDI_Datatype_get_info(rreq->dev.user_count, rreq->dev.datatype, rdt_contig, rdata_sz, rdt_ptr, rdt_true_lb);
            *(rreq->dev.user_buf_handle) = (void *)MPIU_Malloc(rdata_sz);
            MPIU_Assert(*(rreq->dev.user_buf_handle));
            rreq->dev.user_buf = *(rreq->dev.user_buf_handle);
        }
    }

    if (found)
    {
	MPIDI_VC_t * vc;

	/* Message was found in the unexepected queue */
	MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"request found in unexpected queue");

	/* Release the message queue - we've removed this request from
	   the queue already */
	MPID_THREAD_CS_EXIT(POBJ, MPIR_THREAD_POBJ_MSGQ_MUTEX);

	if (MPIDI_Request_get_msg_type(rreq) == MPIDI_REQUEST_EAGER_MSG)
	{
	    int recv_pending;

	    /* This is an eager message */
	    MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"eager message in the request");

	    /* If this is a eager synchronous message, then we need to send an
	       acknowledgement back to the sender. */
	    if (MPIDI_Request_get_sync_send_flag(rreq))
	    {
		MPIDI_Comm_get_vc_set_active(comm, rreq->dev.match.parts.rank, &vc);
		mpi_errno = MPIDI_CH3_EagerSyncAck( vc, rreq );
		if (mpi_errno) MPIR_ERR_POP(mpi_errno);
	    }

            /* the request was found in the unexpected queue, so it has a
               recv_pending_count of at least 1 */
            MPIDI_Request_decr_pending(rreq);
            MPIDI_Request_check_pending(rreq, &recv_pending);

            if (MPID_Request_is_complete(rreq)) {
                /* is it ever possible to have (cc==0 && recv_pending>0) ? */
                MPIU_Assert(!recv_pending);

                /* All of the data has arrived, we need to copy the data and
                   then free the buffer. */
                if (rreq->dev.recv_data_sz > 0)
                {
                    MPIDI_CH3U_Request_unpack_uebuf(rreq);
                    MPIU_Free(rreq->dev.tmpbuf);
                }

                mpi_errno = rreq->status.MPI_ERROR;
                goto fn_exit;
            }
	    else
	    {
                /* there should never be outstanding completion events for an unexpected
                 * recv without also having a "pending recv" */
                MPIU_Assert(recv_pending);
		/* The data is still being transfered across the net.  We'll
		   leave it to the progress engine to handle once the
		   entire message has arrived. */
		if (HANDLE_GET_KIND(datatype) != HANDLE_KIND_BUILTIN)
		{
		    MPID_Datatype_get_ptr(datatype, rreq->dev.datatype_ptr);
		    MPID_Datatype_add_ref(rreq->dev.datatype_ptr);
		}

	    }
	}
	else if (MPIDI_Request_get_msg_type(rreq) == MPIDI_REQUEST_RNDV_MSG)
	{
	    MPIDI_Comm_get_vc_set_active(comm, rreq->dev.match.parts.rank, &vc);

	    mpi_errno = vc->rndvRecv_fn( vc, rreq );
	    if (mpi_errno) MPIR_ERR_POP( mpi_errno );
	    if (HANDLE_GET_KIND(datatype) != HANDLE_KIND_BUILTIN)
	    {
		MPID_Datatype_get_ptr(datatype, rreq->dev.datatype_ptr);
		MPID_Datatype_add_ref(rreq->dev.datatype_ptr);
	    }
	}
	else if (MPIDI_Request_get_msg_type(rreq) == MPIDI_REQUEST_SELF_MSG)
	{
	    mpi_errno = MPIDI_CH3_RecvFromSelf( rreq, buf_handle, count, datatype );
	    if (mpi_errno) MPIR_ERR_POP(mpi_errno);
	}
	else
	{
	    /* --BEGIN ERROR HANDLING-- */
#ifdef HAVE_ERROR_CHECKING
            int msg_type = MPIDI_Request_get_msg_type(rreq);
#endif
            MPID_Request_release(rreq);
	    rreq = NULL;
	    MPIR_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_INTERN, "**ch3|badmsgtype",
                                 "**ch3|badmsgtype %d", msg_type);
	    /* --END ERROR HANDLING-- */
	}
    }
    else
    {
	/* Message has yet to arrived.  The request has been placed on the
	   list of posted receive requests and populated with
           information supplied in the arguments. */
	MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"request allocated in posted queue");

	if (HANDLE_GET_KIND(datatype) != HANDLE_KIND_BUILTIN)
	{
	    MPID_Datatype_get_ptr(datatype, rreq->dev.datatype_ptr);
	    MPID_Datatype_add_ref(rreq->dev.datatype_ptr);
	}

	rreq->dev.recv_pending_count = 1;

	/* We must wait until here to exit the msgqueue critical section
	   on this request (we needed to set the recv_pending_count
	   and the datatype pointer) */
        MPID_THREAD_CS_EXIT(POBJ, MPIR_THREAD_POBJ_MSGQ_MUTEX);
    }

  fn_exit:
    *request = rreq;
    MPIU_DBG_MSG_P(CH3_OTHER,VERBOSE,"request allocated, handle=0x%08x",
		   rreq->handle);

 fn_fail:
    MPIU_DBG_MSG_D(CH3_OTHER,VERBOSE,"IZRECV errno: 0x%08x", mpi_errno);
    MPIU_DBG_MSG_D(CH3_OTHER,VERBOSE,"(class: %d)", MPIR_ERR_GET_CLASS(mpi_errno));
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_IZRECV);
    return mpi_errno;
}
#endif /* matches #if defined(FINEGRAIN_MPI) */
