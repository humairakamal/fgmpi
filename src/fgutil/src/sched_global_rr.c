/*
 *  (C) 2008 Humaira Kamal, The University of British Columbia.
 *      See COPYRIGHT in top-level directory.
 *
 *  Schedulers : Round Robin, Inverse Round Robin, and Blocking.
 *
 */

#include "threadlib_internal.h"
#include "util.h"

#ifndef DEBUG_sched_global_rr_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif


static pointer_list_t *runlist = NULL;
static int progress_thread_running = 0;
fgmpi_thread_t * progress_thread = NULL;

//////////////////////////////////////////////////////////////////////
// generic stuff
//////////////////////////////////////////////////////////////////////

/* Forward declarations */
static inline fgmpi_thread_t* runlist_remove_from_head(void);
static inline fgmpi_thread_t* runlist_remove_from_tail(void);

static inline fgmpi_thread_t* runlist_remove_from_head(void)
{
    fgmpi_thread_t *next = (fgmpi_thread_t *) pl_remove_head(runlist);
    if( next == (fgmpi_thread_t*) -1 )
        return NULL;
    else
        return next;
}

static inline fgmpi_thread_t* runlist_remove_from_tail(void)
{
    fgmpi_thread_t *next = (fgmpi_thread_t *) pl_remove_tail(runlist);
    if( next == (fgmpi_thread_t*) -1 )
        return NULL;
    else
        return next;
}

inline int runlist_size(void)
{
    if (NULL == runlist )
        return (0);
    else
        return pl_size(runlist);
}

inline void noop(void)
{
    ; /* do nothing */
}

//////////////////////////////////////////////////////////////////////
// Round robin scheduler
//////////////////////////////////////////////////////////////////////


inline void sched_global_rr_init(void)
{
  runlist = new_pointer_list("sched_global_rr_runlist");
}

inline void sched_global_rr_add_thread(fgmpi_thread_t *t)
{
  pl_add_tail(runlist, t);
}

inline fgmpi_thread_t* sched_global_rr_next_thread(void)
{
    return runlist_remove_from_head();
}

inline void sched_global_rr_block_thread(fgmpi_thread_t *t, void* event)
{
    sched_global_rr_add_thread(t); /* no blocking, simply add to runlist */
}

inline void sched_global_rr_unblock_thread(void* event)
{
    ; /* do nothing */
}



//////////////////////////////////////////////////////////////////////
// Inverse RR scheduler
//////////////////////////////////////////////////////////////////////


inline void sched_global_invrr_init(void)
{
  runlist = new_pointer_list("sched_global_invrr_runlist");
}


inline void sched_global_invrr_add_thread(fgmpi_thread_t *t)
{
  pl_add_tail(runlist, t);
}


inline fgmpi_thread_t* sched_global_invrr_next_thread(void)
{
    return runlist_remove_from_head();
}

inline void sched_global_invrr_add_init_thread(fgmpi_thread_t *t)
{
  pl_add_head(runlist, t);
}


inline fgmpi_thread_t* sched_global_invrr_next_init_thread(void)
{
    return runlist_remove_from_tail();
}


inline void sched_global_invrr_block_thread(fgmpi_thread_t *t, void* event)
{
    sched_global_invrr_add_thread(t); /* no blocking, simply add to runlist */
}

inline void sched_global_invrr_unblock_thread(void* event)
{
    ; /* do nothing */
}


//////////////////////////////////////////////////////////////////////
// Scheduler with a hash block-queue
//////////////////////////////////////////////////////////////////////
static void *blocklist = NULL;

inline void sched_block_queue_init(void)
{
  runlist = new_pointer_list("sched_block_queue_runlist");
  blocklist = (hshtbl *) SchedulerHashCreate();
  assert (NULL != blocklist);
}


inline void sched_block_queue_add_thread(fgmpi_thread_t *t)
{
    pl_add_tail(runlist, t);
}


inline fgmpi_thread_t* sched_block_queue_next_thread(void)
{
    return runlist_remove_from_head();
}


inline void sched_block_queue_block_thread(fgmpi_thread_t *t, void* event)
{
    schedQueue_itemptr blk_event = (schedQueue_itemptr) event;
    assert((fgmpi_thread_t *)(blk_event->sched_unit) == current_thread);
    SchedulerHashInsert(blocklist, blk_event->worldrank, blk_event->mpi_state_on_event, blk_event->action_on_event, blk_event->sched_unit);
}

inline void sched_block_queue_unblock_thread(void* event)
{
    schedQueue_itemptr unblk_event = (schedQueue_itemptr) event;
    schedQueue_itemptr stored = NULL;
    SchedulerHashRemove(blocklist, unblk_event->worldrank, &stored);
    if(stored){
        sched_block_queue_add_thread( (fgmpi_thread_t*) stored->sched_unit);
        free(stored);
    }
}


inline void sched_block_queue_start_progress_thread(void)
{
    if ( (!progress_thread_running) ) {
        scheduler_event PTunblock = {PROGRESS_THREAD_RANK, INIT, UNBLOCK, NULL};
        sched_block_queue_unblock_thread(&PTunblock);
        progress_thread_running = 1;
    }
}

inline void sched_block_queue_init_progress_thread(void)
{
    assert(!progress_thread_running);
    FG_WrapperProcessPtr_t progressFunc = &FG_Scheduler_progress_loop;
    progress_thread = thread_spawn("FG_PROGRESS_THREAD", (FG_WrapperProcessPtr_t)(progressFunc), NULL);
    assert (NULL != progress_thread);

    /* It is on blocklist for now, it is started when the first MPI process blocks */
    scheduler_event PTblk = {PROGRESS_THREAD_RANK, INIT, BLOCK, progress_thread};
    SchedulerHashInsert(blocklist, PTblk.worldrank, PTblk.mpi_state_on_event, PTblk.action_on_event, PTblk.sched_unit);

}

int is_progress_thread_running(void){
    return (progress_thread_running);
}
