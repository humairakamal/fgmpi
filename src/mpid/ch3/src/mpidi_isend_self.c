/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidimpl.h"

/* FIXME: Explain this function */

/* FIXME: should there be a simpler version of this for short messages, 
   particularly contiguous ones?  See also the FIXME about buffering
   short messages */

#undef FUNCNAME
#define FUNCNAME MPIDI_Isend_self
#undef FCNAME
#define FCNAME MPL_QUOTE(FUNCNAME)
#if defined(FINEGRAIN_MPI)
int MPIDI_Isend_self(const void ** buf_handle, MPI_Aint count, MPI_Datatype datatype, int rank, int tag, MPID_Comm * comm, int context_offset,
		     int type, MPID_Request ** request)
#else
int MPIDI_Isend_self(const void * buf, MPI_Aint count, MPI_Datatype datatype, int rank, int tag, MPID_Comm * comm, int context_offset,
		     int type, MPID_Request ** request)
#endif
{
    MPIDI_Message_match match;
    MPID_Request * sreq;
    MPID_Request * rreq;
    MPIDI_VC_t * vc;
#if defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif    
    int found;
    int mpi_errno = MPI_SUCCESS;
#if defined(FINEGRAIN_MPI)
    void * buf = (void *) (*buf_handle);
    int destpid=-1, destworldrank=-1;
    MPIDI_Comm_get_pid_worldrank(comm, rank, &destpid, &destworldrank);
#endif
	
    MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"sending message to self");
	
    MPIDI_Request_create_sreq(sreq, mpi_errno, goto fn_exit);
    MPIDI_Request_set_type(sreq, type);
    MPIDI_Request_set_msg_type(sreq, MPIDI_REQUEST_SELF_MSG);

#if defined(FINEGRAIN_MPI)
    /* FG: Zerocopy */
    if ( MPIDI_REQUEST_TYPE_ZSEND == type ){
        MPIDI_Request_set_self_zerocopy_flag(sreq, TRUE);
        sreq->dev.user_buf_handle  = (void **)buf_handle;
        sreq->dev.user_buf = NULL;
    }
    match.parts.dest_rank = destworldrank;
    match.parts.rank = comm->rank;
#else
    match.parts.rank = rank;
#endif
    match.parts.tag = tag;
    match.parts.context_id = comm->context_id + context_offset;

    MPID_THREAD_CS_ENTER(POBJ, MPIR_THREAD_POBJ_MSGQ_MUTEX);

    rreq = MPIDI_CH3U_Recvq_FDP_or_AEU(&match, &found);
    /* --BEGIN ERROR HANDLING-- */
    if (rreq == NULL)
    {
#if defined(FINEGRAIN_MPI)
        /* FG: Zerocopy */
        if ( MPIDI_Request_get_self_zerocopy_flag(sreq) ){
            /* Free the MPIX_Zsend/Izsend sender's buffer */
            MPIU_Free(buf);
        }
#endif
        /* We release the send request twice, once to release the
         * progress engine reference and the second to release the
         * user reference since the user will never have a chance to
         * release their reference. */
        MPID_Request_release(sreq);
        MPID_Request_release(sreq);
	sreq = NULL;
        MPIR_ERR_SET1(mpi_errno, MPI_ERR_OTHER, "**nomem", 
		      "**nomemuereq %d", MPIDI_CH3U_Recvq_count_unexp());
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    /* If the completion counter is 0, that means that the communicator to
     * which this message is being sent has been revoked and we shouldn't
     * bother finishing this. */
    if (!found && MPID_cc_get(rreq->cc) == 0) {
        /* We release the send request twice, once to release the
         * progress engine reference and the second to release the
         * user reference since the user will never have a chance to
         * release their reference. */
        MPID_Request_release(sreq);
        MPID_Request_release(sreq);
        sreq = NULL;
        goto fn_exit;
    }

#if defined(FINEGRAIN_MPI)
    MPIDI_Comm_get_vc_set_active_direct(comm, destpid, &vc);
#else
    MPIDI_Comm_get_vc_set_active(comm, rank, &vc);
#endif
    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
    MPIDI_Request_set_seqnum(sreq, seqnum);
    MPIDI_Request_set_seqnum(rreq, seqnum);

#if defined(FINEGRAIN_MPI)
    rreq->status.MPI_SOURCE = match.parts.rank;
#else
    rreq->status.MPI_SOURCE = rank;
#endif
    rreq->status.MPI_TAG = tag;
    
    if (found)
    {
	MPIDI_msg_sz_t data_sz;

#if defined(FINEGRAIN_MPI)
        /* FG: Zerocopy
          Pre-posted receive request found. Checking for different sender-receiver
          match pairing scenarios.
        */
        if ( MPIDI_Request_get_self_zerocopy_flag(sreq) && MPIDI_Request_get_self_zerocopy_flag(rreq) )
        {
            int sdt_contig;
            MPI_Aint sdt_true_lb;
            MPID_Datatype * sdt_ptr;

            /* Preposted Recv-Collocated MPIX_Zsend/Izsend - MPIX_Zrecv/Izrecv pairing */
            MPIU_Assert(NULL == rreq->dev.user_buf);
            *(rreq->dev.user_buf_handle) = (void*) (*buf_handle);

            MPIDI_Datatype_get_info(count, datatype, sdt_contig, data_sz, sdt_ptr, sdt_true_lb);

            /* MPIX_Zsend buf_handle can't be set to NULL as we don't have
               a ptr to void**. */
        }
        else if( MPIDI_Request_get_self_zerocopy_flag(sreq) && !MPIDI_Request_get_self_zerocopy_flag(rreq) ){
            /* Preposted Recv-Collocated MPIX_Zsend/Izsend - MPI_Recv/Irecv pairing. Freeing Sender's buffer */
            MPIDI_CH3U_Buffer_copy(buf, count, datatype, &sreq->status.MPI_ERROR,
                                   rreq->dev.user_buf, rreq->dev.user_count, rreq->dev.datatype, &data_sz, &rreq->status.MPI_ERROR);
            /* Free the sender's buffer */
            MPIU_Free(buf);
        }
        else if( !MPIDI_Request_get_self_zerocopy_flag(sreq) && MPIDI_Request_get_self_zerocopy_flag(rreq) ){
            /* Preposted Recv-Collocated MPI_Send/Isend - MPIX_Zrecv/Izrecv pairing. Allocating receiver's buffer. */
            MPIU_Assert(NULL == rreq->dev.user_buf);
            /* Added checks for buffer count size as is done in MPIDI_CH3U_Buffer_copy() */
            MPIDI_CH3U_Buffer_allocate(buf, count, datatype, &sreq->status.MPI_ERROR,
                                       rreq->dev.user_buf_handle, rreq->dev.user_count, rreq->dev.datatype, &data_sz, &rreq->status.MPI_ERROR);
            MPIDI_CH3U_Buffer_copy(buf, count, datatype, &sreq->status.MPI_ERROR,
                                   *(rreq->dev.user_buf_handle), rreq->dev.user_count, rreq->dev.datatype, &data_sz, &rreq->status.MPI_ERROR);
        } else {
            /* Preposted Recv-Collocated MPI_Send/Isend - MPI_Recv/Irecv pairing */
#endif /* matches #if defined(FINEGRAIN_MPI) */

        /* we found a posted req, which we now own, so we can release the CS */
        MPID_THREAD_CS_EXIT(POBJ, MPIR_THREAD_POBJ_MSGQ_MUTEX);

	MPIU_DBG_MSG(CH3_OTHER,VERBOSE,
		     "found posted receive request; copying data");

	MPIDI_CH3U_Buffer_copy(buf, count, datatype, &sreq->status.MPI_ERROR,
			       rreq->dev.user_buf, rreq->dev.user_count, rreq->dev.datatype, &data_sz, &rreq->status.MPI_ERROR);
#if defined(FINEGRAIN_MPI)
        }
#endif

	MPIR_STATUS_SET_COUNT(rreq->status, data_sz);
        mpi_errno = MPID_Request_complete(rreq);
        if (mpi_errno != MPI_SUCCESS) {
            MPIR_ERR_POP(mpi_errno);
        }

        mpi_errno = MPID_Request_complete(sreq);
        if (mpi_errno != MPI_SUCCESS) {
            MPIR_ERR_POP(mpi_errno);
        }
    }
    else
    {
	if (type != MPIDI_REQUEST_TYPE_RSEND)
	{
	    MPI_Aint dt_sz;
	
	    /* FIXME: Insert code here to buffer small sends in a temporary buffer? */

	    MPIU_DBG_MSG(CH3_OTHER,VERBOSE,
          "added receive request to unexpected queue; attaching send request");
	    if (HANDLE_GET_KIND(datatype) != HANDLE_KIND_BUILTIN)
	    {
		MPID_Datatype_get_ptr(datatype, sreq->dev.datatype_ptr);
		MPID_Datatype_add_ref(sreq->dev.datatype_ptr);
	    }
	    rreq->partner_request = sreq;
	    rreq->dev.sender_req_id = sreq->handle;
	    MPID_Datatype_get_size_macro(datatype, dt_sz);
	    MPIR_STATUS_SET_COUNT(rreq->status, count * dt_sz);
	}
	else
	{
	    /* --BEGIN ERROR HANDLING-- */
	    MPIU_DBG_MSG(CH3_OTHER,TYPICAL,
			 "ready send unable to find matching recv req");
	    sreq->status.MPI_ERROR = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER,
							  "**rsendnomatch", "**rsendnomatch %d %d", rank, tag);
	    rreq->status.MPI_ERROR = sreq->status.MPI_ERROR;
	    
	    rreq->partner_request = NULL;
	    rreq->dev.sender_req_id = MPI_REQUEST_NULL;
	    MPIR_STATUS_SET_COUNT(rreq->status, 0);
	    
	    /* sreq has never been seen by the user or outside this thread, so it is safe to reset ref_count and cc */
            mpi_errno = MPID_Request_complete(sreq);
            if (mpi_errno != MPI_SUCCESS) {
                MPIR_ERR_POP(mpi_errno);
            }
	    /* --END ERROR HANDLING-- */
	}
	    
	MPIDI_Request_set_msg_type(rreq, MPIDI_REQUEST_SELF_MSG);

        /* can release now that we've set fields in the unexpected request */
        MPID_THREAD_CS_EXIT(POBJ, MPIR_THREAD_POBJ_MSGQ_MUTEX);

        /* kick the progress engine in case another thread that is performing a
           blocking recv or probe is waiting in the progress engine */
	MPIDI_CH3_Progress_signal_completion();
    }

  fn_exit:
    *request = sreq;

    return mpi_errno;
 fn_fail:
    goto fn_exit;
}
