#include "stacklink.h"
#include <stdlib.h>
#include <stdint.h>
#include "util.h"

#ifndef DEBUG_stack_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

#define MAX_BUCKETS 32

int stack_fingerprint = 0;

int stack_extern = 0;
int stack_check_link = 0;
int stack_check_nolink = 0;
int stack_nocheck = 0;

int stack_waste_ext = 0;
int stack_waste_int = 0;
int stack_allocchunks[MAX_BUCKETS] =
{
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
};

/* disable stack checking for initial thread */
void *stack_bottom = NULL;
void *stack_freechunks[MAX_BUCKETS] =
{
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
};

/*
  stack_alloc_many_chunks, allocates chunks of size = bucket*1024 bytes.
  stack_freechunks[] array stores pointer to list of chunks of size (bucket+1)*1024 at
  array location stack_freechunks[bucket]. E.g. pointers to list of chunks of size 1K
  are stored at stack_freechunks[0], those of size 2K are stored at stack_freechunks[1] and so-on.

  The list of chunks has the following format:-
  Suppose I want to have a list of free chunks of size 2K. stack_alloc_many_chunks() will allocate
  an contiguous array of size 64KB. It will then divide it into chunks of 2K each and will store the
  starting address of next chunk in the list in the second-last byte of the current chunk. Note,
  bucket=1 in this example. stack_freechunks[1] will point to the first 2K chunk in this list.
  When a chunk is used up, stack_freechunks[1] will point to next free chunk. See macro
  GET_CHUNK for reference.
*/
void stack_alloc_many_chunks(int bucket)
{
  int bytes = 1 << 16; /* 64 KB */
  char *p = (char*) malloc(bytes);

  if (p != NULL) {
    int chunkbytes = 1 << (bucket + 10);
    char *base = p;

    /* Note: Using (intptr_t) is important for portability to 64-bit machines.
       Previous (int) cast results in loss of data because int is half as big as
       void*.
       See http://stackoverflow.com/questions/2024895/how-should-i-handle-cast-from-void-to-int-loses-precision-when-compiling-3
    */
    /* get pointer to end of memory area and align */
    p += bytes;
    p = (char*) (((intptr_t) p) & ~(chunkbytes - 1)); 

    /* we should get at least one chunk */
    assert((p - base) >= chunkbytes);

    while ((p - base) >= chunkbytes) {
      *(((void**) p) - 1) = stack_freechunks[bucket];
      stack_freechunks[bucket] = p;

      debug("allocated %p\n", p);

      p -= chunkbytes;
    }
  } else {
    Output("malloc error\n");
    abort();
  }
}

void stack_alloc_one_chunk(int bucket)
{
  int bytes = 1 << (bucket + 10);
  char *p = (char*) malloc(bytes);

  if (p != NULL) {
    p += bytes;

    *(((void**) p) - 1) = stack_freechunks[bucket];
    stack_freechunks[bucket] = p;

    debug("allocated %p\n", p);
  } else {
    Output("malloc error\n");
    abort();
  }
}

void stack_alloc_chunk(int bucket)
{
  assert(bucket >= 0);
  if (bucket < 5) {
    stack_alloc_many_chunks(bucket);
  } else {
    stack_alloc_one_chunk(bucket);
  }
}

void *stack_get_chunk(int bucket)
{
  void *chunk;
  GET_CHUNK(bucket, chunk);
  return chunk;
}

void stack_return_chunk(int bucket, void *chunk)
{
    /* FIXME: this needs to follow any chained pages that still remain, in the case of pthread_exit() */
    if(chunk){
        RETURN_CHUNK(bucket, chunk);
    }
}

void stack_report_call_stats(void)
{
  Output("links:      %d check/links    %d check/nolinks\n"
         "            %d externals    %d nochecks\n",
         stack_check_link, stack_check_nolink,
         stack_extern, stack_nocheck);
}

void stack_report_usage_stats(void)
{
  int total = 0;
  int size;
  int i;

  for (i = 0, size = 1; i < MAX_BUCKETS; i++, size <<= 1) {
    total += (stack_allocchunks[i] * size);
  }

  Output("stack:      %d KB allocated    %d KB internal waste\n",
         total, stack_waste_int / 1024);
}

void stack_report_link(void *chunk, int used, int node, int succ)
{
  Output("linking stack %p (used %d node %d succ %d )\n",
         chunk, used, node, succ);
}

void stack_report_unlink(void *chunk)
{
  Output("unlinking stack %p\n", chunk);
}

void stack_report_overflow(void)
{
  abort();
}

void stack_report_unreachable(int id, char *name)
{
  Output("error: reached unreachable node (%d, %s)\n", id, name);
  abort();
}
