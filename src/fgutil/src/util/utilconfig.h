#ifndef UTILCONFIG_H
#define UTILCONFIG_H


/* configuration flags */
extern int conf_no_init_messages;
extern int conf_dump_blocking_graph;
extern int conf_dump_timing_info;
extern int conf_show_thread_stacks;
extern int conf_show_thread_details;
extern int conf_no_debug;
extern int conf_no_stacktrace;
extern int conf_no_statcollect;

extern long conf_new_stack_size;
extern int conf_new_stack_kb_log2;

/* this is exported so we can force this to be initialized before the */
/* threading library. */

/* FIXME: This should really be handled by initializer priorities. */
void read_config(void);

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#endif // UTILCONFIG_H
