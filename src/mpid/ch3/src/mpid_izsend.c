/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *
 *      See COPYRIGHT in top-level directory.
 */
#if defined(FINEGRAIN_MPI)
#include "mpidimpl.h"

/* FIXME: HOMOGENEOUS SYSTEMS ONLY -- no data conversion is performed */

/*
 * MPID_Izsend()
 */
#undef FUNCNAME
#define FUNCNAME MPID_Izsend
#undef FCNAME
#define FCNAME MPL_QUOTE(FUNCNAME)
int MPID_Izsend(const void ** buf_handle, MPI_Aint count, MPI_Datatype datatype, int rank,
	       int tag, MPID_Comm * comm, int context_offset,
               MPID_Request ** request)
{
    MPIDI_msg_sz_t data_sz;
    int dt_contig;
    MPI_Aint dt_true_lb;
    MPID_Datatype * dt_ptr;
    MPID_Request * sreq = NULL;
    MPIDI_VC_t * vc=0;
#if defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif
    int eager_threshold = -1;
    int mpi_errno = MPI_SUCCESS;
    void* buf = (void*) (*buf_handle);
    int destpid=-1, destworldrank=-1;

    MPIDI_STATE_DECL(MPID_STATE_MPID_IZSEND);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_IZSEND);

    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
                  "rank=%d, tag=%d, context=%d",
                  rank, tag, comm->context_id + context_offset));

    /* Check to make sure the communicator hasn't already been revoked */
    if (comm->revoked &&
            MPIR_AGREE_TAG != MPIR_TAG_MASK_ERROR_BITS(tag & ~MPIR_Process.tagged_coll_mask) &&
            MPIR_SHRINK_TAG != MPIR_TAG_MASK_ERROR_BITS(tag & ~MPIR_Process.tagged_coll_mask)) {
        MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"Communicator revoked. MPID_IZSEND returning");
        MPIR_ERR_SETANDJUMP(mpi_errno,MPIX_ERR_REVOKED,"**revoked");
    }

    MPIDI_Comm_get_pid_worldrank(comm, rank, &destpid, &destworldrank);
    if (COMPARE_RANKS(rank,comm,destpid) && comm->comm_kind != MPID_INTERCOMM)
    {
        mpi_errno = MPIDI_Isend_self(buf_handle, count, datatype, rank, tag, comm,
			    context_offset, MPIDI_REQUEST_TYPE_ZSEND, &sreq);
	goto fn_exit;
    }

    if (rank != MPI_PROC_NULL) {
        MPIDI_Comm_get_vc_set_active_direct(comm, destpid, &vc);
    }

    MPIDI_Request_create_sreq(sreq, mpi_errno, goto fn_exit);
    MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_SEND);
    MPIDI_Request_set_self_zerocopy_flag(sreq, TRUE);
    sreq->dev.user_buf_handle  = (void **)buf_handle;

    if (rank == MPI_PROC_NULL)
    {
	MPIU_Object_set_ref(sreq, 1);
        MPID_cc_set(&sreq->cc, 0);
	goto fn_exit;
    }

    MPIDI_Datatype_get_info(count, datatype, dt_contig, data_sz, dt_ptr,
			    dt_true_lb);

    if (data_sz == 0)
    {
	MPIDI_CH3_Pkt_t upkt;
	MPIDI_CH3_Pkt_eager_send_t * const eager_pkt = &upkt.eager_send;

	MPIDI_Request_set_msg_type(sreq, MPIDI_REQUEST_EAGER_MSG);
	sreq->dev.OnDataAvail = 0;

	MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"sending zero length message");
	MPIDI_Pkt_init(eager_pkt, MPIDI_CH3_PKT_EAGER_SEND);

        eager_pkt->match.parts.dest_rank = destworldrank;
	eager_pkt->match.parts.rank = comm->rank;
	eager_pkt->match.parts.tag = tag;
	eager_pkt->match.parts.context_id = comm->context_id + context_offset;
	eager_pkt->sender_req_id = sreq->handle;
	eager_pkt->data_sz = 0;

	MPIDI_VC_FAI_send_seqnum(vc, seqnum);
	MPIDI_Pkt_set_seqnum(eager_pkt, seqnum);
	MPIDI_Request_set_seqnum(sreq, seqnum);

	MPID_THREAD_CS_ENTER(POBJ, vc->pobj_mutex);
	mpi_errno = MPIDI_CH3_iSend(vc, sreq, eager_pkt, sizeof(*eager_pkt));
	MPID_THREAD_CS_EXIT(POBJ, vc->pobj_mutex);
	/* --BEGIN ERROR HANDLING-- */
	if (mpi_errno != MPI_SUCCESS)
	{
            MPID_Request_release(sreq);
	    sreq = NULL;
            MPIR_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**ch3|eagermsg");
	    goto fn_exit;
	}
	/* --END ERROR HANDLING-- */

	goto fn_exit;
    }

    MPIDI_CH3_GET_EAGER_THRESHOLD(&eager_threshold, comm, vc);

    /* FIXME: flow control: limit number of outstanding eager messages
       containing data and need to be buffered by the receiver */
    if (data_sz + sizeof(MPIDI_CH3_Pkt_eager_send_t) <= eager_threshold)
    {
	if (dt_contig)
	{
	    mpi_errno = MPIDI_CH3_EagerContigIsend( &sreq,
						    MPIDI_CH3_PKT_EAGER_SEND,
						    (char*)buf + dt_true_lb,
						    data_sz, rank, tag,
						    comm, context_offset );
	}
	else
	{
	    mpi_errno = MPIDI_CH3_EagerNoncontigSend( &sreq,
                                                      MPIDI_CH3_PKT_EAGER_SEND,
                                                      buf, count, datatype,
                                                      data_sz, rank, tag,
                                                      comm, context_offset );
	    /* If we're not complete, then add a reference to the datatype */
	    if (sreq && sreq->dev.OnDataAvail) {
		sreq->dev.datatype_ptr = dt_ptr;
		MPID_Datatype_add_ref(dt_ptr);
	    }
	}
    }
    else
    {
	/* Note that the sreq was created above */
	MPIDI_Request_set_msg_type( sreq, MPIDI_REQUEST_RNDV_MSG );
	mpi_errno = vc->rndvSend_fn( &sreq, buf, count, datatype, dt_contig,
                                     data_sz, dt_true_lb, rank, tag, comm,
                                     context_offset );
	/* FIXME: fill temporary IOV or pack temporary buffer after send to
	   hide some latency.  This requires synchronization
           because the CTS packet could arrive and be processed before the
	   above iStartmsg completes (depending on the progress
           engine, threads, etc.). */

	if (sreq && dt_ptr != NULL)
	{
	    sreq->dev.datatype_ptr = dt_ptr;
	    MPID_Datatype_add_ref(dt_ptr);
	}
    }

  fn_exit:
    *request = sreq;

    MPIU_DBG_STMT(CH3_OTHER,VERBOSE,
    {
	if (sreq != NULL)
	{
	    MPIU_DBG_MSG_P(CH3_OTHER,VERBOSE,"request allocated, handle=0x%08x", sreq->handle);
	}
    }
		  );

  fn_fail:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_IZSEND);
    return mpi_errno;
}
#endif /* matches #if defined(FINEGRAIN_MPI) */