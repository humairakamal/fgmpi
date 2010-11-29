/*
 * Copyright © 2009 CNRS
 * Copyright © 2009-2010 INRIA
 * Copyright © 2009 Université Bordeaux 1
 * See COPYING in top-level directory.
 */

#include <private/config.h>
#include <hwloc.h>

#include <assert.h>

/* check hwloc_bitmap_first(), _last(), _next() and _weight() */

int main(void)
{
  hwloc_bitmap_t set;
  int i, cpu, expected_cpu = 0;

  /* empty set */
  set = hwloc_bitmap_alloc();
  assert(hwloc_bitmap_first(set) == -1);
  assert(hwloc_bitmap_last(set) == -1);
  assert(hwloc_bitmap_next(set, 0) == -1);
  assert(hwloc_bitmap_weight(set) == 0);

  /* full set */
  hwloc_bitmap_fill(set);
  assert(hwloc_bitmap_first(set) == 0);
  assert(hwloc_bitmap_last(set) == -1);
  assert(hwloc_bitmap_next(set, 0) == 1);
  assert(hwloc_bitmap_next(set, 1) == 2);
  assert(hwloc_bitmap_next(set, 2) == 3);
  assert(hwloc_bitmap_next(set, 30) == 31);
  assert(hwloc_bitmap_next(set, 31) == 32);
  assert(hwloc_bitmap_next(set, 32) == 33);
  assert(hwloc_bitmap_next(set, 62) == 63);
  assert(hwloc_bitmap_next(set, 63) == 64);
  assert(hwloc_bitmap_next(set, 64) == 65);
  assert(hwloc_bitmap_next(set, 12345) == 12346);
  assert(hwloc_bitmap_weight(set) == -1);

  /* custom sets */
  hwloc_bitmap_zero(set);
  hwloc_bitmap_set_range(set, 36, 59);
  assert(hwloc_bitmap_first(set) == 36);
  assert(hwloc_bitmap_last(set) == 59);
  assert(hwloc_bitmap_next(set, 0) == 36);
  assert(hwloc_bitmap_next(set, 36) == 37);
  assert(hwloc_bitmap_next(set, 59) == -1);
  assert(hwloc_bitmap_weight(set) == 24);
  hwloc_bitmap_set_range(set, 136, 259);
  assert(hwloc_bitmap_first(set) == 36);
  assert(hwloc_bitmap_last(set) == 259);
  assert(hwloc_bitmap_next(set, 59) == 136);
  assert(hwloc_bitmap_next(set, 259) == -1);
  assert(hwloc_bitmap_weight(set) == 148);
  hwloc_bitmap_clr(set, 199);
  assert(hwloc_bitmap_first(set) == 36);
  assert(hwloc_bitmap_last(set) == 259);
  assert(hwloc_bitmap_next(set, 198) == 200);
  assert(hwloc_bitmap_next(set, 199) == 200);
  assert(hwloc_bitmap_weight(set) == 147);

  i = 0;
  hwloc_bitmap_foreach_begin(cpu, set) {
    if (0 <= i && i < 24)
      expected_cpu = i + 36;
    else if (24 <= i && i < 87)
      expected_cpu = i + 112;
    else if (87 <= i && i < 147)
      expected_cpu = i + 113;

    assert(expected_cpu == cpu);

    i++;
  } hwloc_bitmap_foreach_end();

  hwloc_bitmap_free(set);

  return 0;
}