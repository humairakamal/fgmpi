/* -------------- File hashlib.h ------------------ */
#ifndef hashlib_h
#define hashlib_h

#ifdef __cplusplus
extern "C"
{
#endif

/*  Copyright (C) 2002 by C.B. Falconer

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

  I can be contacted by mailto:cbfalconer@worldnet.att.net
           or at 680 Hartford Tpk., Hamden, CT 06517, USA.

  Should you wish to incorporate this in proprietary software for
  sale, contact me and we can discuss a different license, such
  as the GNU Library license or other mutually agreeable terms.

  =========== significant revisions ============

  v 0.0.0.5 Largely due to prodding from:
      jmccarty@sun1307.ssd.usa.alcatel.com (Mike Mccarty Sr)
  I have come to the conclusion that a hshdelete function is
  implementable.  It has been added. This also requires that
  a 'hdeleted' value be added to the status.
  The table actually holds hentries-hdeleted active entries.
*/

/* This is an example of object oriented programming in C, in   */
/* that it isolates the hashtable functioning from the objects  */
/* it stores and retrieves.  It is expected to be useful in     */
/* such areas as symbol tables and Markov chains.               */

/* Note that the user need not be concerned with the table size */

/* This library is intended to be totally re-entrant and thread */
/* safe.  It can provide storage and lookup for arbitrary data  */
/* items of arbitrary types.  The hshkill() function will       */
/* release all associated storage, after which hshinit() is     */
/* needed before using the database again.                      */

/* The pointers returned by hshinsert() and hshfind() may be    */
/* used to modify the data items, PROVIDED THAT such does NOT   */
/* affect the values returned from hshcmp() or hshhash() or     */
/* hshrehash() in any way.  Note that these returned pointers   */
/* are normally NOT the same as the pointers passed in to those */
/* two functions.  This is controlled in hshdupe and hshundupe  */

/* opaque incomplete object */
typedef struct hshtag hshtbl;

/* Possible error returns, powers of 2 */
//enum hsherr {hshOK = 0, hshNOMEM, hshTBLFULL, hshINTERR = 4};
#define hshOK 0
#define hshNOMEM 1
#define hshTBLFULL 2
#define hshINTERR 4    
    

/* NOTE: probes and misses aids evaluating hash functions       */
typedef struct hshstats {
   unsigned long probes, misses,  /* usage statistics */
                 hentries,        /* current entries count */
                 hdeleted;        /* deleted entry count */
   int           herror;          /* error status */
   unsigned int  version;         /* hashlib version */
} hshstats;

/* Note: version when expressed in decimal, is of the form": */
/* M.n.v.p  where  M = Major version                         */
/*                 n = minor version                         */
/*                 v = variation                             */
/*                 p = patch                                 */
/* with all values except M being single decimal digits.     */


/* -------------- The auxiliary function types ---------------- */
/* The actual storage for the various void * item pointers is   */
/* known to the calling program, but not to this library. These */
/* function pointers allow the library to adapt itself to       */
/* arbitrary types of data.                                     */
/*                                                              */
/* When called from the hashlib system the 'item' parameter to  */
/* these functions will never be NULL.  The application is thus */
/* free to use such a value for any special purpose, such as    */
/* re-initialization of static variables.                       */

/* a hashfn() returns some hashed value from the item           */
/* Note that two functions of this form must be provided        */
/* The quality of these functions strongly affects performance  */
typedef unsigned long (*hshfn)(void *item);

/* A hshcmpfn() compares two items, and returns -ve, 0 (equal), +ve */
/* corresponding to litem < ritem, litem == ritem, litem > ritem    */
/* It need only return zero/non-zero if not to be used elsewhere    */
typedef int  (*hshcmpfn)(void *litem, void *ritem);

/* A hshdupfn() duplicates the item into malloced space.  Further   */
/* management of the duplicated space is the libraries duty.  It is */
/* only used via hshinsert(), and must return NULL for failure.     */
/* The application is free to modify fields of the allocated space, */
/* provided such modification does NOT affect hshcmpfn              */
typedef void *(*hshdupfn)(void *item);

/* A hshfreefn() reverses the action of a hshdupfn.  It is only     */
/* called during execution of the hshkill() function.  This allows  */
/* clean-up of memory malloced within the hshdupfn. After execution */
/* of hshfree the item pointer should normally not be used.  See    */
/* sort demo for an exception.                                      */
typedef void (*hshfreefn)(void *item);

/* A hshexecfn() performs some operation on a data item.  It may be */
/* passed additional data in datum.  It is only used in walking the */
/* complete stored database. It returns 0 for success.              */
/* xtra will normally be NULL, but may be used for debug purposes   */
/* During a database walk, the item parameter will never be NULL    */
typedef int (*hshexecfn)(void *item, void *datum, void *xtra);

/* ------------ END of auxiliary function types ------------- */

/* initialize and return a pointer to the data base */
hshtbl *hshinit(hshfn hash, hshfn rehash,
                hshcmpfn cmp,
                hshdupfn dupe, hshfreefn undupe,
                int      hdebug);

/* 1------------------1 */

/* destroy the data base. Accepts NULL and does nothing */
void   hshkill(hshtbl *master);

/* 1------------------1 */

/* find an existing entry. NULL == notfound */
void * hshfind(hshtbl *master, void *item);

/* 1------------------1 */

/* delete an existing entry. NULL == notfound      */
/* Disposal of the storage returned by hshdelete   */
/* (originally created by hshdupfn) is up to the   */
/* application. It is no longer managed by hashlib */
/* It will usually be disposable by hshfreefn().   */
void * hshdelete(hshtbl *master, void *item);

/* 1------------------1 */

/* insert an entry.  NULL == failure, else item */
void * hshinsert(hshtbl *master, void *item);

/* 1------------------1 */

/* apply exec to all entries in table. 0 = success */
/* The order of application is arbitrary.  If exec */
/* returns non-zero (error) the walk stops         */
/* datum can provide a global data area for exec   */
int    hshwalk(hshtbl *master, hshexecfn exec, void *datum);

/* 1------------------1 */

/* return various statistics on use of this hshtbl */
hshstats hshstatus(hshtbl *master);

/* ============= Useful generic functions ============= */

/* Hash a string quantity */
unsigned long hshstrhash(const char * string);

/* 1------------------1 */

/* ReHash a string quantity */
unsigned long hshstrehash(const char * string);

#ifdef __cplusplus
}      /* Corrected per David Titkin 2005-03-14 */
#endif
#endif
/* -------------- File hashlib.h ------------------ */
