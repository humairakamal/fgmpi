/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2014 FG-MPI: Fine-Grain MPI.
 *      See copyright in top-level directory.
 */

#ifndef FGMPIUTIL_H_INCLUDED
#define FGMPIUTIL_H_INCLUDED

#include <mpiimpl.h>

#if defined(FINEGRAIN_MPI)

int FG_is_within_same_HWP(int reqfgrank, MPID_Comm *comm, int *reqrankpid);
int FG_Yield_on_incomplete_request(MPID_Request *req);
int FG_set_PROC_NULL(int *worldrank_ptr, int *pid_ptr);

#endif /* #if defined(FINEGRAIN_MPI) */

#endif /* FGMPIUTIL_H_INCLUDED */
