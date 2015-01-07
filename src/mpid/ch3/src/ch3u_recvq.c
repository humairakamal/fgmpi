/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidimpl.h"
#include "mpidi_recvq_statistics.h"

/* MPIDI_POSTED_RECV_ENQUEUE_HOOK(req): Notifies channel that req has
   been enqueued on the posted recv queue.  Returns void. */
#ifndef MPIDI_POSTED_RECV_ENQUEUE_HOOK
#define MPIDI_POSTED_RECV_ENQUEUE_HOOK(req) do{}while(0)
#endif
/* MPIDI_POSTED_RECV_DEQUEUE_HOOK(req): Notifies channel that req has
   been dequeued from the posted recv queue.  Returns non-zero if the
   channel has already matched the request; 0 otherwise.  This happens
   when the channel supports shared-memory and network communication
   with a network capable of matching, and the same request is matched
   by the network and, e.g., shared-memory.  When that happens the
   dequeue functions below should, either search for the next matching
   request, or report that no request was found. */
#ifndef MPIDI_POSTED_RECV_DEQUEUE_HOOK
#define MPIDI_POSTED_RECV_DEQUEUE_HOOK(req) 0
#endif

/* FIXME: 
 * Recvq_lock/unlock removed because it is not needed for the SINGLE_CS
 * approach and we might want a different, non-lock-based approach in 
 * a finer-grained thread-sync version.  For example, 
 * some routines can be implemented in a lock-free
 * fashion (because the user is required to guarantee non-conflicting 
 * accesses, such as doing a probe and a receive that matches in different 
 * threads).  
 *
 * There are a lot of routines here.  Do we really need them all?
 * 
 * The search criteria can be implemented in a single 64 bit compare on
 * systems with efficient 64-bit operations.  The rank and contextid can also
 * in many cases be combined into a single 32-bit word for the comparison
 * (in which case the message info should be stored in the queue in a 
 * naturally aligned, 64-bit word.
 * 
 */

#if defined(FINEGRAIN_MPI)
#define GET_MATCH_QUEUE_INDEX(matchrank_, index_ptr_)  do { \
        int fgstartrank, nfg;                               \
        MPIX_Get_collocated_startrank(&fgstartrank);        \
        MPIX_Get_collocated_size(&nfg);                     \
        *(index_ptr_) = (matchrank_ - fgstartrank);         \
        MPIU_Assert ( (*(index_ptr_) >= 0) && (*(index_ptr_) < nfg) );   \
    } while (0)
/* FG: Some parts of the process group, channel and net module initialization
   is to be done only once per HWP.
   Here, we record the state of initialization. The first FGP will complete
   the initialization, before other FGPs are allowed to enter InitPG() and
   subsequent functions. */
MPID_Request ** FG_recvq_posted_head = 0;
MPID_Request ** FG_recvq_posted_tail = 0;
MPID_Request ** FG_recvq_unexpected_head = 0;
MPID_Request ** FG_recvq_unexpected_tail = 0;
#else
static MPID_Request * recvq_posted_head = 0;
static MPID_Request * recvq_posted_tail = 0;
static MPID_Request * recvq_unexpected_head = 0;
static MPID_Request * recvq_unexpected_tail = 0;
#endif

/* Export the location of the queue heads if debugger support is enabled.
 * This allows the queue code to rely on the local variables for the
 * queue heads while also exporting those variables to the debugger.
 * See src/mpi/debugger/dll_mpich.c for how this is used to 
 * access the message queues.
 */
#ifdef HAVE_DEBUGGER_SUPPORT
#if defined(FINEGRAIN_MPI) /* FG: TODO */
MPID_Request ** const MPID_Recvq_posted_head_ptr     = NULL;
MPID_Request ** const MPID_Recvq_unexpected_head_ptr = NULL;
#else
MPID_Request ** const MPID_Recvq_posted_head_ptr     = &recvq_posted_head;
MPID_Request ** const MPID_Recvq_unexpected_head_ptr = &recvq_unexpected_head;
#endif
#endif

/* If the MPIDI_Message_match structure fits into a pointer size, we
 * can directly work on it */
/* MATCH_WITH_NO_MASK compares the match values without masking
 * them. This is useful for the case where there are no ANY_TAG or
 * ANY_SOURCE wild cards. */
#if defined(FINEGRAIN_MPI) /* FG: The dest_rank comparisons are redundant since we are
                              already looking the FG_recvq_xxx queue corresponding to dest_rank.*/
#define MATCH_WITH_NO_MASK(match1, match2)                              \
    ((sizeof(MPIDI_Message_match) == SIZEOF_VOID_P) ? ((match1).whole == (match2).whole) : \
    (((match1).parts.dest_rank == (match2).parts.dest_rank) &&                    \
     ((match1).parts.rank == (match2).parts.rank) &&                   \
      ((match1).parts.tag == (match2).parts.tag) &&                     \
      ((match1).parts.context_id == (match2).parts.context_id)))
#else
#define MATCH_WITH_NO_MASK(match1, match2)                              \
    ((sizeof(MPIDI_Message_match) == SIZEOF_VOID_P) ? ((match1).whole == (match2).whole) : \
     (((match1).parts.rank == (match2).parts.rank) &&                   \
      ((match1).parts.tag == (match2).parts.tag) &&                     \
      ((match1).parts.context_id == (match2).parts.context_id)))
#endif

/* MATCH_WITH_LEFT_MASK compares the match values after masking only
 * the left field. This is useful for the case where the right match
 * is a part of the unexpected queue and has no ANY_TAG or ANY_SOURCE
 * wild cards, but the left match might have them. */
#if defined(FINEGRAIN_MPI)
#define MATCH_WITH_LEFT_MASK(match1, match2, mask)                      \
    ((sizeof(MPIDI_Message_match) == SIZEOF_VOID_P) ?                   \
     (((match1).whole & (mask).whole) == (match2).whole) :              \
     ((((match1).parts.dest_rank & (mask).parts.dest_rank) == (match2).parts.dest_rank) && \
     (((match1).parts.rank & (mask).parts.rank) == (match2).parts.rank) && \
      (((match1).parts.tag & (mask).parts.tag) == (match2).parts.tag) && \
      ((match1).parts.context_id == (match2).parts.context_id)))
#else
#define MATCH_WITH_LEFT_MASK(match1, match2, mask)                      \
    ((sizeof(MPIDI_Message_match) == SIZEOF_VOID_P) ?                   \
     (((match1).whole & (mask).whole) == (match2).whole) :              \
     ((((match1).parts.rank & (mask).parts.rank) == (match2).parts.rank) && \
      (((match1).parts.tag & (mask).parts.tag) == (match2).parts.tag) && \
      ((match1).parts.context_id == (match2).parts.context_id)))
#endif

/* This is the most general case where both matches have to be
 * masked. Both matches are masked with the same value. There doesn't
 * seem to be a need for two different masks at this time. */
#if defined(FINEGRAIN_MPI)
#define MATCH_WITH_LEFT_RIGHT_MASK(match1, match2, mask)                \
    ((sizeof(MPIDI_Message_match) == SIZEOF_VOID_P) ?                   \
     (((match1).whole & (mask).whole) == ((match2).whole & (mask).whole)) : \
     ((((match1).parts.dest_rank & (mask).parts.dest_rank) == ((match2).parts.dest_rank & (mask).parts.dest_rank)) && \
     (((match1).parts.rank & (mask).parts.rank) == ((match2).parts.rank & (mask).parts.rank)) && \
      (((match1).parts.tag & (mask).parts.tag) == ((match2).parts.tag & (mask).parts.tag)) && \
      ((match1).parts.context_id == (match2).parts.context_id)))
#else
#define MATCH_WITH_LEFT_RIGHT_MASK(match1, match2, mask)                \
    ((sizeof(MPIDI_Message_match) == SIZEOF_VOID_P) ?                   \
     (((match1).whole & (mask).whole) == ((match2).whole & (mask).whole)) : \
     ((((match1).parts.rank & (mask).parts.rank) == ((match2).parts.rank & (mask).parts.rank)) && \
      (((match1).parts.tag & (mask).parts.tag) == ((match2).parts.tag & (mask).parts.tag)) && \
      ((match1).parts.context_id == (match2).parts.context_id)))
#endif

MPIR_T_PVAR_UINT_LEVEL_DECL_STATIC(RECVQ, posted_recvq_length);
MPIR_T_PVAR_UINT_LEVEL_DECL_STATIC(RECVQ, unexpected_recvq_length);
MPIR_T_PVAR_ULONG2_COUNTER_DECL_STATIC(RECVQ, posted_recvq_match_attempts);
MPIR_T_PVAR_ULONG2_COUNTER_DECL_STATIC(RECVQ, unexpected_recvq_match_attempts);
MPIR_T_PVAR_DOUBLE_TIMER_DECL_STATIC(RECVQ, time_failed_matching_postedq);
MPIR_T_PVAR_DOUBLE_TIMER_DECL_STATIC(RECVQ, time_matching_unexpectedq);

/* used in ch3u_eager.c and ch3u_handle_recv_pkt.c */
MPIR_T_PVAR_ULONG2_LEVEL_DECL(RECVQ, unexpected_recvq_buffer_size);

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Recvq_init
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIDI_CH3U_Recvq_init(void)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_T_PVAR_LEVEL_REGISTER_STATIC(
        RECVQ,
        MPI_UNSIGNED,
        posted_recvq_length,
        0, /* init value */
        MPI_T_VERBOSITY_USER_DETAIL,
        MPI_T_BIND_NO_OBJECT,
        (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
        "CH3", /* category name */
        "length of the posted message receive queue");

    MPIR_T_PVAR_LEVEL_REGISTER_STATIC(
        RECVQ,
        MPI_UNSIGNED,
        unexpected_recvq_length,
        0, /* init value */
        MPI_T_VERBOSITY_USER_DETAIL,
        MPI_T_BIND_NO_OBJECT,
        (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
        "CH3", /* category name */
        "length of the unexpected message receive queue");

    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
        RECVQ,
        MPI_UNSIGNED_LONG_LONG,
        posted_recvq_match_attempts,
        MPI_T_VERBOSITY_USER_DETAIL,
        MPI_T_BIND_NO_OBJECT,
        (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
        "CH3", /* category name */
        "number of search passes on the posted message receive queue");

    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
        RECVQ,
        MPI_UNSIGNED_LONG_LONG,
        unexpected_recvq_match_attempts,
        MPI_T_VERBOSITY_USER_DETAIL,
        MPI_T_BIND_NO_OBJECT,
        (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
        "CH3",
        "number of search passes on the unexpected message receive queue");

    MPIR_T_PVAR_TIMER_REGISTER_STATIC(
        RECVQ,
        MPI_DOUBLE,
        time_failed_matching_postedq,
        MPI_T_VERBOSITY_USER_DETAIL,
        MPI_T_BIND_NO_OBJECT,
        (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
        "CH3", /* category name */
        "total time spent on unsuccessful search passes on the posted receives queue");

    MPIR_T_PVAR_TIMER_REGISTER_STATIC(
        RECVQ,
        MPI_DOUBLE,
        time_matching_unexpectedq,
        MPI_T_VERBOSITY_USER_DETAIL,
        MPI_T_BIND_NO_OBJECT,
        (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
        "CH3", /* category name */
        "total time spent on search passes on the unexpected receive queue");

    MPIR_T_PVAR_LEVEL_REGISTER_STATIC(
        RECVQ,
        MPI_UNSIGNED_LONG_LONG,
        unexpected_recvq_buffer_size,
        0, /* init value */
        MPI_T_VERBOSITY_USER_DETAIL,
        MPI_T_BIND_NO_OBJECT,
        (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
        "CH3", /* category name */
        "total buffer size allocated in the unexpected receive queue");

fn_fail:
    return mpi_errno;
}

/* FIXME: If this routine is only used by probe/iprobe, then we don't need
   to set the cancelled field in status (only set for nonblocking requests) */
/*
 * MPIDI_CH3U_Recvq_FU()
 *
 * Search for a matching request in the unexpected receive queue.  Return 
 * true if one is found, false otherwise.  If the status argument is
 * not MPI_STATUS_IGNORE, return information about the request in that
 * parameter.  This routine is used by mpid_probe and mpid_iprobe.
 *
 * Multithread - As this is a read-only routine, it need not
 * require an external critical section (careful organization of the
 * queue updates would not even require a critical section within this
 * routine).  However, this routine is used both from within the progress
 * engine and from without it.  To make that work with the current
 * design for MSGQUEUE and the brief-global mode, the critical section 
 * is *outside* of this routine.
 *
 * This routine is used only in mpid_iprobe and mpid_probe
 *
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Recvq_FU
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3U_Recvq_FU(int source, int tag, int context_id, MPI_Status *s)
{
    MPID_Request * rreq;
    int found = 0;
    MPIDI_Message_match match, mask;
#if defined(FINEGRAIN_MPI)
    int fg_offset;
#endif
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3U_RECVQ_FU);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3U_RECVQ_FU);

    MPIU_THREAD_CS_ASSERT_HELD(MSGQUEUE);

#if defined(FINEGRAIN_MPI)
    GET_MATCH_QUEUE_INDEX(my_fgrank, &fg_offset);
    rreq = FG_recvq_unexpected_head[fg_offset];
#else
    rreq = recvq_unexpected_head;
#endif

    match.parts.context_id = context_id;
    match.parts.tag = tag;
    match.parts.rank = source;
#if defined(FINEGRAIN_MPI)
    match.parts.dest_rank = my_fgrank;
    mask.parts.dest_rank = ~0;
#endif

    mask.parts.context_id = mask.parts.rank = mask.parts.tag = ~0;
    /* Mask the error bit that might be set on incoming messages. It is
     * assumed that the local receive operation won't have the error bit set
     * (or it is masked away at some other level). */
    MPIR_TAG_CLEAR_ERROR_BITS(mask.parts.tag);
    if (tag != MPI_ANY_TAG && source != MPI_ANY_SOURCE) {
        MPIR_T_PVAR_TIMER_START(RECVQ, time_matching_unexpectedq);
	while (rreq != NULL) {
        MPIR_T_PVAR_COUNTER_INC(RECVQ, unexpected_recvq_match_attempts, 1);
	    if (MATCH_WITH_LEFT_MASK(rreq->dev.match, match, mask))
		break;
	    rreq = rreq->dev.next;
	}
        MPIR_T_PVAR_TIMER_END(RECVQ, time_matching_unexpectedq);
    }
    else {
	if (tag == MPI_ANY_TAG)
	    match.parts.tag = mask.parts.tag = 0;
	if (source == MPI_ANY_SOURCE)
	    match.parts.rank = mask.parts.rank = 0;

        MPIR_T_PVAR_TIMER_START(RECVQ, time_matching_unexpectedq);
	while (rreq != NULL) {
        MPIR_T_PVAR_COUNTER_INC(RECVQ, unexpected_recvq_match_attempts, 1);
	    if (MATCH_WITH_LEFT_MASK(rreq->dev.match, match, mask))
		break;
	    rreq = rreq->dev.next;
	}
        MPIR_T_PVAR_TIMER_END(RECVQ, time_matching_unexpectedq);
    }

    /* Save the information about the request before releasing the 
       queue */
    if (rreq) {
	if (s != MPI_STATUS_IGNORE) {
	    /* Avoid setting "extra" fields like MPI_ERROR */
	    s->MPI_SOURCE = rreq->status.MPI_SOURCE;
	    s->MPI_TAG    = rreq->status.MPI_TAG;
            MPIR_STATUS_SET_COUNT(*s, MPIR_STATUS_GET_COUNT(rreq->status));
            MPIR_STATUS_SET_CANCEL_BIT(*s, MPIR_STATUS_GET_CANCEL_BIT(rreq->status));
	}
	found = 1;
    }

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3U_RECVQ_FU);
    return found;
}

/*
 * MPIDI_CH3U_Recvq_FDU()
 *
 * Find a request in the unexpected queue and dequeue it; otherwise return NULL.
 *
 * Multithread - This routine must be atomic (since it dequeues a
 * request).  However, once the request is dequeued, no other thread can
 * see it, so this routine provides its own atomicity.
 *
 * This routine is used only in the case of send_cancel.  However, it is used both
 * within mpid_send_cancel and within a packet handler.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Recvq_FDU
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
MPID_Request * MPIDI_CH3U_Recvq_FDU(MPI_Request sreq_id, 
				    MPIDI_Message_match * match)
{
    MPID_Request * rreq;
    MPID_Request * prev_rreq;
    MPID_Request * cur_rreq;
    MPID_Request * matching_prev_rreq;
    MPID_Request * matching_cur_rreq;
    MPIDI_Message_match mask;
#if defined(FINEGRAIN_MPI)
    int fg_offset;
#endif
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3U_RECVQ_FDU);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3U_RECVQ_FDU);

    MPIU_THREAD_CS_ASSERT_HELD(MSGQUEUE);

    matching_prev_rreq = NULL;
    matching_cur_rreq = NULL;
    prev_rreq = NULL;

    mask.parts.context_id = mask.parts.rank = mask.parts.tag = ~0;
    /* Mask the error bit that might be set on incoming messages. It is
     * assumed that the local receive operation won't have the error bit set
     * (or it is masked away at some other level). */
    MPIR_TAG_CLEAR_ERROR_BITS(mask.parts.tag);

    /* Note that since this routine is used only in the case of send_cancel,
       there can be only one match if at all. */
    /* FIXME: Why doesn't this exit after it finds the first match? */
#if defined(FINEGRAIN_MPI)
    GET_MATCH_QUEUE_INDEX(match->parts.dest_rank, &fg_offset);
    cur_rreq = FG_recvq_unexpected_head[fg_offset];
    mask.parts.dest_rank = ~0;
#else
    cur_rreq = recvq_unexpected_head;
#endif
    while (cur_rreq != NULL) {
        MPIR_T_PVAR_TIMER_START(RECVQ, time_matching_unexpectedq);

        if (cur_rreq->dev.sender_req_id == sreq_id) {
            MPIR_T_PVAR_COUNTER_INC(RECVQ, unexpected_recvq_match_attempts, 1);
            if (MATCH_WITH_LEFT_MASK(cur_rreq->dev.match, *match, mask)) {
                matching_prev_rreq = prev_rreq;
                matching_cur_rreq = cur_rreq;
            }
	    }

        MPIR_T_PVAR_TIMER_END(RECVQ, time_matching_unexpectedq);

	prev_rreq = cur_rreq;
	cur_rreq = cur_rreq->dev.next;
    }

    if (matching_cur_rreq != NULL) {
	if (matching_prev_rreq != NULL) {
	    matching_prev_rreq->dev.next = matching_cur_rreq->dev.next;
	}
	else {
#if defined(FINEGRAIN_MPI)
            FG_recvq_unexpected_head[fg_offset] = matching_cur_rreq->dev.next;
#else
	    recvq_unexpected_head = matching_cur_rreq->dev.next;
#endif
	}
	
	if (matching_cur_rreq->dev.next == NULL) {
#if defined(FINEGRAIN_MPI)
            FG_recvq_unexpected_tail[fg_offset] = matching_prev_rreq;
#else
	    recvq_unexpected_tail = matching_prev_rreq;
#endif
	}

    MPIR_T_PVAR_LEVEL_DEC(RECVQ, unexpected_recvq_length, 1);
	rreq = matching_cur_rreq;

        MPIR_T_PVAR_LEVEL_DEC(RECVQ, unexpected_recvq_buffer_size, rreq->dev.tmpbuf_sz);
    }
    else {
	rreq = NULL;
    }

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3U_RECVQ_FDU);
    return rreq;
}

/* TODO rename the old FDU and use that name for this one */
/* This is the routine that you expect to be named "_FDU".  It implements the
 * behavior needed for improbe; specifically, searching the receive queue for
 * the first matching request and dequeueing it. */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Recvq_FDU_matchonly
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
MPID_Request * MPIDI_CH3U_Recvq_FDU_matchonly(int source, int tag, int context_id, MPID_Comm *comm, int *foundp)
{
    int found = FALSE;
    MPID_Request *rreq, *prev_rreq;
    MPIDI_Message_match match;
    MPIDI_Message_match mask;
#if defined(FINEGRAIN_MPI)
    int fg_offset;
#endif
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3U_RECVQ_FDU_MATCHONLY);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3U_RECVQ_FDU_MATCHONLY);

    MPIU_THREAD_CS_ASSERT_HELD(MSGQUEUE);

    /* Store how much time is spent traversing the queue */
    MPIR_T_PVAR_TIMER_START(RECVQ, time_matching_unexpectedq);

#if defined(FINEGRAIN_MPI)
    GET_MATCH_QUEUE_INDEX(my_fgrank, &fg_offset);
    rreq = FG_recvq_unexpected_head[fg_offset];
#else
    /* Optimize this loop for an empty unexpected receive queue */
    rreq = recvq_unexpected_head;
#endif
    if (rreq) {
        prev_rreq = NULL;

        match.parts.context_id = context_id;
        match.parts.tag = tag;
        match.parts.rank = source;
#if defined(FINEGRAIN_MPI)
        match.parts.dest_rank = my_fgrank;
        mask.parts.dest_rank = ~0;
#endif

        mask.parts.context_id = mask.parts.rank = mask.parts.tag = ~0;
        /* Mask the error bit that might be set on incoming messages. It is
         * assumed that the local receive operation won't have the error bit set
         * (or it is masked away at some other level). */
        MPIR_TAG_CLEAR_ERROR_BITS(mask.parts.tag);

        if (tag != MPI_ANY_TAG && source != MPI_ANY_SOURCE) {
            do {
                MPIR_T_PVAR_COUNTER_INC(RECVQ, unexpected_recvq_match_attempts, 1);
                if (MATCH_WITH_LEFT_MASK(rreq->dev.match, match, mask)) {
                    if (prev_rreq != NULL) {
                        prev_rreq->dev.next = rreq->dev.next;
                    }
                    else {
#if defined(FINEGRAIN_MPI)
                        FG_recvq_unexpected_head[fg_offset] = rreq->dev.next;
#else
                        recvq_unexpected_head = rreq->dev.next;
#endif
                    }

                    if (rreq->dev.next == NULL) {
#if defined(FINEGRAIN_MPI)
                        FG_recvq_unexpected_tail[fg_offset] = prev_rreq;
#else
                        recvq_unexpected_tail = prev_rreq;
#endif
                    }
                    MPIR_T_PVAR_LEVEL_DEC(RECVQ, unexpected_recvq_length, 1);
                    MPIR_T_PVAR_LEVEL_DEC(RECVQ, unexpected_recvq_buffer_size, rreq->dev.tmpbuf_sz);

                    rreq->comm = comm;
                    MPIR_Comm_add_ref(comm);
                    /* don't have the (buf,count,type) info right now, can't add
                     * it to the request */
                    found = TRUE;
                    goto lock_exit;
                }
                prev_rreq = rreq;
                rreq      = rreq->dev.next;
            } while (rreq);
        }
        else {
            if (tag == MPI_ANY_TAG)
                match.parts.tag = mask.parts.tag = 0;
            if (source == MPI_ANY_SOURCE)
                match.parts.rank = mask.parts.rank = 0;

            do {
                MPIR_T_PVAR_COUNTER_INC(RECVQ, unexpected_recvq_match_attempts, 1);
                if (MATCH_WITH_LEFT_MASK(rreq->dev.match, match, mask)) {
                    if (prev_rreq != NULL) {
                        prev_rreq->dev.next = rreq->dev.next;
                    }
                    else {
#if defined(FINEGRAIN_MPI)
                        FG_recvq_unexpected_head[fg_offset] = rreq->dev.next;
#else
                        recvq_unexpected_head = rreq->dev.next;
#endif
                    }
                    if (rreq->dev.next == NULL) {
#if defined(FINEGRAIN_MPI)
                        FG_recvq_unexpected_tail[fg_offset] = prev_rreq;
#else
                        recvq_unexpected_tail = prev_rreq;
#endif
                    }
                    MPIR_T_PVAR_LEVEL_DEC(RECVQ, unexpected_recvq_length, 1);
                    MPIR_T_PVAR_LEVEL_DEC(RECVQ, unexpected_recvq_buffer_size, rreq->dev.tmpbuf_sz);

                    rreq->comm                 = comm;
                    MPIR_Comm_add_ref(comm);
                    /* don't have the (buf,count,type) info right now, can't add
                     * it to the request */
                    found = TRUE;
                    goto lock_exit;
                }
                prev_rreq = rreq;
                rreq = rreq->dev.next;
            } while (rreq);
        }
    }

lock_exit:
    MPIR_T_PVAR_TIMER_END(RECVQ, time_matching_unexpectedq);

    *foundp = found;

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3U_RECVQ_FDU_MATCHONLY);
    return rreq;
}

/*
 * MPIDI_CH3U_Recvq_FDU_or_AEP()
 *
 * Atomically find a request in the unexpected queue and dequeue it, or 
 * allocate a new request and enqueue it in the posted queue
 *
 * Multithread - This routine must be called from within a MSGQUEUE 
 * critical section.  If a request is allocated, it must not release
 * the MSGQUEUE until the request is completely valid, as another thread
 * may then find it and dequeue it.
 *
 * This routine is used in mpid_irecv and mpid_recv.
 *
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Recvq_FDU_or_AEP
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
MPID_Request * MPIDI_CH3U_Recvq_FDU_or_AEP(int source, int tag, 
                                           int context_id, MPID_Comm *comm, void *user_buf,
                                           int user_count, MPI_Datatype datatype, int * foundp)
{
    int mpi_errno = MPI_SUCCESS;
    int found = FALSE;
    MPID_Request *rreq, *prev_rreq;
    MPIDI_Message_match match;
    MPIDI_Message_match mask;
#if defined(FINEGRAIN_MPI)
    int fg_offset;
#endif
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3U_RECVQ_FDU_OR_AEP);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3U_RECVQ_FDU_OR_AEP);

    MPIU_THREAD_CS_ASSERT_HELD(MSGQUEUE);

    /* Store how much time is spent traversing the queue */
    MPIR_T_PVAR_TIMER_START(RECVQ, time_matching_unexpectedq);

#if defined(FINEGRAIN_MPI)
    GET_MATCH_QUEUE_INDEX(my_fgrank, &fg_offset); /* FG: FG_recvq_unexpected_head is per FGP.
                                                  It is indexed by destination's rank (receiver's) */
    rreq = FG_recvq_unexpected_head[fg_offset];
#else
    /* Optimize this loop for an empty unexpected receive queue */
    rreq = recvq_unexpected_head;
#endif
    if (rreq) {
	prev_rreq = NULL;

	match.parts.context_id = context_id;
	match.parts.tag = tag;
	match.parts.rank = source;
#if defined(FINEGRAIN_MPI)
        match.parts.dest_rank = my_fgrank;
        mask.parts.dest_rank = ~0;
#endif

    mask.parts.context_id = mask.parts.rank = mask.parts.tag = ~0;
    /* Mask the error bit that might be set on incoming messages. It is
     * assumed that the local receive operation won't have the error bit set
     * (or it is masked away at some other level). */
    MPIR_TAG_CLEAR_ERROR_BITS(mask.parts.tag);

	if (tag != MPI_ANY_TAG && source != MPI_ANY_SOURCE) {
	    do {
            MPIR_T_PVAR_COUNTER_INC(RECVQ, unexpected_recvq_match_attempts, 1);
		if (MATCH_WITH_LEFT_MASK(rreq->dev.match, match, mask)) {
		    if (prev_rreq != NULL) {
			prev_rreq->dev.next = rreq->dev.next;
		    }
		    else {
#if defined(FINEGRAIN_MPI)
                        FG_recvq_unexpected_head[fg_offset] = rreq->dev.next;
#else
			recvq_unexpected_head = rreq->dev.next;
#endif
		    }

		    if (rreq->dev.next == NULL) {
#if defined(FINEGRAIN_MPI)
                        FG_recvq_unexpected_tail[fg_offset] = prev_rreq;
#else
			recvq_unexpected_tail = prev_rreq;
#endif
		    }
            MPIR_T_PVAR_LEVEL_DEC(RECVQ, unexpected_recvq_length, 1);

            if (MPIDI_Request_get_msg_type(rreq) == MPIDI_REQUEST_EAGER_MSG)
                MPIR_T_PVAR_LEVEL_DEC(RECVQ, unexpected_recvq_buffer_size, rreq->dev.tmpbuf_sz);

		    rreq->comm = comm;
		    MPIR_Comm_add_ref(comm);
		    rreq->dev.user_buf = user_buf;
		    rreq->dev.user_count = user_count;
		    rreq->dev.datatype = datatype;
		    found = TRUE;
		    goto lock_exit;
		}
		prev_rreq = rreq;
		rreq      = rreq->dev.next;
	    } while (rreq);
	}
	else {
        do { /* This loop is just to make it easy to break out if necessary */
            if (tag == MPI_ANY_TAG)
                match.parts.tag = mask.parts.tag = 0;
            if (source == MPI_ANY_SOURCE) {
                if (!MPIDI_CH3I_Comm_AS_enabled(comm)) {
                    /* If MPI_ANY_SOURCE is disabled right now, we should
                     * just add this request to the posted queue instead and
                     * return the appropriate error. */
                    continue;
                }
                match.parts.rank = mask.parts.rank = 0;
            }
	    do {
            MPIR_T_PVAR_COUNTER_INC(RECVQ, unexpected_recvq_match_attempts, 1);
		if (MATCH_WITH_LEFT_MASK(rreq->dev.match, match, mask)) {
		    if (prev_rreq != NULL) {
			prev_rreq->dev.next = rreq->dev.next;
		    }
		    else {
#if defined(FINEGRAIN_MPI)
                        FG_recvq_unexpected_head[fg_offset] = rreq->dev.next;
#else
			recvq_unexpected_head = rreq->dev.next;
#endif
		    }
		    if (rreq->dev.next == NULL) {
#if defined(FINEGRAIN_MPI)
                        FG_recvq_unexpected_tail[fg_offset] = prev_rreq;
#else
			recvq_unexpected_tail = prev_rreq;
#endif
		    }
                    MPIR_T_PVAR_LEVEL_DEC(RECVQ, unexpected_recvq_length, 1);

            if (MPIDI_Request_get_msg_type(rreq) == MPIDI_REQUEST_EAGER_MSG)
                MPIR_T_PVAR_LEVEL_DEC(RECVQ, unexpected_recvq_buffer_size, rreq->dev.tmpbuf_sz);

                    rreq->comm                 = comm;
                    MPIR_Comm_add_ref(comm);
                    rreq->dev.user_buf         = user_buf;
                    rreq->dev.user_count       = user_count;
                    rreq->dev.datatype         = datatype;
                    found = TRUE;
                    goto lock_exit;
                }
                prev_rreq = rreq;
                rreq = rreq->dev.next;
            } while (rreq);
        } while (0);
	}
    }
    MPIR_T_PVAR_TIMER_END(RECVQ, time_matching_unexpectedq);

    /* A matching request was not found in the unexpected queue, so we 
       need to allocate a new request and add it to the posted queue */
    {
        found = FALSE;

	MPIDI_Request_create_rreq( rreq, mpi_errno, goto lock_exit );
	rreq->dev.match.parts.tag	   = tag;
	rreq->dev.match.parts.rank	   = source;
	rreq->dev.match.parts.context_id   = context_id;
#if defined(FINEGRAIN_MPI)
        rreq->dev.match.parts.dest_rank  = my_fgrank; /* FG: FGP MATCH for a receive request. */
        rreq->dev.mask.parts.dest_rank = ~0;
#endif

	/* Added a mask for faster search on 64-bit capable
	 * platforms */
	rreq->dev.mask.parts.context_id = ~0;
	if (rreq->dev.match.parts.rank == MPI_ANY_SOURCE)
	    rreq->dev.mask.parts.rank = 0;
	else
	    rreq->dev.mask.parts.rank = ~0;
	if (rreq->dev.match.parts.tag == MPI_ANY_TAG)
	    rreq->dev.mask.parts.tag = 0;
	else
	    rreq->dev.mask.parts.tag = ~0;

        rreq->comm                 = comm;
        MPIR_Comm_add_ref(comm);
        rreq->dev.user_buf         = user_buf;
        rreq->dev.user_count       = user_count;
        rreq->dev.datatype         = datatype;

        /* check whether VC has failed, or this is an ANY_SOURCE in a
           failed communicator */
        if (source != MPI_ANY_SOURCE) {
            MPIDI_VC_t *vc;
            MPIDI_Comm_get_vc(comm, source, &vc);
            if (vc->state == MPIDI_VC_STATE_MORIBUND) {
                MPIU_ERR_SET1(mpi_errno, MPIX_ERR_PROC_FAILED, "**comm_fail", "**comm_fail %d", vc->pg_rank);
                rreq->status.MPI_ERROR = mpi_errno;
                MPIDI_CH3U_Request_complete(rreq);
                goto lock_exit;
            }
        } else if (!MPIDI_CH3I_Comm_AS_enabled(comm)) {
            /* If this receive is for MPI_ANY_SOURCE, we will still add the
            * request to the queue for now, but we will also set the error
            * class to MPIX_ERR_PROC_FAILED_PENDING since the request shouldn't
            * be matched as long as there is a failure pending. This will get
            * checked again later during the completion function to see if the
            * request can be completed at that time. */
            MPIU_ERR_SET(mpi_errno, MPIX_ERR_PROC_FAILED_PENDING, "**failure_pending");
            rreq->status.MPI_ERROR = mpi_errno;
        }
        
	rreq->dev.next = NULL;
#if defined(FINEGRAIN_MPI)
	if (FG_recvq_posted_tail[fg_offset] != NULL) {
	    FG_recvq_posted_tail[fg_offset]->dev.next = rreq;
	}
	else {
	    FG_recvq_posted_head[fg_offset] = rreq;
	}
	FG_recvq_posted_tail[fg_offset] = rreq;
#else
	if (recvq_posted_tail != NULL) {
	    recvq_posted_tail->dev.next = rreq;
	}
	else {
	    recvq_posted_head = rreq;
	}
	recvq_posted_tail = rreq;
#endif
    MPIR_T_PVAR_LEVEL_INC(RECVQ, posted_recvq_length, 1);
	MPIDI_POSTED_RECV_ENQUEUE_HOOK(rreq);
    }
    
  lock_exit:
    *foundp = found;

    /* If a match was not found, the timer was stopped after the traversal */
    if (found)
        MPIR_T_PVAR_TIMER_END(RECVQ, time_matching_unexpectedq);
    
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3U_RECVQ_FDU_OR_AEP);
    return rreq;
}


/*
 * MPIDI_CH3U_Recvq_DP()
 *
 * Given an existing request, dequeue that request from the posted queue, or 
 * return NULL if the request was not in the posted queued
 *
 * Multithread - This routine is atomic
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Recvq_DP
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3U_Recvq_DP(MPID_Request * rreq)
{
    int found;
    MPID_Request * cur_rreq;
    MPID_Request * prev_rreq;
    int dequeue_failed;
#if defined(FINEGRAIN_MPI)
    int nfg, index = 0;
    MPIX_Get_collocated_size(&nfg);
#endif
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3U_RECVQ_DP);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3U_RECVQ_DP);

    found = FALSE;
    prev_rreq = NULL;

    /* MT FIXME is this right? or should the caller do this? */
    MPIU_THREAD_CS_ENTER(MSGQUEUE,);
    MPIR_T_PVAR_TIMER_START(RECVQ, time_failed_matching_postedq);
#if defined(FINEGRAIN_MPI)
    for (index=0; index<nfg; index++) {
        cur_rreq = FG_recvq_posted_head[index];
        prev_rreq = NULL;
#else
    cur_rreq = recvq_posted_head;
#endif
    while (cur_rreq != NULL) {
        MPIR_T_PVAR_COUNTER_INC(RECVQ, posted_recvq_match_attempts, 1);
	if (cur_rreq == rreq) {
	    if (prev_rreq != NULL) {
		prev_rreq->dev.next = cur_rreq->dev.next;
	    }
	    else {
#if defined(FINEGRAIN_MPI)
                FG_recvq_posted_head[index] = cur_rreq->dev.next;
#else
		recvq_posted_head = cur_rreq->dev.next;
#endif
	    }
	    if (cur_rreq->dev.next == NULL) {
#if defined(FINEGRAIN_MPI)
                FG_recvq_posted_tail[index] = prev_rreq;
#else
		recvq_posted_tail = prev_rreq;
#endif
	    }
        MPIR_T_PVAR_LEVEL_DEC(RECVQ, posted_recvq_length, 1);
            /* Notify channel that rreq has been dequeued and check if
               it has already matched rreq, fail if so */
	    dequeue_failed = MPIDI_POSTED_RECV_DEQUEUE_HOOK(rreq);
            if (!dequeue_failed)
                found = TRUE;
	    break;
	}
	
	prev_rreq = cur_rreq;
	cur_rreq = cur_rreq->dev.next;
    }
#if defined(FINEGRAIN_MPI)
    }
#endif
    if (!found)
        MPIR_T_PVAR_TIMER_END(RECVQ, time_failed_matching_postedq);

    MPIU_THREAD_CS_EXIT(MSGQUEUE,);

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3U_RECVQ_DP);
    return found;
}

/*
 * MPIDI_CH3U_Recvq_FDP_or_AEU()
 *
 * Locate a request in the posted queue and dequeue it, or allocate a new 
 * request and enqueue it in the unexpected queue
 *
 * Multithread - This routine must be called from within a MSGQUEUE 
 * critical section.  If a request is allocated, it must not release
 * the MSGQUEUE until the request is completely valid, as another thread
 * may then find it and dequeue it.
 *
 * This routine is used in ch3u_eager, ch3u_eagersync, ch3u_handle_recv_pkt,
 * ch3u_rndv, and mpidi_isend_self.  Routines within the progress engine
 * will need to be careful to avoid nested critical sections.  
 *
 * FIXME: Currently, the routines called from within the progress engine
 * do not use the MSGQUEUE CS, because in the brief-global mode, that
 * simply uses the global_mutex .  
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Recvq_FDP_or_AEU
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
MPID_Request * MPIDI_CH3U_Recvq_FDP_or_AEU(MPIDI_Message_match * match, 
					   int * foundp)
{
    int found;
    MPID_Request * rreq;
    MPID_Request * prev_rreq;
    int channel_matched;
    int error_bit_masked = 0, proc_failure_bit_masked = 0;
#if defined(FINEGRAIN_MPI)
    int fg_offset;
#endif

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3U_RECVQ_FDP_OR_AEU);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3U_RECVQ_FDP_OR_AEU);

    MPIU_THREAD_CS_ASSERT_HELD(MSGQUEUE);

    /* Unset the error bit if it is set on the incoming packet so we don't
     * have to mask it every time. It will get reset at the end of the loop or
     * before the request is added to the unexpected queue if was set here. */
    if (MPIR_TAG_CHECK_ERROR_BIT(match->parts.tag)) {
        error_bit_masked = 1;
        if (MPIR_TAG_CHECK_PROC_FAILURE_BIT(match->parts.tag)) proc_failure_bit_masked = 1;
        MPIR_TAG_CLEAR_ERROR_BITS(match->parts.tag);
    }

#if defined(FINEGRAIN_MPI)
    GET_MATCH_QUEUE_INDEX(match->parts.dest_rank, &fg_offset);
#endif

 top_loop:
    prev_rreq = NULL;

#if defined(FINEGRAIN_MPI)
    rreq = FG_recvq_posted_head[fg_offset];
#else
    rreq = recvq_posted_head;
#endif

    MPIR_T_PVAR_TIMER_START(RECVQ, time_failed_matching_postedq);
    while (rreq != NULL) {
        MPIR_T_PVAR_COUNTER_INC(RECVQ, posted_recvq_match_attempts, 1);
	if (MATCH_WITH_LEFT_RIGHT_MASK(rreq->dev.match, *match, rreq->dev.mask)) {
	    if (prev_rreq != NULL) {
		prev_rreq->dev.next = rreq->dev.next;
	    }
	    else {
#if defined(FINEGRAIN_MPI)
                FG_recvq_posted_head[fg_offset] = rreq->dev.next;
#else
		recvq_posted_head = rreq->dev.next;
#endif
	    }
	    if (rreq->dev.next == NULL) {
#if defined(FINEGRAIN_MPI)
                FG_recvq_posted_tail[fg_offset] = prev_rreq;
#else
		recvq_posted_tail = prev_rreq;
#endif
	    }
        MPIR_T_PVAR_LEVEL_DEC(RECVQ, posted_recvq_length, 1);

#if defined(FINEGRAIN_MPI)
        /* FG: TODO Zerocopy */
#endif
            /* give channel a chance to match the request, try again if so */
	    channel_matched = MPIDI_POSTED_RECV_DEQUEUE_HOOK(rreq);
            if (channel_matched) /* FG: TODO Double-check */
                goto top_loop;
            
	    found = TRUE;                
	    goto lock_exit;
	}
	prev_rreq = rreq;
	rreq = rreq->dev.next;
    }
    MPIR_T_PVAR_TIMER_END(RECVQ, time_failed_matching_postedq);

#if !defined(FINEGRAIN_MPI) /* FG: TODO temporary bypass. MPIDI_CH3I_Comm_find uses comm->node_comm */
    /* If we didn't match the request, look to see if the communicator is
     * revoked. If so, just throw this request away since it won't be used
     * anyway. */
    {
        MPID_Comm *comm_ptr;
        int mpi_errno ATTRIBUTE((unused)) = MPI_SUCCESS;

        MPIDI_CH3I_Comm_find(match->parts.context_id, &comm_ptr);

        if (comm_ptr && comm_ptr->revoked && MPIR_TAG_MASK_ERROR_BITS(match->parts.tag) != MPIR_AGREE_TAG &&
                        comm_ptr->revoked && MPIR_TAG_MASK_ERROR_BITS(match->parts.tag) != MPIR_SHRINK_TAG) {
            *foundp = FALSE;
            MPIDI_Request_create_null_rreq( rreq, mpi_errno, found=FALSE;goto lock_exit );
            MPIU_Assert(mpi_errno == MPI_SUCCESS);

            MPIU_DBG_MSG_FMT(CH3_OTHER, VERBOSE,
                (MPIU_DBG_FDEST, "RECEIVED MESSAGE FOR REVOKED COMM (tag=%d,src=%d,cid=%d)\n", MPIR_TAG_MASK_ERROR_BITS(match->parts.tag), match->parts.rank, comm_ptr->context_id));
            return rreq;
        }
    }
#endif

    /* A matching request was not found in the posted queue, so we 
       need to allocate a new request and add it to the unexpected queue */
    {
        int mpi_errno ATTRIBUTE((unused)) = 0;
	MPIDI_Request_create_rreq( rreq, mpi_errno, 
				   found=FALSE;goto lock_exit );
        MPIU_Assert(mpi_errno == 0);
        rreq->dev.recv_pending_count = 1;
        /* Reset the error bits if we unset it earlier. */
        if (error_bit_masked) MPIR_TAG_SET_ERROR_BIT(match->parts.tag);
        if (proc_failure_bit_masked) MPIR_TAG_SET_PROC_FAILURE_BIT(match->parts.tag);
	rreq->dev.match	= *match;
	rreq->dev.next	= NULL;
#if defined(FINEGRAIN_MPI)
	if (FG_recvq_unexpected_tail[fg_offset] != NULL) {
	    FG_recvq_unexpected_tail[fg_offset]->dev.next = rreq;
	}
	else {
	    FG_recvq_unexpected_head[fg_offset] = rreq;
	}
	FG_recvq_unexpected_tail[fg_offset] = rreq;
#else
	if (recvq_unexpected_tail != NULL) {
	    recvq_unexpected_tail->dev.next = rreq;
	}
	else {
	    recvq_unexpected_head = rreq;
	}
	recvq_unexpected_tail = rreq;
#endif
    MPIR_T_PVAR_LEVEL_INC(RECVQ, unexpected_recvq_length, 1);
    }
    
    found = FALSE;

  lock_exit:

    /* Reset the error bits if we unset it earlier. */
    if (error_bit_masked) MPIR_TAG_SET_ERROR_BIT(match->parts.tag);
    if (proc_failure_bit_masked) MPIR_TAG_SET_PROC_FAILURE_BIT(match->parts.tag);

    *foundp = found;

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3U_RECVQ_FDP_OR_AEU);
    return rreq;
}

/* returns TRUE iff the request was sent on the vc */
static inline int req_uses_vc(const MPID_Request* req, const MPIDI_VC_t *vc)
{
    MPIDI_VC_t *vc1;
    
    MPIDI_Comm_get_vc(req->comm, req->dev.match.parts.rank, &vc1);
    return vc == vc1;
}

#undef FUNCNAME
#define FUNCNAME dequeue_and_set_error
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
/* This dequeues req from the posted recv queue, set req's error code to comm_fail, and updates the req pointer.
   Note that this creates a new error code if one hasn't already been created (i.e., if *error is MPI_SUCCESS). */
static inline void dequeue_and_set_error(MPID_Request **req,  MPID_Request *prev_req, MPID_Request **head, MPID_Request **tail, int *error, int rank)
{
    MPID_Request *next = (*req)->dev.next;
    
    /* remove from queue */
    if (*head == *req) {
#if defined(FINEGRAIN_MPI)
        MPIR_T_PVAR_LEVEL_DEC(RECVQ, posted_recvq_length, 1);
#else
        if (*head == recvq_posted_head) MPIR_T_PVAR_LEVEL_DEC(RECVQ, posted_recvq_length, 1);
#endif

        *head = (*req)->dev.next;
    } else
        prev_req->dev.next = (*req)->dev.next;

    if (*tail == *req)
        *tail = prev_req;

    /* set error and complete */
    (*req)->status.MPI_ERROR = *error;
    MPIDI_CH3U_Request_complete(*req);
    MPIU_DBG_MSG_FMT(CH3_OTHER, VERBOSE,
                     (MPIU_DBG_FDEST, "set error of req %p (%#08x) to %#x and completing.",
                      *req, (*req)->handle, *error));
    *req = next;
}

/*
 * MPIDI_CH3U_Clean_recvq()
 *
 * Looks through the entire unexpected recv queue and the posted recv queues.
 * If a request is found that involved the provided communicator (comm_ptr),
 * it is dequeed and marked as failed via MPIX_ERR_REVOKED.
 *
 * Multithread - This routine must be called from within a MSGQUEUE
 * critical section.  If a request is allocated, it must not release
 * the MSGQUEUE until the request is completely valid, as another thread
 * may then find it and dequeue it.
 *
 */
int MPIDI_CH3U_Clean_recvq(MPID_Comm *comm_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    int error = MPI_SUCCESS;
    MPID_Request *rreq, *prev_rreq = NULL;
    MPIDI_Message_match match;
    MPIDI_Message_match mask;
#if defined(FINEGRAIN_MPI)
    int nfg, index = 0;
    MPIX_Get_collocated_size(&nfg);
#endif

    MPIDI_STATE_DECL(MPIDI_CH3U_CLEAN_RECVQ);

    MPIDI_FUNC_ENTER(MPIDI_CH3U_CLEAN_RECVQ);

    MPIU_THREAD_CS_ASSERT_HELD(MSGQUEUE);

    MPIU_ERR_SETSIMPLE(error, MPIX_ERR_REVOKED, "**revoked");

#if !defined(FINEGRAIN_MPI)
    rreq = recvq_unexpected_head;
#endif
    mask.parts.context_id = ~0;
    mask.parts.rank = mask.parts.tag = 0;

    /* Clear the error bit in the tag since we don't care about whether or
     * not we're trying to report an error anymore. */
    MPIR_TAG_CLEAR_ERROR_BITS(mask.parts.tag);

#if defined(FINEGRAIN_MPI)
    mask.parts.dest_rank = 0;
    for (index=0; index<nfg; index++) {
        rreq = FG_recvq_unexpected_head[index];
        prev_rreq = NULL;
#endif
    while (NULL != rreq) {
        /* We'll have to do this matching twice. Once for the pt2pt context id
         * and once for the collective context id */
        match.parts.context_id = comm_ptr->recvcontext_id + MPID_CONTEXT_INTRA_PT2PT;

        if (MATCH_WITH_LEFT_RIGHT_MASK(rreq->dev.match, match, mask)) {
            MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
                        "cleaning up unexpected pt2pt pkt rank=%d tag=%d contextid=%d",
                        rreq->dev.match.parts.rank, rreq->dev.match.parts.tag, rreq->dev.match.parts.context_id));
#if defined(FINEGRAIN_MPI)
            dequeue_and_set_error(&rreq, prev_rreq, &FG_recvq_unexpected_head[index], &FG_recvq_unexpected_tail[index], &error, MPI_PROC_NULL);
#else
            dequeue_and_set_error(&rreq, prev_rreq, &recvq_unexpected_head, &recvq_unexpected_tail, &error, MPI_PROC_NULL);
#endif
            continue;
        }

        match.parts.context_id = comm_ptr->recvcontext_id + MPID_CONTEXT_INTRA_COLL;

        if (MATCH_WITH_LEFT_RIGHT_MASK(rreq->dev.match, match, mask)) {
            if (MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_AGREE_TAG &&
                MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_SHRINK_TAG) {
                MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
                            "cleaning up unexpected collective pkt rank=%d tag=%d contextid=%d",
                            rreq->dev.match.parts.rank, rreq->dev.match.parts.tag, rreq->dev.match.parts.context_id));
#if defined(FINEGRAIN_MPI)
                dequeue_and_set_error(&rreq, prev_rreq, &FG_recvq_unexpected_head[index], &FG_recvq_unexpected_tail[index], &error, MPI_PROC_NULL);
#else
                dequeue_and_set_error(&rreq, prev_rreq, &recvq_unexpected_head, &recvq_unexpected_tail, &error, MPI_PROC_NULL);
#endif
                continue;
            }
        }

        if (MPIR_Comm_is_node_aware(comm_ptr)) { /* FG: TODO */
            int offset;
            offset = (comm_ptr->comm_kind == MPID_INTRACOMM) ?  MPID_CONTEXT_INTRA_PT2PT : MPID_CONTEXT_INTER_PT2PT;
            match.parts.context_id = comm_ptr->recvcontext_id + MPID_CONTEXT_INTRANODE_OFFSET + offset;

            if (MATCH_WITH_LEFT_RIGHT_MASK(rreq->dev.match, match, mask)) {
                if (MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_AGREE_TAG &&
                    MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_SHRINK_TAG) {
                    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
                                "cleaning up unexpected pt2pt pkt rank=%d tag=%d contextid=%d",
                                rreq->dev.match.parts.rank, rreq->dev.match.parts.tag, rreq->dev.match.parts.context_id));
#if defined(FINEGRAIN_MPI)
                    dequeue_and_set_error(&rreq, prev_rreq, &FG_recvq_unexpected_head[index], &FG_recvq_unexpected_tail[index], &error, MPI_PROC_NULL);
#else
                    dequeue_and_set_error(&rreq, prev_rreq, &recvq_unexpected_head, &recvq_unexpected_tail, &error, MPI_PROC_NULL);
#endif
                    continue;
                }
            }

            offset = (comm_ptr->comm_kind == MPID_INTRACOMM) ?  MPID_CONTEXT_INTRA_COLL : MPID_CONTEXT_INTER_COLL;
            match.parts.context_id = comm_ptr->recvcontext_id + MPID_CONTEXT_INTRANODE_OFFSET + offset;

            if (MATCH_WITH_LEFT_RIGHT_MASK(rreq->dev.match, match, mask)) {
                if (MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_AGREE_TAG &&
                    MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_SHRINK_TAG) {
                    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
                                "cleaning up unexpected collective pkt rank=%d tag=%d contextid=%d",
                                rreq->dev.match.parts.rank, rreq->dev.match.parts.tag, rreq->dev.match.parts.context_id));
#if defined(FINEGRAIN_MPI)
                    dequeue_and_set_error(&rreq, prev_rreq, &FG_recvq_unexpected_head[index], &FG_recvq_unexpected_tail[index], &error, MPI_PROC_NULL);
#else
                    dequeue_and_set_error(&rreq, prev_rreq, &recvq_unexpected_head, &recvq_unexpected_tail, &error, MPI_PROC_NULL);
#endif
                    continue;
                }
            }

            offset = (comm_ptr->comm_kind == MPID_INTRACOMM) ?  MPID_CONTEXT_INTRA_PT2PT : MPID_CONTEXT_INTER_PT2PT;
            match.parts.context_id = comm_ptr->recvcontext_id + MPID_CONTEXT_INTERNODE_OFFSET + offset;

            if (MATCH_WITH_LEFT_RIGHT_MASK(rreq->dev.match, match, mask)) {
                if (MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_AGREE_TAG &&
                    MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_SHRINK_TAG) {
                    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
                                "cleaning up unexpected pt2pt pkt rank=%d tag=%d contextid=%d",
                                rreq->dev.match.parts.rank, rreq->dev.match.parts.tag, rreq->dev.match.parts.context_id));
#if defined(FINEGRAIN_MPI)
                    dequeue_and_set_error(&rreq, prev_rreq, &FG_recvq_unexpected_head[index], &FG_recvq_unexpected_tail[index], &error, MPI_PROC_NULL);
#else
                    dequeue_and_set_error(&rreq, prev_rreq, &recvq_unexpected_head, &recvq_unexpected_tail, &error, MPI_PROC_NULL);
#endif
                    continue;
                }
            }

            offset = (comm_ptr->comm_kind == MPID_INTRACOMM) ?  MPID_CONTEXT_INTRA_COLL : MPID_CONTEXT_INTER_COLL;
            match.parts.context_id = comm_ptr->recvcontext_id + MPID_CONTEXT_INTERNODE_OFFSET + offset;

            if (MATCH_WITH_LEFT_RIGHT_MASK(rreq->dev.match, match, mask)) {
                if (MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_AGREE_TAG &&
                    MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_SHRINK_TAG) {
                    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
                                "cleaning up unexpected collective pkt rank=%d tag=%d contextid=%d",
                                rreq->dev.match.parts.rank, rreq->dev.match.parts.tag, rreq->dev.match.parts.context_id));
#if defined(FINEGRAIN_MPI)
                    dequeue_and_set_error(&rreq, prev_rreq, &FG_recvq_unexpected_head[index], &FG_recvq_unexpected_tail[index], &error, MPI_PROC_NULL);
#else
                    dequeue_and_set_error(&rreq, prev_rreq, &recvq_unexpected_head, &recvq_unexpected_tail, &error, MPI_PROC_NULL);
#endif
                    continue;
                }
            }
        }

        prev_rreq = rreq;
        rreq = rreq->dev.next;
    }
#if defined(FINEGRAIN_MPI)
    }
#endif

#if defined(FINEGRAIN_MPI)
    for (index=0; index<nfg; index++) {
        rreq = FG_recvq_posted_head[index];
#else
    rreq = recvq_posted_head;
#endif
    prev_rreq = NULL;

    while (NULL != rreq) {
        /* We'll have to do this matching twice. Once for the pt2pt context id
         * and once for the collective context id */
        match.parts.context_id = comm_ptr->recvcontext_id + MPID_CONTEXT_INTRA_PT2PT;

        if (MATCH_WITH_LEFT_RIGHT_MASK(rreq->dev.match, match, mask)) {
            MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
                        "cleaning up posted pt2pt pkt rank=%d tag=%d contextid=%d",
                        rreq->dev.match.parts.rank, rreq->dev.match.parts.tag, rreq->dev.match.parts.context_id));
#if defined(FINEGRAIN_MPI)
            dequeue_and_set_error(&rreq, prev_rreq, &FG_recvq_posted_head[index], &FG_recvq_posted_tail[index], &error, MPI_PROC_NULL);
#else
            dequeue_and_set_error(&rreq, prev_rreq, &recvq_posted_head, &recvq_posted_tail, &error, MPI_PROC_NULL);
#endif
            continue;
        }

        match.parts.context_id = comm_ptr->recvcontext_id + MPID_CONTEXT_INTRA_COLL;

        if (MATCH_WITH_LEFT_RIGHT_MASK(rreq->dev.match, match, mask)) {
            if (MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_AGREE_TAG &&
                MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_SHRINK_TAG) {
                MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
                            "cleaning up posted collective pkt rank=%d tag=%d contextid=%d",
                            rreq->dev.match.parts.rank, rreq->dev.match.parts.tag, rreq->dev.match.parts.context_id));
#if defined(FINEGRAIN_MPI)
                dequeue_and_set_error(&rreq, prev_rreq, &FG_recvq_posted_head[index], &FG_recvq_posted_tail[index], &error, MPI_PROC_NULL);
#else
                dequeue_and_set_error(&rreq, prev_rreq, &recvq_posted_head, &recvq_posted_tail, &error, MPI_PROC_NULL);
#endif
                continue;
            }
        }

        if (MPIR_Comm_is_node_aware(comm_ptr)) { /* FG: TODO */
            int offset;
            offset = (comm_ptr->comm_kind == MPID_INTRACOMM) ?  MPID_CONTEXT_INTRA_PT2PT : MPID_CONTEXT_INTER_PT2PT;
            match.parts.context_id = comm_ptr->recvcontext_id + MPID_CONTEXT_INTRANODE_OFFSET + offset;

            if (MATCH_WITH_LEFT_RIGHT_MASK(rreq->dev.match, match, mask)) {
                if (MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_AGREE_TAG &&
                    MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_SHRINK_TAG) {
                    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
                                "cleaning up posted pt2pt pkt rank=%d tag=%d contextid=%d",
                                rreq->dev.match.parts.rank, rreq->dev.match.parts.tag, rreq->dev.match.parts.context_id));
#if defined(FINEGRAIN_MPI)
                    dequeue_and_set_error(&rreq, prev_rreq, &FG_recvq_posted_head[index], &FG_recvq_posted_tail[index], &error, MPI_PROC_NULL);
#else
                    dequeue_and_set_error(&rreq, prev_rreq, &recvq_posted_head, &recvq_posted_tail, &error, MPI_PROC_NULL);
#endif
                    continue;
                }
            }

            offset = (comm_ptr->comm_kind == MPID_INTRACOMM) ?  MPID_CONTEXT_INTRA_COLL : MPID_CONTEXT_INTER_COLL;
            match.parts.context_id = comm_ptr->recvcontext_id + MPID_CONTEXT_INTRANODE_OFFSET + offset;

            if (MATCH_WITH_LEFT_RIGHT_MASK(rreq->dev.match, match, mask)) {
                if (MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_AGREE_TAG &&
                    MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_SHRINK_TAG) {
                    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
                                "cleaning up posted collective pkt rank=%d tag=%d contextid=%d",
                                rreq->dev.match.parts.rank, rreq->dev.match.parts.tag, rreq->dev.match.parts.context_id));
#if defined(FINEGRAIN_MPI)
                    dequeue_and_set_error(&rreq, prev_rreq, &FG_recvq_posted_head[index], &FG_recvq_posted_tail[index], &error, MPI_PROC_NULL);
#else
                    dequeue_and_set_error(&rreq, prev_rreq, &recvq_posted_head, &recvq_posted_tail, &error, MPI_PROC_NULL);
#endif
                    continue;
                }
            }

            offset = (comm_ptr->comm_kind == MPID_INTRACOMM) ?  MPID_CONTEXT_INTRA_PT2PT : MPID_CONTEXT_INTER_PT2PT;
            match.parts.context_id = comm_ptr->recvcontext_id + MPID_CONTEXT_INTERNODE_OFFSET + offset;

            if (MATCH_WITH_LEFT_RIGHT_MASK(rreq->dev.match, match, mask)) {
                if (MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_AGREE_TAG &&
                    MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_SHRINK_TAG) {
                    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
                                "cleaning up posted pt2pt pkt rank=%d tag=%d contextid=%d",
                                rreq->dev.match.parts.rank, rreq->dev.match.parts.tag, rreq->dev.match.parts.context_id));
#if defined(FINEGRAIN_MPI)
                    dequeue_and_set_error(&rreq, prev_rreq, &FG_recvq_posted_head[index], &FG_recvq_posted_tail[index], &error, MPI_PROC_NULL);
#else
                    dequeue_and_set_error(&rreq, prev_rreq, &recvq_posted_head, &recvq_posted_tail, &error, MPI_PROC_NULL);
#endif
                    continue;
                }
            }

            offset = (comm_ptr->comm_kind == MPID_INTRACOMM) ?  MPID_CONTEXT_INTRA_COLL : MPID_CONTEXT_INTER_COLL;
            match.parts.context_id = comm_ptr->recvcontext_id + MPID_CONTEXT_INTERNODE_OFFSET + offset;

            if (MATCH_WITH_LEFT_RIGHT_MASK(rreq->dev.match, match, mask)) {
                if (MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_AGREE_TAG &&
                    MPIR_TAG_MASK_ERROR_BITS(rreq->dev.match.parts.tag) != MPIR_SHRINK_TAG) {
                    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
                                "cleaning up posted collective pkt rank=%d tag=%d contextid=%d",
                                rreq->dev.match.parts.rank, rreq->dev.match.parts.tag, rreq->dev.match.parts.context_id));
#if defined(FINEGRAIN_MPI)
                    dequeue_and_set_error(&rreq, prev_rreq, &FG_recvq_posted_head[index], &FG_recvq_posted_tail[index], &error, MPI_PROC_NULL);
#else
                    dequeue_and_set_error(&rreq, prev_rreq, &recvq_posted_head, &recvq_posted_tail, &error, MPI_PROC_NULL);
#endif
                    continue;
                }
            }
        }

        prev_rreq = rreq;
        rreq = rreq->dev.next;
    }
#if defined(FINEGRAIN_MPI)
    }
#endif

    MPIDI_FUNC_EXIT(MPIDI_CH3U_CLEAN_RECVQ);

    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Complete_disabled_anysources
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIDI_CH3U_Complete_disabled_anysources(void)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *req, *prev_req;
    int error = MPI_SUCCESS;
#if defined(FINEGRAIN_MPI)
    int nfg, index = 0;
    MPIX_Get_collocated_size(&nfg);
#endif

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3U_COMPLETE_DISABLED_ANYSOURCES);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3U_COMPLETE_DISABLED_ANYSOURCES);
    MPIU_THREAD_CS_ENTER(MSGQUEUE,);

    MPIU_ERR_SETSIMPLE(error, MPIX_ERR_PROC_FAILED_PENDING, "**failure_pending");

    /* Check each request in the posted queue, and complete-with-error any
       anysource requests posted on communicators that have disabled
       anysources */
#if defined(FINEGRAIN_MPI)
    for (index=0; index<nfg; index++) {
        req = FG_recvq_posted_head[index];
#else
    req = recvq_posted_head;
#endif
    prev_req = NULL;
    while (req) {
        if (req->dev.match.parts.rank == MPI_ANY_SOURCE && !MPIDI_CH3I_Comm_AS_enabled(req->comm)) {
#if defined(FINEGRAIN_MPI)
            dequeue_and_set_error(&req, prev_req, &FG_recvq_posted_head[index], &FG_recvq_posted_tail[index], &error, MPI_PROC_NULL);
#else
            dequeue_and_set_error(&req, prev_req, &recvq_posted_head, &recvq_posted_tail, &error, MPI_PROC_NULL); /* we don't know the rank of the failed proc */
#endif
        } else {
            prev_req = req;
            req = req->dev.next;
        }
    }
#if defined(FINEGRAIN_MPI)
    }
#endif


 fn_exit:
    MPIU_THREAD_CS_EXIT(MSGQUEUE,);

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3U_COMPLETE_DISABLED_ANYSOURCES);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPIDU_Complete_posted_with_error
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIDI_CH3U_Complete_posted_with_error(MPIDI_VC_t *vc)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *req, *prev_req;
    int error = MPI_SUCCESS;
#if defined(FINEGRAIN_MPI)
    int nfg, index = 0;
    MPIX_Get_collocated_size(&nfg);
#endif

    MPIDI_STATE_DECL(MPID_STATE_MPIDU_COMPLETE_POSTED_WITH_ERROR);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDU_COMPLETE_POSTED_WITH_ERROR);

    MPIU_THREAD_CS_ENTER(MSGQUEUE,);

    MPIU_ERR_SETSIMPLE(error, MPIX_ERR_PROC_FAILED, "**proc_failed");

    /* check each req in the posted queue and complete-with-error any requests
       using this VC. */
#if defined(FINEGRAIN_MPI)
    for (index=0; index<nfg; index++) {
        req = FG_recvq_posted_head[index];
#else
    req = recvq_posted_head;
#endif
    prev_req = NULL;
    while (req) {
        if (req->dev.match.parts.rank != MPI_ANY_SOURCE && req_uses_vc(req, vc)) {
#if defined(FINEGRAIN_MPI)
            dequeue_and_set_error(&req, prev_req, &FG_recvq_posted_head[index], &FG_recvq_posted_tail[index], &error, MPI_PROC_NULL);
#else
            dequeue_and_set_error(&req, prev_req, &recvq_posted_head, &recvq_posted_tail, &error, MPI_PROC_NULL);
#endif
        } else {
            prev_req = req;
            req = req->dev.next;
        }
    }
#if defined(FINEGRAIN_MPI)
    }
#endif

 fn_exit:
    MPIU_THREAD_CS_EXIT(MSGQUEUE,);

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDU_COMPLETE_POSTED_WITH_ERROR);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}


/* --BEGIN ERROR HANDLING-- */
/* pretty prints tag, returns out for calling convenience */
static char *tag_val_to_str(int tag, char *out, int max)
{
    if (tag == MPI_ANY_TAG) {
        MPIU_Strncpy(out, "MPI_ANY_TAG", max);
    }
    else {
        MPIU_Snprintf(out, max, "%d", tag);
    }
    return out;
}

/* pretty prints rank, returns out for calling convenience */
static char *rank_val_to_str(int rank, char *out, int max)
{
    if (rank == MPI_ANY_SOURCE) {
        MPIU_Strncpy(out, "MPI_ANY_SOURCE", max);
    }
    else {
        MPIU_Snprintf(out, max, "%d", rank);
    }
    return out;
}
/* --END ERROR HANDLING-- */

/* --BEGIN DEBUG-- */
/* satisfy the compiler */
void MPIDI_CH3U_Dbg_print_recvq(FILE *stream);

/* This function can be called by a debugger to dump the recvq state to the
 * given stream. */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Dbg_print_recvq
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3U_Dbg_print_recvq(FILE *stream)
{
    MPID_Request * rreq;
    int i;
    char tag_buf[128];
    char rank_buf[128];
#if defined(FINEGRAIN_MPI)
    int nfg, index = 0;
    MPIX_Get_collocated_size(&nfg);
#endif

    fprintf(stream, "========================================\n");
    fprintf(stream, "MPI_COMM_WORLD  ctx=%#x rank=%d\n", MPIR_Process.comm_world->context_id, MPIR_Process.comm_world->rank);
    fprintf(stream, "MPI_COMM_SELF   ctx=%#x\n", MPIR_Process.comm_self->context_id);
    if (MPIR_Process.comm_parent) {
        fprintf(stream, "MPI_COMM_PARENT ctx=%#x recvctx=%#x\n",
                MPIR_Process.comm_self->context_id,
                MPIR_Process.comm_parent->recvcontext_id);
    }
    else {
        fprintf(stream, "MPI_COMM_PARENT (NULL)\n");
    }

    fprintf(stream, "CH3 Posted RecvQ:\n");
#if defined(FINEGRAIN_MPI)
    i = 0;
    for (index=0; index<nfg; index++)
    {
        rreq = FG_recvq_posted_head[index];
#else
    rreq = recvq_posted_head;
    i = 0;
#endif
    while (rreq != NULL) {
        fprintf(stream, "..[%d] rreq=%p ctx=%#x rank=%s tag=%s\n", i, rreq,
                        rreq->dev.match.parts.context_id,
                        rank_val_to_str(rreq->dev.match.parts.rank, rank_buf, sizeof(rank_buf)),
                        tag_val_to_str(rreq->dev.match.parts.tag, tag_buf, sizeof(tag_buf)));
        ++i;
        rreq = rreq->dev.next;
    }
#if defined(FINEGRAIN_MPI)
    }
#endif

    fprintf(stream, "CH3 Unexpected RecvQ:\n");
#if defined(FINEGRAIN_MPI)
    i = 0;
    for (index=0; index<nfg; index++)
    {
        rreq = FG_recvq_unexpected_head[index];
#else
    rreq = recvq_unexpected_head;
    i = 0;
#endif
    while (rreq != NULL) {
        fprintf(stream, "..[%d] rreq=%p ctx=%#x rank=%s tag=%s\n", i, rreq,
                        rreq->dev.match.parts.context_id,
                        rank_val_to_str(rreq->dev.match.parts.rank, rank_buf, sizeof(rank_buf)),
                        tag_val_to_str(rreq->dev.match.parts.tag, tag_buf, sizeof(tag_buf)));
        fprintf(stream, "..    status.src=%s status.tag=%s\n",
                        rank_val_to_str(rreq->status.MPI_SOURCE, rank_buf, sizeof(rank_buf)),
                        tag_val_to_str(rreq->status.MPI_TAG, tag_buf, sizeof(tag_buf)));
        ++i;
        rreq = rreq->dev.next;
    }
#if defined(FINEGRAIN_MPI)
    }
#endif

    fprintf(stream, "========================================\n");
}
/* --END DEBUG-- */

/* returns the number of elements in the unexpected queue */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Recvq_count_unexp
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3U_Recvq_count_unexp(void)
{
    int count = 0;
#if defined(FINEGRAIN_MPI)
    int nfg, i = 0;
    MPID_Request *req;
    MPIX_Get_collocated_size(&nfg);
#else
    MPID_Request *req = recvq_unexpected_head;
#endif

    MPIU_THREAD_CS_ASSERT_HELD(MSGQUEUE);

#if defined(FINEGRAIN_MPI)
    for (i=0; i<nfg; i++)
    {
        req = FG_recvq_unexpected_head[i];
#endif
    while (req)
    {
        ++count;
        req = req->dev.next;
    }
#if defined(FINEGRAIN_MPI)
    }
#endif

    return count;
}
