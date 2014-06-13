GENERAL (modified 2002-03-12)
=======

Hashlib implements a generalized hash storage data base package
which has O(1) storage and retrieval performance.  It removes
all the effort of implementation and storage management, and
only requires that a few application specific routines be
provided.  It can report statistics and is thus also an
excellent test bed for checking hash function performance.

The complete package can be found at:

   <http://cbfalconer.home.att.net/download/hashlib.zip>
   
Bug reports to <mailto:cbfalconer@worldnet.att.net>   

I have removed this from Beta status, and marked hashlib.c as
being version 1.0.0.0.  There have been no bug reports in over
six months.  All code, except the detection of keyboard input 
in the demo wdfreq.c, is ISO standard C.

For general use in other applications, see the notes in file
hashlib.h.  These describe all the entry points and auxiliary
routines needed.

This package should be ready to use.  The hashlib module should
compile on any system, leaving a suitable hashlib.o for use in
any required project.

Under DJGPP, or CYGWIN, or Linux, the only command needed
should be:

   make hashlib
   
Alternatively you can execute the full set of regression tests
by executing either runtests (under linux/cygwin)   
or                  runtests.bat (under DJGPP)

Note that the runtests script will also function under DJGPP if
executed by bash.

In either case the package will be completely rebuilt from the
source and verified.

cokusmt.c (.h) is included purely to ensure that regression
tests will function on any system.

markov.c is a demonstration of a fairly complex use of the
hashlib library.  Kernighan and Pikes "The Practice of 
Programming" gives another implementation with somewhat
different characteristics.

wdfreq.c is a demonstration of forming a linked list from the
content of a filled hashtable, and then sorting that list. 
This has been added in the release of version 1.0.0.0, and is
extensively commented.  The techniques used can enable dumping
and reloading of hash databases to/from external files.

Note that the xref.exe included is ONLY for use under DOS or
Windows.
   
2002-10-18 cbf (bug)
====================

I think I have uncovered a bug in the hshkill function 
(in hshlib.c), which now reads as follows:

/* destroy the data base */
void hshkill(hshtblptr master)
{
   unsigned long i;

   /* unload the actual data storage */
   if (master)
      for (i = 0; i < master->currentsz; i++) {
         if (master->htbl[i])
            master->undupe(master->htbl[i]);
      }
   free(master);
} /* hshkill */

It should be:

/* destroy the data base */
void hshkill(hshtblptr master)
{
   unsigned long i;
   void         *h;  /* add line */

   /* unload the actual data storage */
   if (master)
      for (i = 0; i < master->currentsz; i++) {
         if ((h = master->htbl[i]) && (DELETED != h))      /**/
            master->undupe(h);  /* revise this and prev line */
      }
   free(master);
} /* hshkill */

because the control of storage for deleted items has been
returned to the user at deletion.

If you make this change also revise VER from 6 to 7 (line 59)
(done in this issue)

In addition, the markov.c source in the zip file did not have
a "#include <string.h>" line, although my working source did!

2004-11-04 (documentation)
==========================

Added hshusage.txt to the package.  This is a manual on the
use of the hashlib package, in more or less narrative form.
