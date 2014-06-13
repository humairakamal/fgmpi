/**
 * routines for getting timing info from the cycle clock
 **/

//#include <stdio.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

#include "clock.h"
#include "debug.h"


/**
 * calibrate the cycle clock to wall-clock time. This is rough, but
 * hopefully good enough for our purposes.
 **/

cpu_tick_t ticks_per_nanosecond  = 6*10e2;
cpu_tick_t ticks_per_microsecond = 6*10e5;
cpu_tick_t ticks_per_millisecond = 6*10e8;
cpu_tick_t ticks_per_second      = 6*10e11;
cpu_tick_t real_start_ticks = 0;
cpu_tick_t virtual_start_ticks = 0;


#define __usecs(t) (1e6*(long long)t.tv_sec + t.tv_usec)

static long long timing_loop()
{
  struct timeval start_tv, end_tv;
  long usec;
  cpu_tick_t start_ticks, end_ticks;

  while( 1 ) {
    /* start the counter right when the clock changes */
    gettimeofday(&start_tv, NULL);
    usec = start_tv.tv_usec;
    do {
      gettimeofday(&start_tv, NULL);
      GET_REAL_CPU_TICKS( start_ticks );
    } while( start_tv.tv_usec == usec );

    /* now do the timing loop */
    do {
      gettimeofday(&end_tv, NULL);
      GET_REAL_CPU_TICKS( end_ticks );
    } while( __usecs(end_tv) < __usecs(start_tv)+1000 );

    if(__usecs(end_tv) == __usecs(start_tv)+1000)
      break;
  }

  return end_ticks - start_ticks;
}


void init_cycle_clock() __attribute__((constructor));
void init_cycle_clock(void)
{
  static int init_done = 0;
  int i;
  long long val = 0;

  if(init_done) return;
  init_done = 1;

  /* collect some samples */
  for(i=0; i<10; i++) {
    val += timing_loop();
  }
  val = val / 10;

  ticks_per_second      = val * 1e3;
  ticks_per_millisecond = val * 1e0;
  ticks_per_microsecond = val / 1e3;
  ticks_per_nanosecond  = val / 1e6;

  GET_REAL_CPU_TICKS( real_start_ticks );
  GET_CPU_TICKS( virtual_start_ticks );
}




/*

#include <sys/time.h>
#include <unistd.h>

#include "misc.h"
#include "debug.h"

#ifndef DEBUG_misc_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif


long long current_usecs()
{
  struct timeval tv;
  int rv;
  rv = gettimeofday(&tv,NULL);
  assert (rv == 0);

  return ((long long)tv.tv_sec * 1000000) + tv.tv_usec;
}

*/
