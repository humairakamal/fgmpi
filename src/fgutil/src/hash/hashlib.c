/* -------------- File hashlib.c ------------------ */
/* See hashlib.h for general notes, copyright, etc. */

/* hashlib.c is a general purpose data storage and retrieval module  */
/* Time for storage and retrieval remains virtually constant over a  */
/* very wide range.  It take virtually the same time to retrieve an  */
/* entry from a data base of 1,000,000 items as for 100 items.  It   */
/* will compile error and warning free on any ISO C compliant system */

/* Note that the user need not be concerned with the table size */

/* Copyright (C) 2002, 2006 by C.B. Falconer <cbfalconer@maineline.net>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
  USA

  I can be contacted by <mailto:cbfalconer@maineline.net>
           or at 3 Salt Bay Dr. Apt 3c, Damariscotta, ME 04543, USA.

  Should you wish to incorporate this in proprietary software for
  sale, contact me and we can discuss a different license, such
  as the GNU Library license or other mutually agreeable terms.

  v 0.0.0.5 Largely due to prodding from:
      jmccarty@sun1307.ssd.usa.alcatel.com (Mike Mccarty Sr)
  I have come to the conclusion that a hshdelete function is
  implementable, and have done so.  This also requires that a
  'hdeleted' value be added to the status.  'hentries' shows
  the sum of active and deleted entries.
  v 0.0.0.6 - minor mods for lint 2002-06-16
  v 0.0.0.7 - fixed hashkill      2002-10-18
  v 1.0.0.0 - Comments modified   2003-03-12
  v 1.0.0.1 - hshkill fix, per Hans-Juergen Taenzer
                was memory leak   2003-05-31
  v 1.0.0.2 - Increased maximum items to about 4.8 G.  Most systems
                will probably refuse to malloc sufficient memory
                before reaching this point.  No interface changes.
                On my system (450 Mhz P3 with 512 GB memory) the
                system inserts 10,000,000 items at about 5 to 10 uS per
                item, as shown by hashtest 4 10000001.  This is very
                sensitive to malloc/free performance for large numbers
                of items, and was done using my nmalloc package.
                2006-10-22

   TODO list:
   Make parameters const where possible
   Make some form of manual besides the header
   Make some better usage examples than the test suite & markov.
*/

#include <stdlib.h>
#include "hashlib.h"

/* Note: version when expressed in decimal, is of the form:  */
/* M.n.v.p  where  M = Major version                         */
/*                 n = minor version                         */
/*                 v = variation                             */
/*                 p = patch                                 */
/* with all values except M being single decimal digits.     */
#define VER  1002   /* 1.0.0.2 */

/* This is the entity that remembers all about the database  */
/* It occurs in the users data space, keeping the system     */
/* reentrant, because it is passed to all entry routines.    */
typedef struct hshtag {
   void*          *htbl;      /* points to an array of void* */
   unsigned long   currentsz;          /* size of that array */
   hshfn           hash, rehash;
   hshcmpfn        cmp;
   hshdupfn        dupe;
   hshfreefn       undupe;
   int             hdebug;          /* flag for debug output */
   hshstats        hstatus;  /* statistics, entry ct, errors */
} *hshtblptr;

/* WARN   WARN   WARN   WARN  WARN */
/* Peculiar definition, never to escape this module and NEVER */
/* NEVER to be dereferenced.   This is the equivalent of NULL */
/* NEVER EVER change the name of 'master' anywhere.           */
#define DELETED (void*)master

/* Threshold above which reorganization is desirable */
#define TTHRESH(sz) (sz - (sz >> 3))

/* Space available before reaching threshold */
/* Ensure this can return a negative value   */
#define TSPACE(m)  ((long)TTHRESH(m->currentsz) \
                  - (long)m->hstatus.hentries)

/* table of k where 2**n - k is prime, for n=8 up. 0 ends   */
/* These numbers are chosen so that memory allocation will  */
/* usually allow space for system overhead in a 2**n block  */
/*http://www.utm.edu/research/primes/lists/2small/0bit.html */
#define FIRSTN 8
static int primetbl[] = {45, 45, 41, 45, 45, 45, 45, 49,
                         57, 49, 41, 45, 59, 55, 57, 61,
                         63, 61, 45, 79, 0};
/* So the prime of interest, vs index i into above table,   */
/* is    ( 2**(FIRSTN + i) ) - primetbl[i]                  */
/* The above table suffices for about 48,000,000 entries.   */

/* 1------------------1 */

/* return a prime slightly less than 2**(FIRSTN + i) -1 */
/* return 0 for i value out of range                    */
static unsigned long ithprime(size_t i)
{
   if ((i < sizeof primetbl / sizeof (int)) && (primetbl[i]))
      return ((1 << (FIRSTN + i)) - primetbl[i]);
   else return 0;
} /* ithprime */

/* 1------------------1 */

/* Create, allocate and initialize an empty hash table      */
/* This is always doubling the table size, and the size of  */
/* all the old tables together won't hold it.  So any       */
/* freed old table space is effectively useless for this    */
/* because of fragmentation. Changing the ratio won't help. */
static void* *maketbl(unsigned long newsize)
{
   unsigned long  i;
   void*         *newtbl;

   newtbl = (void**)malloc(newsize * sizeof *newtbl);
   if (newtbl) {
      for (i = 0; i < newsize; i++)
         newtbl[i] = NULL;
   }
   return newtbl;
} /* maketbl */

/* 1------------------1 */

/* initialize and return a pointer to the data base */
struct hshtag *hshinit(hshfn    hash, hshfn     rehash,
                       hshcmpfn cmp,
                       hshdupfn dupe, hshfreefn undupe,
                       int      hdebug)
{
#define INITSZ 17   /* small prime, for easy testing */

   struct hshtag *master;

   if (!hash || !rehash || !cmp || !dupe || !undupe)
      master = NULL;
   else if ((master = (struct hshtag *)malloc(sizeof *master))) {
      if ((master->htbl = maketbl(INITSZ))) {
         master->currentsz = INITSZ;
         master->hash = hash; master->rehash = rehash;
         master->cmp = cmp;
         master->dupe = dupe; master->undupe = undupe;
         master->hdebug = hdebug;

         /* initialise the status portion */
         master->hstatus.probes = master->hstatus.misses = 0;
         master->hstatus.hentries = 0;
         master->hstatus.hdeleted = 0;
         master->hstatus.herror = hshOK;
         master->hstatus.version = VER;
      }
      else {
         free(master);
         master = NULL;
      }
   }
   return master;
} /* hshinit */

/* 1------------------1 */

/* destroy the data base */
void hshkill(hshtblptr master)
{
   unsigned long i;
   void         *h;                                    /*v7*/

   /* unload the actual data storage */
   if (master) {
      for (i = 0; i < master->currentsz; i++) {
         if ((h = master->htbl[i]) && (DELETED != h))  /*v7*/
            master->undupe(master->htbl[i]);
      }
      free(master->htbl);                         /* v1001 */
   }
   free(master);
} /* hshkill */

/* 1------------------1 */

/* Attempt to insert item at the hth position in the table */
/* Returns NULL if position already taken or if dupe fails */
/* (when master->herror is set to hshNOMEM)                */
static void * inserted(hshtblptr master, unsigned long h,
                       void *item,
                       int copying)  /* during reorganization */
{
   void  * hh;

   master->hstatus.probes++;         /* count total probes */
   hh = master->htbl[h];
   if (NULL == hh) {                  /* we have found a slot */
      if (copying) master->htbl[h] = item;
      else if ((master->htbl[h] = master->dupe(item)))
         /* new entry, so dupe and insert */
         master->hstatus.hentries++;          /* count 'em */
      else master->hstatus.herror |= hshNOMEM;
   }
   else if (copying) return NULL; /* no compare if copying */
   else if (DELETED == hh) return NULL;  /* nor if DELETED */
   else if (0 != master->cmp(master->htbl[h], item)) {
      /* not found here */
      return NULL;
   }
/* else found already inserted here */
   return master->htbl[h];
} /* inserted */

/* 1------------------1 */

/* insert an entry.  NULL == failure, else item */
/* This always succeeds unless memory runs out, */
/* provided that the hashtable is not full      */
static void * putintbl(hshtblptr master, void *item, int copying)
{
   unsigned long h, h2;
   void         *stored;

   h = master->hash(item) % master->currentsz;
   if (!(stored = inserted(master, h, item, copying))
       && master->hstatus.herror == hshOK) {
      h2 = master->rehash(item) % (master->currentsz >> 3) + 1;
      do {       /* we had to go past 1 per item */
         master->hstatus.misses++;
         h = (h + h2) % master->currentsz;
      } while (!(stored = inserted(master, h, item, copying))
               && (master->hstatus.herror == hshOK));
   }
   return stored;
} /* putintbl */

/* 1------------------1 */

/* Increase the table size by roughly a factor of 2    */
/* reinsert all entries from the old table in the new. */
/* revise the currentsz value to match                 */
/* free the storage for the old table.                 */
static int reorganize(hshtblptr master)
{
   void*         *newtbl;
   void*         *oldtbl;
   unsigned long  newsize, oldsize;
   unsigned long  oldentries, j;
   unsigned int   i;

   oldsize = master->currentsz;
   oldtbl =  master->htbl;
   oldentries = 0;

   if (master->hstatus.hdeleted > (master->hstatus.hentries / 4))
      /* don't expand table if we can get reasonable space  */
      /* by simply removing the accumulated DELETED entries */
      newsize = oldsize;
   else {
      newsize = ithprime(0);
      for (i = 1; newsize && (newsize <= oldsize); i++)
         newsize = ithprime(i);
   }
   if (newsize) newtbl = maketbl(newsize);
   else         newtbl = NULL;

   if (newtbl) {
      master->currentsz = newsize;
      master->htbl = newtbl;

      /* Now reinsert all old entries in new table */
      for (j = 0; j < oldsize; j++)
         if (oldtbl[j] && (oldtbl[j] != DELETED)) {
            (void) putintbl(master, oldtbl[j], 1);
            oldentries++;
         }
      /* Sanity check */
      if (oldentries != master->hstatus.hentries
                        - master->hstatus.hdeleted) {
         master->hstatus.herror |= hshINTERR;
         free(master->htbl);
         master->htbl = oldtbl;
         master->currentsz = oldsize;
         return 0;      /* failure */
      }
      else {
         master->hstatus.hentries = oldentries;
         master->hstatus.hdeleted = 0;
         free(oldtbl);
         return 1;      /* success */
      }
   }
   return 0;            /* failure */
} /* reorganize */

/* 1------------------1 */

/* insert an entry.  NULL == failure, else item */
void * hshinsert(hshtblptr master, void *item)
{
   if ((TSPACE(master) <= 0) && !reorganize(master)) {
      master->hstatus.herror |= hshTBLFULL;
      return NULL;
   }
   return putintbl(master, item, 0);
} /* hshinsert */

/* 1------------------1 */

/* Attempt to find item at the hth position in the table */
/* counting attempts.  Returns 1 if found, else 0        */
static int found(hshtblptr master, unsigned long h, void *item)
{
   void *hh;

   master->hstatus.probes++;            /* count total probes */
   hh = master->htbl[h];
   if ((hh) && (hh != DELETED))  /* NEVER cmp against DELETED */
      return !(master->cmp(hh, item));
   else return 0;
} /* found */

/* 1------------------1 */

/* Find the current hashtbl index for item, or an empty slot */
static unsigned long huntup(hshtblptr master, void *item)
{
   unsigned long h, h2;

   h = master->hash(item) % master->currentsz;

   /* Within this a DELETED item simply causes a rehash */
   /* i.e. treat it like a non-equal item               */

   if (!(found(master, h, item)) && master->htbl[h]) {
      h2 = master->rehash(item) % (master->currentsz >> 3) + 1;
      do {       /* we had to go past 1 per item */
         master->hstatus.misses++;
         h = (h + h2) % master->currentsz;
      } while (!(found(master, h, item)) && (master->htbl[h]));
   }
   return h;
} /* huntup */

/* 1------------------1 */

/* find an existing entry. NULL == notfound */
void * hshfind(hshtblptr master, void *item)
{
   unsigned long h;

   h = huntup(master, item);
   return master->htbl[h];
} /* hshfind */

/* 1------------------1 */

/* delete an existing entry. NULL == notfound      */
/* Disposal of the storage returned by hshdelete   */
/* (originally created by hshdupfn) is up to the   */
/* application. It is no longer managed by hashlib */
/* It will usually be disposable by hshfreefn().   */
void * hshdelete(hshtblptr master, void *item)
{
   unsigned long h;
   void         *olditem;

   h = huntup(master, item);
   if ((olditem = master->htbl[h])) {
      master->htbl[h] = DELETED;
      master->hstatus.hdeleted++;
   }
   return olditem;
} /* hshdelete */

/* 1------------------1 */

/* apply exec() to all entries in table. 0 = success */
/* The order of application is arbitrary.  If exec() */
/* returns non-zero (error) the walk stops           */
int    hshwalk(hshtblptr master, hshexecfn exec, void *datum)
{
   unsigned long i;
   int           err;
   void         *xtra;
   void         *hh;

   if (NULL == exec) return -1;

   if (master->hdebug) xtra = &i;
   else                xtra = NULL;

   for (i = 0; i < master->currentsz; i++) {
      hh = master->htbl[i];
      if ((hh) && (hh != DELETED))
         if ((err = exec(hh, datum, xtra)))
            return err;
   }
   return 0;
} /* hshwalk */

/* 1------------------1 */

/* return various status values */
hshstats hshstatus(hshtbl *master)
{
   return master->hstatus;
} /* hshstatus */

/* ============= Useful generic functions ============= */

/* NOTE: hash is called once per operation, while rehash is
         called _no more_ than once per operation.  Thus it
         is preferable that hash be the more efficient.
*/

/* Hash a string quantity */
unsigned long hshstrhash(const char * string)
{
   unsigned long h;

   h = 0;
   while (*string) {
      h = h * 31UL + (unsigned char) *string++;
   }
   return h;
} /* hshstrhash */

/* 1------------------1 */

/* ReHash a string quantity */
unsigned long hshstrehash(const char * string)
{
   unsigned long h;

   h = 0;
   while (*string) {
      h = h * 37UL + (unsigned char) *string++;
   }
   return h;
} /* hshstrehash */

/* -------------- File hashlib.c ------------------ */
