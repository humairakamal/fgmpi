/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

/* -- Begin Profiling Symbol Block for routine MPI_Comm_compare */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Comm_compare = PMPI_Comm_compare
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Comm_compare  MPI_Comm_compare
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Comm_compare as PMPI_Comm_compare
#elif defined(HAVE_WEAK_ATTRIBUTE)
int MPI_Comm_compare(MPI_Comm comm1, MPI_Comm comm2, int *result) __attribute__((weak,alias("PMPI_Comm_compare")));
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#undef MPI_Comm_compare
#define MPI_Comm_compare PMPI_Comm_compare

#endif

#undef FUNCNAME
#define FUNCNAME MPI_Comm_compare
#undef FCNAME
#define FCNAME "MPI_Comm_compare"

/*@

MPI_Comm_compare - Compares two communicators

Input Parameters:
+ comm1 - comm1 (handle) 
- comm2 - comm2 (handle) 

Output Parameters:
. result - integer which is 'MPI_IDENT' if the contexts and groups are the
same, 'MPI_CONGRUENT' if different contexts but identical groups, 'MPI_SIMILAR'
if different contexts but similar groups, and 'MPI_UNEQUAL' otherwise

Using 'MPI_COMM_NULL' with 'MPI_Comm_compare':

It is an error to use 'MPI_COMM_NULL' as one of the arguments to
'MPI_Comm_compare'.  The relevant sections of the MPI standard are 

$(2.4.1 Opaque Objects)
A null handle argument is an erroneous 'IN' argument in MPI calls, unless an
exception is explicitly stated in the text that defines the function.

$(5.4.1. Communicator Accessors)
where there is no text in 'MPI_COMM_COMPARE' allowing a null handle.

.N ThreadSafe
(To perform the communicator comparisions, this routine may need to
allocate some memory.  Memory allocation is not interrupt-safe, and hence
this routine is only thread-safe.)

.N Fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_ARG
@*/
int MPI_Comm_compare(MPI_Comm comm1, MPI_Comm comm2, int *result)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm *comm_ptr1 = NULL;
    MPID_Comm *comm_ptr2 = NULL;
    MPID_MPI_STATE_DECL(MPID_STATE_MPI_COMM_COMPARE);

    MPIR_ERRTEST_INITIALIZED_ORDIE();
    
    MPID_THREAD_CS_ENTER(GLOBAL, MPIR_THREAD_GLOBAL_ALLFUNC_MUTEX);
    MPID_MPI_FUNC_ENTER(MPID_STATE_MPI_COMM_COMPARE);

#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
	    MPIR_ERRTEST_COMM(comm1, mpi_errno);
	    MPIR_ERRTEST_COMM(comm2, mpi_errno);
	}
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */
    
    /* Get handles to MPI objects. */
    MPID_Comm_get_ptr( comm1, comm_ptr1 );
    MPID_Comm_get_ptr( comm2, comm_ptr2 );

    /* Validate parameters and objects (post conversion) */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
            /* Validate comm_ptr */
            MPID_Comm_valid_ptr( comm_ptr1, mpi_errno, TRUE );
            if (mpi_errno) goto fn_fail;
            MPID_Comm_valid_ptr( comm_ptr2, mpi_errno, TRUE );
            if (mpi_errno) goto fn_fail;
	    MPIR_ERRTEST_ARGNULL( result, "result", mpi_errno );
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ...  */
    if (comm_ptr1->comm_kind != comm_ptr2->comm_kind) {
	*result = MPI_UNEQUAL;
    }
    else if (comm1 == comm2) {
	*result = MPI_IDENT;
    }
    else if (comm_ptr1->comm_kind == MPID_INTRACOMM) {
	MPID_Group *group_ptr1, *group_ptr2;

        mpi_errno = MPIR_Comm_group_impl(comm_ptr1, &group_ptr1);
        if (mpi_errno) MPIR_ERR_POP(mpi_errno);
        mpi_errno = MPIR_Comm_group_impl(comm_ptr2, &group_ptr2);
        if (mpi_errno) MPIR_ERR_POP(mpi_errno);
        mpi_errno = MPIR_Group_compare_impl(group_ptr1, group_ptr2, result);
        if (mpi_errno) MPIR_ERR_POP(mpi_errno);
	/* If the groups are the same but the contexts are different, then
	   the communicators are congruent */
	if (*result == MPI_IDENT)
	    *result = MPI_CONGRUENT;
	mpi_errno = MPIR_Group_free_impl(group_ptr1);
        if (mpi_errno) MPIR_ERR_POP(mpi_errno);
	mpi_errno = MPIR_Group_free_impl(group_ptr2);
        if (mpi_errno) MPIR_ERR_POP(mpi_errno);
    }
    else { 
	/* INTER_COMM */
	int       lresult, rresult;
	MPID_Group *group_ptr1, *group_ptr2;
	MPID_Group *rgroup_ptr1, *rgroup_ptr2;
	
	/* Get the groups and see what their relationship is */
	mpi_errno = MPIR_Comm_group_impl(comm_ptr1, &group_ptr1);
        if (mpi_errno) MPIR_ERR_POP(mpi_errno);
	mpi_errno = MPIR_Comm_group_impl(comm_ptr2, &group_ptr2);
        if (mpi_errno) MPIR_ERR_POP(mpi_errno);
        mpi_errno = MPIR_Group_compare_impl(group_ptr1, group_ptr2, &lresult);
        if (mpi_errno) MPIR_ERR_POP(mpi_errno);

	mpi_errno = MPIR_Comm_remote_group_impl(comm_ptr1, &rgroup_ptr1);
        if (mpi_errno) MPIR_ERR_POP(mpi_errno);
	mpi_errno = MPIR_Comm_remote_group_impl(comm_ptr2, &rgroup_ptr2);
        if (mpi_errno) MPIR_ERR_POP(mpi_errno);
        mpi_errno = MPIR_Group_compare_impl(rgroup_ptr1, rgroup_ptr2, &rresult);
        if (mpi_errno) MPIR_ERR_POP(mpi_errno);

	/* Choose the result that is "least" strong. This works 
	   due to the ordering of result types in mpi.h */
	(*result) = (rresult > lresult) ? rresult : lresult;

	/* They can't be identical since they're not the same
	   handle, they are congruent instead */
	if ((*result) == MPI_IDENT)
	  (*result) = MPI_CONGRUENT;

	/* Free the groups */
	mpi_errno = MPIR_Group_free_impl(group_ptr1);
        if (mpi_errno) MPIR_ERR_POP(mpi_errno);
	mpi_errno = MPIR_Group_free_impl(group_ptr2);
        if (mpi_errno) MPIR_ERR_POP(mpi_errno);
	mpi_errno = MPIR_Group_free_impl(rgroup_ptr1);
        if (mpi_errno) MPIR_ERR_POP(mpi_errno);
	mpi_errno = MPIR_Group_free_impl(rgroup_ptr2);
        if (mpi_errno) MPIR_ERR_POP(mpi_errno);
    }
    /* ... end of body of routine ... */

  fn_exit:
    MPID_MPI_FUNC_EXIT(MPID_STATE_MPI_COMM_COMPARE);
    MPID_THREAD_CS_EXIT(GLOBAL, MPIR_THREAD_GLOBAL_ALLFUNC_MUTEX);
    return mpi_errno;
    
    /* --BEGIN ERROR HANDLING-- */
  fn_fail:
#   ifdef HAVE_ERROR_CHECKING
    {
	mpi_errno = MPIR_Err_create_code(
	    mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER,
	    "**mpi_comm_compare",
	    "**mpi_comm_compare %C %C %p", comm1, comm2, result);
    }
    /* Use whichever communicator is non-null if possible */
#   endif
    mpi_errno = MPIR_Err_return_comm( comm_ptr1 ? comm_ptr1 : comm_ptr2, 
				      FCNAME, mpi_errno );
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


#if defined(FINEGRAIN_MPI)
#include "mpidimpl.h"

/* -- Begin Profiling Symbol Block for routine MPIX_Comm_translate_ranks */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPIX_Comm_translate_ranks = PMPIX_Comm_translate_ranks
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPIX_Comm_translate_ranks  MPIX_Comm_translate_ranks
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPIX_Comm_translate_ranks as PMPIX_Comm_translate_ranks
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#undef MPIX_Comm_translate_ranks
#define MPIX_Comm_translate_ranks PMPIX_Comm_translate_ranks
#endif

#undef FUNCNAME
#define FUNCNAME MPIX_Comm_translate_ranks
#undef FCNAME
#define FCNAME "MPIX_Comm_translate_ranks"
/*@
 MPIX_Comm_translate_ranks - Translates the ranks of processes in one communicator to
                             those in another communicator

  Input Parameters:
+ comm1 - comm1 (handle)
. n - number of ranks in  'ranks1' and 'ranks2'  arrays (integer)
. ranks1 - array of zero or more valid ranks in 'comm1'
- comm2 - comm2 (handle)

  Output Parameter:
. ranks2 - array of corresponding ranks in comm2,  'MPI_UNDEFINED'  when no
  correspondence exists.

  As a special case, if the input rank is
  'MPI_PROC_NULL', 'MPI_PROC_NULL' is given as the output rank.

.N Fortran

.N Errors
.N MPI_SUCCESS
@*/
int MPIX_Comm_translate_ranks(MPI_Comm comm1, int n, int *ranks1,
			      MPI_Comm comm2, int *ranks2)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm *comm_ptr1 = NULL;
    MPID_Comm *comm_ptr2 = NULL;
    int i, j;
    int worldrank1, worldrank2;
    MPID_MPI_STATE_DECL(MPID_STATE_MPIX_COMM_TRANSLATE_RANKS);

    MPIR_ERRTEST_INITIALIZED_ORDIE();

    MPID_MPI_FUNC_ENTER(MPID_STATE_MPIX_COMM_TRANSLATE_RANKS);

    /* Validate parameters, especially handles needing to be converted */
    #   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
	    MPIR_ERRTEST_COMM(comm1, mpi_errno);
	    MPIR_ERRTEST_COMM(comm2, mpi_errno);
            if (mpi_errno) goto fn_fail;
	}
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* Get handles to MPI objects. */
    MPID_Comm_get_ptr( comm1, comm_ptr1 );
    MPID_Comm_get_ptr( comm2, comm_ptr2 );

    /* Validate parameters and objects (post conversion) */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
            /* Validate comm_ptr */
            MPID_Comm_valid_ptr( comm_ptr1, mpi_errno, TRUE );
            MPID_Comm_valid_ptr( comm_ptr2, mpi_errno, TRUE );
	    /* If comm_ptr1 or 2 is not valid, it will be reset to null */

	    MPIR_ERRTEST_ARGNEG(n,"n",mpi_errno);
	    if (comm_ptr1) {
		/* Check that the rank entries are valid */
		int size1 = comm_ptr1->local_size;
		for (i=0; i<n; i++) {
		    if ( (ranks1[i] < 0 && ranks1[i] != MPI_PROC_NULL) ||
                         ranks1[i] >= size1) {
			mpi_errno = MPIR_Err_create_code( MPI_SUCCESS,
                                                          MPIR_ERR_RECOVERABLE, FCNAME, __LINE__,
							  MPI_ERR_RANK,
                                                          "**rank", "**rank %d %d",
                                                          ranks1[i], size1 );
			break;
		    }
		}
	    }
            if (mpi_errno) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ...  */

    /* Initialize the output ranks */
    for (i=0; i<n; i++) {
	ranks2[i] = MPI_UNDEFINED;
    }

    /* FG: TODO optimize for the special case of comm2 is
       a dup of MPI_COMM_WORLD, or more generally, has lrank == worldrank
       for everything within the size of comm2.  */
    for (i=0; i<n; i++) {
        if (ranks1[i] == MPI_PROC_NULL) {
            ranks2[i] = MPI_PROC_NULL;
            continue;
        }
        MPIDI_Comm_get_pid_worldrank(comm_ptr1, ranks1[i], NULL, &worldrank1);
        for (j=0; j<(comm_ptr2->local_size); j++) {
            MPIDI_Comm_get_pid_worldrank(comm_ptr2, j, NULL, &worldrank2);
            if (worldrank2 == worldrank1){
                ranks2[i] = j;
                break;
            }
        }
    }

    /* ... end of body of routine ... */

#ifdef HAVE_ERROR_CHECKING
  fn_exit:
#endif
    MPID_MPI_FUNC_EXIT(MPID_STATE_MPIX_COMM_TRANSLATE_RANKS);
    return mpi_errno;

    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_CHECKING
  fn_fail:
    {
	mpi_errno = MPIR_Err_create_code(
	    mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER,
	    "**mpix_comm_translate_ranks",
	    "**mpix_comm_translate_ranks %C %d %p %C %p",
	    comm1, n, ranks1, comm2, ranks2);
    }
    mpi_errno = MPIR_Err_return_comm( NULL, FCNAME, mpi_errno );
    goto fn_exit;
#   endif
    /* --END ERROR HANDLING-- */
}

#endif /* matches #if defined(FINEGRAIN_MPI) */
