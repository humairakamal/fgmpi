/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 110 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

/* -- Begin Profiling Symbol Block for routine MPIX_Iallgatherv */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPIX_Iallgatherv = PMPIX_Iallgatherv
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPIX_Iallgatherv  MPIX_Iallgatherv
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPIX_Iallgatherv as PMPIX_Iallgatherv
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#undef MPIX_Iallgatherv
#define MPIX_Iallgatherv PMPIX_Iallgatherv

/* any non-MPI functions go here, especially non-static ones */

#undef FUNCNAME
#define FUNCNAME MPIR_Iallgatherv_impl
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Iallgatherv_impl(void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int *recvcounts, int *displs, MPI_Datatype recvtype, MPID_Comm *comm_ptr, MPI_Request *request)
{
    int mpi_errno = MPI_SUCCESS;

    if (comm_ptr->coll_fns != NULL && comm_ptr->coll_fns->Iallgatherv != NULL) {
        mpi_errno = comm_ptr->coll_fns->Iallgatherv(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm_ptr, request);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    }
    else {
        MPID_Abort(comm_ptr, MPI_ERR_OTHER, 1, "default version of MPIX_Iallgatherv not yet implemented");
        MPIU_Assertp(0); /* never get here */
        /* TODO implement the following functions and uncomment this code: */
#if 0
        if (comm_ptr->comm_kind == MPID_INTRACOMM) {
            /* intracommunicator */
            mpi_errno = MPIR_Iallgatherv_intra(sendbuf, recvbuf, recvcount, datatype, op, comm_ptr);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        }
        else {
            /* intercommunicator */
            mpi_errno = MPIR_Iallgatherv_inter(sendbuf, recvbuf, recvcount, datatype, op, comm_ptr);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        }
#endif
    }

fn_exit:
    return mpi_errno;
fn_fail:
    goto fn_exit;
}

#endif /* MPICH_MPI_FROM_PMPI */

#undef FUNCNAME
#define FUNCNAME MPI_Allreduce
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
/*@
MPIX_Iallgatherv - XXX description here

Input Parameters:
+ sendbuf - starting address of the send buffer (choice)
. sendcount - number of elements in send buffer (non-negative integer)
. sendtype - data type of send buffer elements (handle)
. recvcounts - non-negative integer array (of length group size) containing the number of elements that are received from each process
. displs - integer array (of length group size). Entry i specifies the displacement relative to recvbuf at which to place the incoming data from process i
. recvtype - data type of receive buffer elements (handle)
- comm - communicator (handle)

Output Parameters:
+ recvbuf - starting address of the receive buffer (choice)
- request - communication request (handle)

.N ThreadSafe

.N Fortran

.N Errors
@*/
int MPIX_Iallgatherv(void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int *recvcounts, int *displs, MPI_Datatype recvtype, MPI_Comm comm, MPI_Request *request)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm *comm_ptr = NULL;
    MPID_MPI_STATE_DECL(MPID_STATE_MPIX_IALLGATHERV);

    MPIU_THREAD_CS_ENTER(ALLFUNC,);
    MPID_MPI_FUNC_ENTER(MPID_STATE_MPIX_IALLGATHERV);

    /* Validate parameters, especially handles needing to be converted */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS
        {
            MPIR_ERRTEST_DATATYPE(sendtype, "sendtype", mpi_errno);
            MPIR_ERRTEST_DATATYPE(recvtype, "recvtype", mpi_errno);
            MPIR_ERRTEST_COMM(comm, mpi_errno);

            /* TODO more checks may be appropriate */
            if (mpi_errno != MPI_SUCCESS) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* Convert MPI object handles to object pointers */
    MPID_Comm_get_ptr(comm, comm_ptr);

    /* Validate parameters and objects (post conversion) */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS
        {
            if (HANDLE_GET_KIND(sendtype) != HANDLE_KIND_BUILTIN) {
                MPID_Datatype *sendtype_ptr = NULL;
                MPID_Datatype_get_ptr(sendtype, sendtype_ptr);
                MPID_Datatype_valid_ptr(sendtype_ptr, mpi_errno);
                MPID_Datatype_committed_ptr(sendtype_ptr, mpi_errno);
            }

            MPIR_ERRTEST_ARGNULL(recvcounts,"recvcounts", mpi_errno);
            MPIR_ERRTEST_ARGNULL(displs,"displs", mpi_errno);
            if (HANDLE_GET_KIND(recvtype) != HANDLE_KIND_BUILTIN) {
                MPID_Datatype *recvtype_ptr = NULL;
                MPID_Datatype_get_ptr(recvtype, recvtype_ptr);
                MPID_Datatype_valid_ptr(recvtype_ptr, mpi_errno);
                MPID_Datatype_committed_ptr(recvtype_ptr, mpi_errno);
            }

            MPID_Comm_valid_ptr(comm_ptr, mpi_errno);
            MPIR_ERRTEST_ARGNULL(request,"request", mpi_errno);
            /* TODO more checks may be appropriate (counts, in_place, buffer aliasing, etc) */
            if (mpi_errno != MPI_SUCCESS) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ...  */

    mpi_errno = MPIR_Iallgatherv_impl(sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm_ptr, request);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    /* ... end of body of routine ... */

fn_exit:
    MPID_MPI_FUNC_EXIT(MPID_STATE_MPIX_IALLGATHERV);
    MPIU_THREAD_CS_EXIT(ALLFUNC,);
    return mpi_errno;

fn_fail:
    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_CHECKING
    {
        mpi_errno = MPIR_Err_create_code(
            mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER,
            "**mpix_iallgatherv", "**mpix_iallgatherv %p %d %D %p %p %p %D %C %p", sendbuf, sendcount, sendtype, recvbuf, recvcounts, displs, recvtype, comm, request);
    }
#   endif
    mpi_errno = MPIR_Err_return_comm(comm_ptr, FCNAME, mpi_errno);
    goto fn_exit;
    /* --END ERROR HANDLING-- */
    goto fn_exit;
}