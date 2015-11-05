/*
 *  (C) 2008 Humaira Kamal, The University of British Columbia.
 *      See COPYRIGHT in top-level directory.
 *      Part of this code was derived from the Capriccio source
 *      at http://capriccio.cs.berkeley.edu/
 */

#ifndef THREADLIB_INTERNAL_H
#define THREADLIB_INTERNAL_H

#if defined(ETCORO) /* Original coro library. http://www.goron.de/~froese/coro/coro.html */
#include "coro.h"
#else /* PCL coroutine library */
#include "pcl.h"
#include "pcl_private.h"
#endif
#include "threadlib.h"
#include "util.h"
#include "fgmpi.h"

#ifdef USE_NIO
#include <libaio.h>
#endif

extern void* fatalerror(char *fmt, ...);

#define MALLOC(retval, len, type, cast) ((retval=(cast)MPIU_Malloc(len * sizeof(type)))!=NULL) ? retval : fatalerror("MALLOC error")


struct FWraparg {
    int argc;
    char ** argv;
    FG_ProcessPtr_t FG_FPtr;
};
typedef struct FWraparg FWraparg_t;
typedef void* (*FG_WrapperProcessPtr_t)(void*);
typedef enum FGP_Init_State_t { FGP_PRE_INIT=0, FGP_WITHIN_INIT=1,
                                FGP_POST_INIT=2, FGP_ALL_POST_INIT=3 } FGP_Init_State_t;
extern FGP_Init_State_t FGP_init_state;
extern FWraparg_t* FG_Wrapargs;
int runlist_init();

/*
  Abstracting coroutine calls so that PCL (Portable
  Coroutine Library) http://www.xmailserver.org/pcl.html
  may be used instead of original coro library.
  The objective is to be able to switch between the two.
*/
#define CO_DELETE(_co) co_delete(_co)

#if defined(ETCORO)
#define CO_CURRENT co_current
#define CO_MAIN co_main
#define CO_CALL(_co, _data) co_call(_co, _data)
#define CO_CREATE(_func, _data, _stack, _stacksize) co_create(_func, _stack, _stacksize)
#else
#define CO_CURRENT ((coroutine*)co_current())
#define CO_MAIN    &(co_get_thread_ctx()->co_main)
#define CO_CALL(_co, _data) co_call(_co)
#define CO_CREATE(_func, _data, _stack, _stacksize) (coroutine*)co_create(_func, _data, _stack, _stacksize)
#endif


/* provide a way to adjust the behavior when unimplemented function is called */
#define notimplemented(x) Output("WARNING: " #x " not implemented!\n")

/* thread attributes */
/* This is a separate struct because pthread has API for users to initizalize */
/* pthread_attr_t structs before thread creation */
struct _thread_attr {
  thread_t *thread;     /* != NULL when is bound to a thread */

  /* Attributes below are valid only when thread == NULL */
  int joinable:1;
  int daemon:1;

  char *name;
};

#define THREAD_SIG_MAX 32


struct thread_st {
  unsigned tid;   /* thread id, mainly for readability of debug output */
#if defined(ETCORO)
    struct coroutine *coro;
#else /* PCL coroutine library */
    coroutine *coro;
#endif
  void *stack;
  void *stack_bottom;
  int stack_fingerprint;

  /* misc. short fields, grouped to save space */
  enum STATE{
      RUNNABLE=0,
      SUSPENDED,
      ZOMBIE,        /* not yet reaped, for joinable threads */
      GHOST          /* already freed, no valid thread should be in this state */
  } state:3;

  unsigned int joinable:1;
  unsigned int daemon:1;
  unsigned int key_data_count:8;   /* big enough for THREAD_KEY_MAX */
  unsigned int timeout:1;          /* whether it is waken up because of timeout */
  unsigned int sig_waiting:1;	 /* the thread is waiting for a signal (any not blocked in sig_mask). */
 			 /* when it arrives, the thread should be waken up and */
  			 /* the signal handler should *not* be called */
  short sig_num_received;	 /* number of signals in sigs_received */

#ifdef USE_NIO
  struct iocb iocb;         /* aio control block */
  int ioret;                /* i/o ret value, set by polling loop and used by wrapping functions */
  int iocount;		    /* number of I/O operations done without blocking, */
                            /* used to yield the processor when reached a fixed amount */
#endif

  /* startup stuff */
  void* (*initial_func)(void *);
  void *initial_arg;
  char *name;
  int stack_size_kb_log2;

  const void **key_data_value;  /* thread specific values */


  /* per-thread signals */
  /* NOTE: external(global) signals are in corresponding global variable */
  sigset_t sig_received;	/* per-thread received but unhandled signals */
  sigset_t sig_mask;		/* masked signals for this thread */

  thread_t *join_thread;   /* thread waiting for the completion of the thread */
  void *ret;               /* return value, returned to user by thread_join() */

  long long sleep;         /* relative time for this thread to sleep after the prev one in sleep queue */
                           /* -1 means thread is not in the sleep queue */
};



extern thread_t *current;
extern int in_scheduler;


/* scheduler functions */
#define PROGRESS_THREAD_RANK -200
extern void* FG_Scheduler_progress_loop(void* args);
extern thread_t * progress_thread;
extern int runlist_size(void);
extern int is_progress_thread_running(void);
extern void noop(void);

#define DECLARE_SCHEDULER(s)  \
  extern void s##_init(void); \
  extern void s##_add_thread(thread_t *t); \
  extern thread_t* s##_next_thread(void); \
  extern void s##_block_thread(thread_t *t, void* event);   \
  extern void s##_unblock_thread(void* event);

#define DECLARE_SCHEDULER_PROGRESS_THREAD(s)  \
  extern void s##_init_progress_thread(void); \
  extern void s##_start_progress_thread(void);

#define DECLARE_SCHEDULER_ADD_INIT(s)               \
    extern void s##_add_init_thread(thread_t *t);

#define DECLARE_SCHEDULER_NEXT_INIT(s)           \
  extern thread_t* s##_next_init_thread(void);



/* Round robin scheduler */
DECLARE_SCHEDULER(sched_global_rr);
#define sched_global_rr_add_init_thread sched_global_rr_add_thread
#define sched_global_rr_next_init_thread sched_global_rr_next_thread
#define sched_global_rr_init_progress_thread noop
#define sched_global_rr_start_progress_thread noop



/* Inverse round robin scheduler */
DECLARE_SCHEDULER(sched_global_invrr);
DECLARE_SCHEDULER_ADD_INIT(sched_global_invrr)
DECLARE_SCHEDULER_NEXT_INIT(sched_global_invrr)
#define sched_global_invrr_init_progress_thread noop
#define sched_global_invrr_start_progress_thread noop


/* Scheduler with a hash block-queue */
DECLARE_SCHEDULER(sched_block_queue);
#define sched_block_queue_add_init_thread sched_block_queue_add_thread
#define sched_block_queue_next_init_thread sched_block_queue_next_thread
DECLARE_SCHEDULER_PROGRESS_THREAD(sched_block_queue)




#define strong_alias(name, aliasname) extern __typeof (name) aliasname __attribute__ ((alias (#name)));
#define valid_thread(t) (t != NULL  &&  t != (thread_t*)-1)
#define thread_alive(t) ((t)->state == RUNNABLE || (t)->state == SUSPENDED)

/* Internal constants */

#define _BIT(n) (1<<(n))
#define THREAD_RWLOCK_INITIALIZED _BIT(0)

#define THREAD_COND_INITIALIZED _BIT(0)

#ifndef FALSE
#define FALSE (0)
#endif
#ifndef TRUE
#define TRUE 1
#endif

#define return_errno(return_val,errno_val) \
    do { /*errno = (errno_val);*/                          \
             assert(0);                                    \
             debug("return 0x%lx with errno %d(\"%s\")\n", \
                        (unsigned long)(return_val), (errno), strerror((errno))); \
             return (return_val); } while (0)


extern long long total_stack_in_use;

/* process all pending signals.  returns 1 is any actually handled, 0 otherwise */
/* extern inline int sig_process_pending(); */

extern thread_t *scheduler_thread;

#endif /* THREADLIB_INTERNAL_H */
