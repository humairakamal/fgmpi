/*
 *  (C) 2008 Humaira Kamal, The University of British Columbia.
 *      See COPYRIGHT in top-level directory.
 *      Part of this code was derived from the Capriccio source
 *      at http://capriccio.cs.berkeley.edu/
 */

#include "threadlib_internal.h"
#if defined(ETCORO)
#include "coro.h"
#else
#include "pcl.h"
#include "pcl_private.h"
#endif

#include <inttypes.h>
#include "util.h"
#include "utilconfig.h" /* TODO TBR some vars */
#include "stacklink.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <execinfo.h>
#include <sys/time.h>
#include <unistd.h>
#if !defined(__APPLE__)
#include <syscall.h>
#endif
#include <sys/syscall.h>

/* comment out, to enable debugging in this file */
#ifndef DEBUG_threadlib_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif


/* FIXME: this doesn't work properly yet in all cases at program exit. */
/* The performance does seem slightly better, however. */
/* #define NO_SCHEDULER_THREAD 1 */


/* sanity check THREAD_KEY_MAX and size of key_data_count */
/* #if THREAD_KEY_MAX >> (sizeof(fgmpi_thread_t.key_data_count)-1) != 1 */
/* #error not enough space in fgmpi_thread_t.key_data_count */
/* #endif */


/* The PID of the main process.  This is useful to prevent errors when */
/* forked children call exit_func(). */

/* FIXME: unfortunately, this still doesn't seem to work, as AIO gets */
/* hosed if a forked child exits before the main thread.  This may be */
/* a bug w/ AIO, however. */
static pid_t capriccio_main_pid;

/* flag so the signal stuff knows that the current thread is running in the scheduler */
int in_scheduler = 0;

void **start_node_addrs = NULL;
int *start_node_stacks = NULL;

#ifdef USE_NIO
#define SCHEDULER_STACK_SIZE 1024*128
#else
#if defined(ETCORO)
#define SCHEDULER_STACK_SIZE 1024
#else
#define SCHEDULER_STACK_SIZE 1024*4
#endif
#endif

static fgmpi_thread_t* main_thread=NULL;
#ifndef NO_SCHEDULER_THREAD
fgmpi_thread_t* scheduler_thread=NULL;
#endif
fgmpi_thread_t* current_thread=NULL;
static int current_thread_exited = 0;

/* a list of all threads, used by sig_handler() */
pointer_list_t *threadlist = NULL;

static int num_daemon_threads = 0;
static int num_suspended_threads = 0;
int num_runnable_threads = 0;
static int num_zombie_threads = 0;

#define sanity_check_threadcounts() {          \
           assert(num_daemon_threads >= 0);    \
           assert(num_suspended_threads >= 0); \
           assert(num_runnable_threads >= 0);  \
           assert(num_zombie_threads >= 0);    \
           assert(num_runnable_threads + num_suspended_threads + num_zombie_threads == pl_size(threadlist)); \
           }

/* modular scheduling functions */
static void (*sched_init)(void);
static void (*sched_add_thread)(fgmpi_thread_t *t);
static fgmpi_thread_t* (*sched_next_thread)(void);
static void (*sched_add_init_thread)(fgmpi_thread_t *t);
static fgmpi_thread_t* (*sched_next_init_thread)(void);
static void (*sched_block_thread)(fgmpi_thread_t *t, void *event);
static void (*sched_unblock_thread)(void *event);
void (*sched_init_progress_thread)(void);
void (*sched_start_progress_thread)(void);

/* flags regarding the state of main_thread */
int exit_whole_program = 0;
static int exit_func_done = 0;
static int main_exited = 0;


/* sleep queue, points to fgmpi_thread_t that's sleeping */
static pointer_list_t *sleepq = NULL;
static unsigned long long last_check_time = 0;       /* when is the sleep time calculated from */
static unsigned long long max_sleep_time=0;          /* length of the whole sleep queue, in microseconds */
static unsigned long long first_wake_usecs=0;        /* wall clock time of the wake time of the first sleeping thread */

inline static void free_thread( fgmpi_thread_t *t );
inline static void sleepq_check(int sync);
inline static void sleepq_add_thread(fgmpi_thread_t *t, unsigned long long timeout);
inline static void sleepq_remove_thread(fgmpi_thread_t *t);


unsigned long long start_usec;

static cap_timer_t scheduler_timer;
static cap_timer_t main_timer;
static cap_timer_t app_timer;

/**
 * Main scheduling loop
 **/
static void* do_scheduler(void *arg)
{
  static cpu_tick_t next_poll=0, next_info_dump=0, now=0;
  static int pollcount=1000;
  static int init_done = 0;

  (void) arg;  /* suppress GCC "unused parameter" warning */

  in_scheduler = 1;

  /* make sure we start out by saving edge stats for a while */
  if( !init_done ) {
    init_done = 1;
    start_timer(&scheduler_timer);
  }

  while( 1 ) {

    /* current_thread = scheduler_thread; */
    sanity_check_threadcounts();

    /* wake up threads that have timeouts */
    sleepq_check(0);
    sanity_check_threadcounts();

    /* break out if there are only daemon threads */
    if(unlikely (num_suspended_threads == 0  &&  num_runnable_threads == num_daemon_threads)) {

      /* go back to mainthread, which should now be in exit_func() */
      current_thread = main_thread;
      in_scheduler = 0;
      CO_CALL(main_thread->coro, NULL);
      in_scheduler = 1;

      if( unlikely(current_thread_exited) ) {     /* free memory from deleted threads */
        current_thread_exited=0;
        if (current_thread != main_thread)  /* main_thread is needed for whole program exit */
          free_thread( current_thread );
      }

      return NULL;
    }


    /* get the head of the run list */
    current_thread = sched_next_thread(); /* e.g. sched_global_rr_next_thread() */

    /* scheduler gave an invlid even though there are runnable */
    /* threads.  This indicates that every runnable thead is likely to */
    /* require use of an overloaded resource. */
    if( !valid_thread(current_thread) ) {
      pollcount = 0;
      continue;
    }

    /* barf, if the returned thread is still on the sleep queue */
    assert( current_thread->sleep == -1 );

    tdebug("running TID %d (%s)\n", current_thread->tid, current_thread->name ? current_thread->name : "no name");

    sanity_check_threadcounts();


    /* call thread */
    stop_timer(&scheduler_timer);
    start_timer(&app_timer);
    in_scheduler = 0;
    CO_CALL(current_thread->coro, NULL);
    in_scheduler = 1;
    stop_timer(&app_timer);
    start_timer(&scheduler_timer);

    if( unlikely(current_thread_exited) ) {      /* free memory from deleted threads */
      current_thread_exited=0;
      if (current_thread != main_thread)  /* main_thread is needed for whole program exit */
        free_thread( current_thread );
    }

#ifdef NO_SCHEDULER_THREAD
    return NULL;
#endif
  } /* matches while(1) */

  return NULL;
}



static int get_stack_size_kb_log2(void *func)
{
  int result = conf_new_stack_kb_log2;
  if (start_node_addrs != NULL) {
    int i = 0;
    while (start_node_addrs[i] != NULL && start_node_addrs[i] != func) {
      i++;
    }
    if (start_node_addrs[i] == func) {
      result = start_node_stacks[i];
    } else {
      fatal("Couldn't find stack size for thread entry point %p\n", func);
    }
  }
  return result;
}


/**
 * Wrapper function for new threads.  This allows us to clean up
 * correctly if a thread exits without calling thread_exit().
 **/
static void* new_thread_wrapper(void *arg)
{
  void *ret;
  (void) arg;

  /* set up stack limit for new thread */
  stack_bottom = current_thread->stack_bottom;
  stack_fingerprint = current_thread->stack_fingerprint;

  /* start the thread */
  tdebug("Initial arg = %p\n", current_thread->initial_arg);
  ret = current_thread->initial_func(current_thread->initial_arg);

  /* call thread_exit() to do the cleanup */
  thread_exit(ret);

  return NULL;
}

static fgmpi_thread_t* new_thread(char *name, void* (*func)(void *), void *arg, thread_attr_t attr)
{
  static unsigned max_tid = 1;
  fgmpi_thread_t *t = (fgmpi_thread_t *) malloc( sizeof(fgmpi_thread_t) );
  assert(t!=NULL); 
  int stack_size_kb_log2 = get_stack_size_kb_log2(func);
  /*FG: Previously was assigning stack through stack_get_chunk
    void *stack = stack_get_chunk( stack_size_kb_log2 );
    but now co_create allocates the stack.
    The stack_get_chunk is faulty and results in a sudden spike in
    memory allocation in thread_spawn
    of coroutines during mpiexec -nfg 30000 -n 17 ./ringtest 1 . This spike was not seen if the
    RTWmap for MPI_WORLD_COMM was populated by the main co before thread_spawn. Since RTWmap
    for MPI_WORLD_COMM is redundant and takes a lot of memory, so it has been removed and
    allowing co_create to allocate the stack fixed this problem.
  */
  void *stack = NULL;
  int stack_size = 1 << (stack_size_kb_log2 + 10);
  bzero(t, sizeof(fgmpi_thread_t));


  t->coro = CO_CREATE((void*)new_thread_wrapper, NULL, stack, stack_size); /* stack is NULL in this call */
  if(t->coro == NULL){
      printf("CO_CREATE is returning NULL  new_thread_wrapper stack_size=%d---------------------FAILURE!!!!!\n",  stack_size);
      assert(0);
  }
  t->stack = stack;
  t->stack_size_kb_log2 = stack_size_kb_log2;
  t->stack_bottom = (stack - stack_size); /* FG: TODO Cast stack to (char*) for portability?
                                             A (char*) cast represents 1 byte.
                                             See: http://gcc.gnu.org/onlinedocs/gcc-4.1.2/gcc/Pointer-Arith.html
                                          */
  t->stack_fingerprint = 0;
  t->name = (name ? name : "noname");

  t->initial_func = func;
  t->initial_arg = arg;
  t->joinable = 1;
  t->tid = max_tid++;
  t->sleep = -1;

  if( attr ) {
    t->joinable = attr->joinable;
    t->daemon = attr->daemon;
    if(t->daemon)
      num_daemon_threads++;
  }

  /* FIXME: somehow track the parent thread, for stats creation? */

  pl_add_tail(threadlist, t);
  num_runnable_threads++;

  /* If t is not the progress thread */
  if (strcmp(name,"FG_PROGRESS_THREAD")){
      sched_add_thread(t);
  }
  sanity_check_threadcounts();

  return t;
}


/**
 * Free the memory associated with the given thread.
 **/
inline static void free_thread( fgmpi_thread_t *t )
{
  static int iter = -1;
  iter++;
  pl_remove_pointer(threadlist, t);

  assert(t->state == ZOMBIE);
  t->state = GHOST;   /* just for good measure */
  num_zombie_threads--;

  if( t != main_thread ) {
    CO_DELETE( t->coro );
    if(t->stack){
        stack_return_chunk( t->stack_size_kb_log2, t->stack );
    }
    free( t );
  }
}

/*

void exit(int code) {
	fprintf (stderr, "exit called!");
	exit_whole_program = 1;
    syscall(SYS_exit, code);
faint: goto faint;
}
*/

#ifndef NO_ATEXIT
/**
 * give control back to the scheduler after main() exits.  This allows
 * remaining threads to continue running.
 * FIXME: we don't know whether user explicit calls exit() or main() normally returns
 * in the previous case, we should exit immediately, while in the later, we should
 * join other threads.
 * Overriding exit() does not work because normal returning from
 * main() also calls exit().
 **/
static void exit_func(void)
{
  /* don't do anything if we're in a forked child process */
  if( getpid() != capriccio_main_pid )
    return;

  exit_func_done = 1;
  main_exited = 1;
  if( !exit_whole_program )
  	/* this will block until all other threads finish */
    thread_exit(NULL);

  
  /* FIXME: make sure to kill cloned children */

  if( conf_dump_timing_info ) {
    if( main_timer.running )   stop_timer(&main_timer);
    if( scheduler_timer.running )   stop_timer(&scheduler_timer);
    if( app_timer.running )   stop_timer(&app_timer);
    print_timers();
  }
}
#endif

static char *THREAD_STATES[] = {"RUNNABLE", "SUSPENDED", "ZOMBIE", "GHOST"};



/**
 * decide on the scheduler to use, based on the CAPRICCIO_SCHEDULER
 * environment variable.  This function should only be called once,
 * durring the initialization of the thread runtime.
 **/
#define SET_SCHEDULER(s) do {\
  sched_init = sched_##s##_init; \
  sched_next_thread = sched_##s##_next_thread; \
  sched_add_thread = sched_##s##_add_thread; \
  sched_add_init_thread = sched_##s##_add_init_thread; \
  sched_next_init_thread = sched_##s##_next_init_thread;       \
  sched_block_thread = sched_##s##_block_thread; \
  sched_unblock_thread = sched_##s##_unblock_thread; \
  sched_init_progress_thread = sched_##s##_init_progress_thread; \
  sched_start_progress_thread = sched_##s##_start_progress_thread; \
  if( !conf_no_init_messages ) \
    Output("SCHEDULER=%s\n",__STRING(s)); \
} while(0)

static void pick_scheduler()
{
  char *sched = getenv("SCHEDULER");

  if(sched == NULL)
    SET_SCHEDULER( global_rr ); /* default */
  else if( !strcasecmp(sched,"rr") )
    SET_SCHEDULER( global_rr );
  else if( !strcasecmp(sched,"invrr") )
    SET_SCHEDULER( global_invrr );
  else if( !strcasecmp(sched,"block") )
    SET_SCHEDULER( block_queue );
  else
    fatal("Invalid value for SCHEDULER: '%s'\n",sched);

}


/**
 * perform necessary management to yield the current thread
 * if suspended == TRUE && timeout != 0 -> the thread is added
 * to the sleep queue and later waken up when the clock times out
 * returns FALSE if time-out actually happens, TRUE if waken up
 * by other threads, INTERRUPTED if interrupted by a signal
 **/
static int _thread_yield_internal(int suspended,  void *incoming_event, unsigned long long timeout)
{
 /* now we use a per-thread errno stored in fgmpi_thread_t */
  int savederrno;
  int rv = OK;
  schedQueue_itemptr event = (schedQueue_itemptr) incoming_event;
  Scheduler_action_kind_t incoming_action = NONE;
  if (NULL != event){
      incoming_action = event->action_on_event;
  }

  tdebug("current_thread=%p\n",current_thread);

  savederrno = errno;

  /* decide what to do with the thread */
  if( (!suspended) && (incoming_action != BLOCK)) { /* just add it to the runlist */
    sched_add_thread( current_thread );
  }
  else if (incoming_action == BLOCK){
      /* Create a progress engine thread the first time a block event happens */
      sched_start_progress_thread();
      /* Add it to blocklist. For those schedulers that do not have a blocklist, this simply adds to the runlist */
      sched_block_thread( current_thread, incoming_event);
  }
  else if( timeout ) { /* add to the sleep list */
    sleepq_add_thread( current_thread, timeout);
  }



  /* squirrel away the stack limit for next time */
  current_thread->stack_bottom = stack_bottom;
  current_thread->stack_fingerprint = stack_fingerprint;

  /* switch to the scheduler thread */
#ifdef NO_SCHEDULER_THREAD
  do_scheduler(NULL);
#else
  CO_CALL(scheduler_thread->coro, NULL);
#endif

  /* set up stack limit for new thread */
  stack_bottom = current_thread->stack_bottom;
  stack_fingerprint = current_thread->stack_fingerprint;

  /* check whether time-out happens */
  if (suspended && timeout && current_thread->timeout) {
    rv = TIMEDOUT;
    current_thread->timeout = 0;
  }

  return rv;
}


static int thread_yield_internal(int suspended, unsigned long long timeout)
{
    return _thread_yield_internal(suspended, NULL, timeout);
}


/* ////////////////////////////////////////////////////////////////////// */
/*
/*   External functions */
/*
/* ////////////////////////////////////////////////////////////////////// */

void* fatalerror(char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    fprintf(stderr, "FATAL : ");
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    exit(1);
    return (NULL);
}


int runlist_init()
{
  /* now we use a per-thread errno stored in fgmpi_thread_t */
  int savederrno;
  int rv = OK;

  tdebug("current_thread=%p\n",current_thread);

  savederrno = errno;

  sched_add_init_thread( current_thread );

  /* squirrel away the stack limit for next time */
  current_thread->stack_bottom = stack_bottom;
  current_thread->stack_fingerprint = stack_fingerprint;

  current_thread = sched_next_init_thread();

  CO_CALL(current_thread->coro, NULL);

  /* set up stack limit for new thread */
  stack_bottom = current_thread->stack_bottom;
  stack_fingerprint = current_thread->stack_fingerprint;

  return (rv);
}



/**
 * This will be called automatically, either by routines here, or by
 * the AIO routines
 **/
static void thread_init()  __attribute__ ((constructor));
static void thread_init()
{
  static int init_done = 0;

  capriccio_main_pid = getpid();

  /* read config info from the environment */
  read_config();

  if(init_done)
    return;
  init_done = 1;

  /* make sure the clock init is already done, so we don't wind up w/ */
  /* a dependancy loop b/w perfctr and the rest of the code. */
  init_cycle_clock();
  init_debug();

  /* start main timer */
  init_timer(&main_timer);
  register_timer("total", &main_timer);
  start_timer(&main_timer);

  init_timer(&scheduler_timer);
  register_timer("sheduler", &scheduler_timer);

  init_timer(&app_timer);
  register_timer("app", &app_timer);

  /* init scheduler function pointers */
  pick_scheduler();

  /* init the scheduler code */
  sched_init();

  /* create the main thread */
  main_thread = (fgmpi_thread_t *) malloc(sizeof(fgmpi_thread_t));
  assert(main_thread);
  bzero(main_thread, sizeof(fgmpi_thread_t));
  main_thread->name = "main_thread";
  main_thread->coro = CO_MAIN;
  main_thread->initial_arg = NULL;
  main_thread->initial_func = NULL;
  main_thread->tid = 0;   /* fixed value */
  main_thread->sleep = -1;
  current_thread = main_thread;

  /* create the scheduler thread */
#ifndef NO_SCHEDULER_THREAD
  scheduler_thread = (fgmpi_thread_t*) malloc( sizeof(fgmpi_thread_t) );
  assert(scheduler_thread);
  bzero(scheduler_thread, sizeof(fgmpi_thread_t));
  scheduler_thread->name = "scheduler";
  scheduler_thread->coro = CO_CREATE((void*)do_scheduler, 0, 0, SCHEDULER_STACK_SIZE);
  if(scheduler_thread->coro == NULL){
      printf("CO_CREATE is returning NULL  SCHEDULER_STACK_SIZE=%d---------------------FAILURE!!!!!\n",
             SCHEDULER_STACK_SIZE);
      assert(0);
  }
  scheduler_thread->tid = -1;
#endif

  /* don't exit when main exits - wait for threads to die */
#ifndef NO_ATEXIT
  atexit(exit_func);
#endif

  /* create thread list */
  threadlist = new_pointer_list("thread_list");
  /* add main thread to the list */
  pl_add_tail(threadlist, main_thread);
  num_runnable_threads++;

  /* create sleep queue */
  sleepq = new_pointer_list("sleep_queue");
  max_sleep_time = 0;
  last_check_time = 0;
  first_wake_usecs = 0;

  start_usec = current_usecs();

  /* make sure the scheduler runs.  NOTE: this is actually very */
  /* important, as it prevents a degenerate case in which the main */
  /* thread exits before the scheduler is ever called.  This will */
  /* actually cause a core dump, b/c the current_thead_exited flag */
  /* will be set, and incorrectly flag the first user thread for */
  /* deletion, rather than the main thread. */
  thread_yield_internal(FALSE, 0);

}


inline fgmpi_thread_t *thread_spawn_with_attr(char *name, void* (*func)(void *),
                                 void *arg, thread_attr_t attr)
{
  return new_thread(name, func, arg, attr);
}

inline fgmpi_thread_t *thread_spawn(char *name, void* (*func)(void *), void *arg)
{
  return new_thread(name, func, arg, NULL);
}


void thread_yield()
{
  thread_yield_internal( FALSE, 0 );
}

void thread_yield_on_event(scheduler_event yld_event)
{
    scheduler_event this_event = yld_event;
    this_event.sched_unit = (fgmpi_thread_t *) current_thread;
    _thread_yield_internal( FALSE, &this_event, 0 );
}

void thread_notify_on_event(scheduler_event notification){
    if (notification.action_on_event == UNBLOCK){
        sched_unblock_thread(&notification);
    }
    else {
        assert(0);  /* TODO other cases */
    }
}


void thread_exit(void *ret)
{
  fgmpi_thread_t *t = current_thread;

  sanity_check_threadcounts();
  tdebug("current=%s\n", current_thread?current_thread->name : "NULL");

  if (current_thread == main_thread && main_exited == 0) {
	/* the case when the user calls thread_exit() in main thread is complicated */
	/* we cannot simply terminate the main thread, because we need that stack to terminate the */
	/* whole program normally.  so we call exit() to make the c runtime help us get the stack */
	/* context where we can just return to terminate the whole program */
	/* this will call exit_func() and in turn call thread_exit() again */
      main_exited = 1;
      exit (0);
  }


  /* update thread counts */
  num_runnable_threads--;
  if( t->daemon ) num_daemon_threads--;

  t->state = ZOMBIE;
  num_zombie_threads++;

  /* deallocate the TCB */
  /* keep the thread, if the thread is Joinable, and we want the return value for something */
  if ( !( t->joinable ) ) {
    /* tell the scheduler thread to delete the current one */
    current_thread_exited = 1;
  } else {
    t->ret = ret;
    if (t->join_thread)
      thread_resume(t->join_thread);
  }

  sanity_check_threadcounts();

  /* squirrel away the stack limit--not that we'll need it again */
  current_thread->stack_bottom = stack_bottom;
  current_thread->stack_fingerprint = stack_fingerprint;

  /* give control back to the scheduler */
#ifdef NO_SCHEDULER_THREAD
  do_scheduler(NULL);
#else
  CO_CALL(scheduler_thread->coro, NULL);
#endif
}

int thread_join(fgmpi_thread_t *t, void **ret)
{
  if (t == NULL)
    return_errno(FALSE, EINVAL);
  if ( !( t->joinable ) )
    return_errno(FALSE, EINVAL);

  assert(t->state != GHOST);

  /* A thread can be joined only once */
  if (t->join_thread)
    return_errno(FALSE, EACCES);
  t->join_thread = current_thread;

  /* Wait for the thread to complete */
  tdebug( "**** thread state: %d\n" ,t->state);
  if (t->state != ZOMBIE) {
      thread_suspend_self(0);
  }

  /* clean up the dead thread */
  if (ret != NULL)
    *ret = t->ret;
  free_thread( t );

  return TRUE;
}

/* timeout == 0 means infinite time */
int thread_suspend_self(unsigned long long timeout)
{
  num_suspended_threads++;
  num_runnable_threads--;
  sanity_check_threadcounts();
  current_thread->state = SUSPENDED;
  return thread_yield_internal(TRUE, timeout);
}

/* only resume the thread internally */
/* don't touch the timeout flag and the sleep queue */
static void _thread_resume(fgmpi_thread_t *t)
{
  tdebug("t=%p\n",t);
  if (t->state != SUSPENDED)
    return;
  num_suspended_threads--;
  num_runnable_threads++;
  sanity_check_threadcounts();
  assert(t->state == SUSPENDED);
  t->state = RUNNABLE;

  assert( t->sleep == -1 );
  sched_add_thread(t);
}

void thread_resume(fgmpi_thread_t *t)
{
  /* clear timer */
  if (t->sleep != -1)
    sleepq_remove_thread(t);

  /* make the thread runnable */
  _thread_resume(t);
}

void thread_set_daemon(fgmpi_thread_t *t)
{
  if( t->daemon )
    return;

  t->daemon = 1;
  num_daemon_threads++;
}

inline char* thread_name(fgmpi_thread_t *t)
{
  return t->name;
}

void thread_exit_program(int exitcode)
{
  exit_whole_program = 1;
  raise( SIGINT );
  syscall(SYS_exit, exitcode);
}


/* Thread attribute handling */
thread_attr_t thread_attr_of(fgmpi_thread_t *t) {
  thread_attr_t attr = (thread_attr_t)malloc(sizeof(struct _thread_attr));
  attr->thread = t;
  return attr;
}

thread_attr_t thread_attr_new()
{
  thread_attr_t attr = (thread_attr_t)malloc(sizeof(struct _thread_attr));
  attr->thread = NULL;
  thread_attr_init(attr);
  return attr;
}

int thread_attr_init(thread_attr_t attr)
{
  if (attr == NULL)
    return_errno(FALSE, EINVAL);
  if (attr->thread)
    return_errno(FALSE, EPERM);
  attr->joinable = TRUE;
  return TRUE;
}

int thread_attr_set(thread_attr_t attr, int field, ...)
{
  va_list ap;
  int rc = TRUE;
  if(attr == NULL)
    return EINVAL;

  va_start(ap, field);
  switch (field) {
  case THREAD_ATTR_JOINABLE: {
    int val = va_arg(ap, int);
    if(attr->thread == NULL) {
      if( val == THREAD_CREATE_JOINABLE )
        attr->joinable = TRUE;
      else
        attr->joinable = FALSE;
    } else {
      if( val == THREAD_CREATE_JOINABLE )
        attr->thread->joinable = 1;
      else
        attr->thread->joinable = 0;
    }
    break;
  }
  default:
    notimplemented(thread_attr_set);
  }
  va_end(ap);
  return rc;
}

int thread_attr_get(thread_attr_t attr, int field, ...)
{
  va_list ap;
  int rc = TRUE;
  va_start(ap, field);
  switch (field) {
  case THREAD_ATTR_JOINABLE: {
    int *val = va_arg(ap, int *);
    int joinable = (attr->thread == NULL) ? attr->joinable : attr->thread->joinable;
    *val = joinable ? THREAD_CREATE_JOINABLE : THREAD_CREATE_DETACHED;
  }
  default:
    notimplemented(thread_attr_get);
  }
  va_end(ap);
  return rc;
}

int thread_attr_destroy(thread_attr_t attr)
{
  free(attr);
  return TRUE;
}


// for finding the location of the current errno variable
/*
int __global_errno = 0;
int *__errno_location (void)
{
	if (likely((int)current_thread))
		return &current_thread->__errno;
	else
		return &__global_errno;
}
*/

unsigned fgmpi_thread_tid(fgmpi_thread_t *t)
{
  return t ? t->tid : 0xffffffff;
}

#if 1
#define sleepq_sanity_check() \
	assert ((max_sleep_time > 0 && sleepq->num_entries > 0) \
		|| (max_sleep_time == 0 && sleepq->num_entries == 0) )
#else
#define sleepq_sanity_check() \
do { \
  assert ((max_sleep_time > 0 && sleepq->num_entries > 0) \
	| (max_sleep_time == 0 && sleepq->num_entries == 0) ); \
 { \
  linked_list_entry_t *e; \
  unsigned long long _total = 0; \
  e = ll_view_head(sleepq);\
  while (e) {\
    fgmpi_thread_t *tt = (fgmpi_thread_t *)pl_get_pointer(e);\
    assert( tt->sleep >= 0 );\
    _total += tt->sleep;\
    e = ll_view_next(sleepq, e);\
  }\
  assert( _total == max_sleep_time );\
 }\
} while( 0 );
#endif


int print_sleep_queue(void) __attribute__((unused));
int print_sleep_queue(void)
{
  linked_list_entry_t *e;
  unsigned long long _total = 0;
  e = ll_view_head(sleepq);

  while (e) {
    fgmpi_thread_t *tt = (fgmpi_thread_t *)pl_get_pointer(e);
    _total += tt->sleep;
    Output(" %s:  %lld   (%lld)\n", tt->name ? tt->name : "null", tt->sleep, _total );
    e = ll_view_next(sleepq, e);
  }
  return 1;
}

/* check sleep queue to wake up all timed-out threads */
/* sync == TRUE -> synchronize last_check_time */
static void sleepq_check(int sync)
{
  unsigned long long now;
  long long interval;
  linked_list_entry_t *e;

  if (!sync && max_sleep_time == 0) {  /* shortcut to return */
    first_wake_usecs = 0; 	/* FIXME: don't write to it every time */
    return;
  }

  sleepq_sanity_check();

  now = current_usecs();
  if( now > last_check_time )
    interval = now-last_check_time;
  else
    interval = 0;
  last_check_time = now;


  /* adjust max_sleep_time */
  if (max_sleep_time < (unsigned long long)interval)
    max_sleep_time = 0;
  else
    max_sleep_time -= interval;

  while (interval > 0 && (e = ll_view_head(sleepq))) {
    fgmpi_thread_t *t = (fgmpi_thread_t *)pl_get_pointer(e);

    if (t->sleep > interval) {
      t->sleep -= interval;
      first_wake_usecs = now + t->sleep;
      break;
    }

    interval -= t->sleep;
    t->sleep = -1;
    t->timeout = 1;

    //Output("  %10llu: thread %d timeout\n", current_usecs(), t->tid);

    _thread_resume(t);    /* this doesn't deal with sleep queue */
    ll_free_entry(sleepq, ll_remove_head(sleepq));
  }

  if (ll_size(sleepq) == 0) {
     /* the sleepq is empty again */
     first_wake_usecs = 0;
  }

  sleepq_sanity_check();
}


/* set a timer on a thread that will wake the thread up after timeout */
/* microseconds.  this is used to implement thread_suspend_self(timeout) */
static void sleepq_add_thread(fgmpi_thread_t *t, unsigned long long timeout)
{
  linked_list_entry_t *e;
  long long total_time;
  sleepq_check(1); /* make sure: last_check_time == now */

  assert(t->sleep == -1);
  sleepq_sanity_check();

  if (timeout >= max_sleep_time) {
    /* set first_wake_usecs if this is the first item */
    if( pl_view_head(sleepq) == NULL )
      first_wake_usecs = current_usecs() + timeout;

    /* just append the thread to the end of sleep queue */
    pl_add_tail(sleepq, t);
    t->sleep = timeout - max_sleep_time;
    assert( t->sleep >= 0 );
    max_sleep_time = timeout;
    sleepq_sanity_check();
    return;
  }

  /* let's find a place in the queue to insert the thread */
  /* we go backwards */
  e = ll_view_tail(sleepq);
  total_time = max_sleep_time;
  while (e) {
    fgmpi_thread_t *tt = (fgmpi_thread_t *)pl_get_pointer(e);
    assert(tt->sleep >= 0);
    total_time -= tt->sleep;

    assert (total_time >= 0); /* can be == 0 if we are adding the head item */
    if ((unsigned long long)total_time <= timeout) {
      /* insert t into the queue */
      linked_list_entry_t *newp = ll_insert_before(sleepq, e);
      pl_set_pointer(newp, t);
      t->sleep = timeout - total_time;
      assert( t->sleep > 0 );

      /* set first_wake_usecs if this is the first item */
      if( total_time == 0 )
        first_wake_usecs = current_usecs() + timeout;

      /* update the sleep time of the thread right after t */
      tt->sleep -= t->sleep;
      assert( tt->sleep > 0 );
      break;
    }

    e = ll_view_prev(sleepq, e);
  }

  assert (e != NULL);   /* we're sure to find such an e */
  sleepq_sanity_check();

  return;
}

/* remove the timer associated with the thread */
inline static void sleepq_remove_thread(fgmpi_thread_t *t)
{
  linked_list_entry_t *e;

  assert(t->sleep >= 0);  /* the thread must be in the sleep queue */
  sleepq_sanity_check();

  /* let's find the thread in the queue */
  e = ll_view_head(sleepq);
  while (e) {
    fgmpi_thread_t *tt = (fgmpi_thread_t *)pl_get_pointer(e);
    if (tt == t) {
      linked_list_entry_t *nexte = ll_view_next(sleepq, e);
      if (nexte) {
	/* e is not the last thread in the queue */
	/* we need to lengthen the time the next thread will sleep */
	fgmpi_thread_t *nextt = (fgmpi_thread_t *)pl_get_pointer(nexte);
	nextt->sleep += t->sleep;
      } else {
	/* e is the last thread, so we need to adjust max_sleep_time */
	max_sleep_time -= t->sleep;
      }
      /* remove t */
      ll_remove_entry(sleepq, e);
      ll_free_entry(sleepq, e);
      t->sleep = -1;
      assert (!t->timeout);    /* if this fails, someone must has */
                               /* forgot to reset timeout some time ago */
      break;
    }
    e = ll_view_next(sleepq, e);
  }

  assert( t->sleep == -1);
  assert (e != NULL);   /* we must find t in sleep queue */
  sleepq_sanity_check();
}


int sleepq_size(void)
{
    if (NULL == sleepq )
        return (0);
    else
        return pl_size(sleepq);
}


void thread_usleep(unsigned long long timeout)
{
  thread_suspend_self(timeout);
}
