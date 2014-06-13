#ifndef CLOCK_H
#define CLOCK_H

#if defined(__i386__)

static __inline__ unsigned long long rdtsc(void)
{
  unsigned long long int x;
     __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
     return x;
}
#elif defined(__x86_64__)

static __inline__ unsigned long long rdtsc(void)
{
  unsigned hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}
#endif

typedef long long cpu_tick_t;

#define GET_REAL_CPU_TICKS(Var)	Var=rdtsc()
//#define GET_REAL_CPU_TICKS(Var)	__asm__ __volatile__ ("_rdtsc" : "=A" (Var))
// interestingly, without __volatile__, it's slower
//#define GET_REAL_CPU_TICKS(Var)	__asm__ ("rdtsc" : "=A" (Var))




# define TIMING_NOW_64(Var) GET_REAL_CPU_TICKS(Var)
# define GET_CPU_TICKS(Var) GET_REAL_CPU_TICKS(Var)



extern cpu_tick_t ticks_per_second;
extern cpu_tick_t ticks_per_millisecond;
extern cpu_tick_t ticks_per_microsecond;
extern cpu_tick_t ticks_per_nanosecond;
extern cpu_tick_t real_start_ticks;
extern cpu_tick_t virtual_start_ticks;


static inline long long current_usecs()
{
  register cpu_tick_t ret;
  GET_REAL_CPU_TICKS( ret );
  return (ret / ticks_per_microsecond);
}

void init_cycle_clock(void);

#endif


