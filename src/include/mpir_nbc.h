/* -*- Mode: c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2010 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef MPIR_NBC_H_INCLUDED
#define MPIR_NBC_H_INCLUDED

/* This specifies the interface that must be exposed by the ADI in order to
 * support MPI-3 non-blocking collectives.  MPID_Sched_ routines are all
 * permitted to be inlines.  They are not permitted to be macros.
 *
 * Most (currently all) devices will just use the default implementation that
 * lives in "src/mpid/common/sched" */

/* The device must supply a typedef for MPID_Sched_t.  MPID_Sched_t is a handle
 * to the schedule (often a pointer under the hood), not the actual schedule.
 * This makes it easy to cheaply pass the schedule between functions.  Many
 *
 * The device must also define a constant (possibly a macro) for an invalid
 * schedule: MPID_SCHED_NULL */

/* Context/tag strategy for send/recv ops:
 * -------------------------------
 *
 * Blocking collectives were able to more or less safely separate all
 * communication between different collectives by using a fixed tag per
 * operation.  This prevents some potentially very surprising message matching
 * patterns when two different collectives are posted on the same communicator
 * in rapid succession.  But this strategy probably won't work for NBC because
 * multiple operations of any combination of types can be outstanding at the
 * same time.
 *
 * The MPI-3 draft standard says that all collective ops must be collectively
 * posted in a consistent order w.r.t. other collective operations, including
 * nonblocking collectives.  This means that we can just use a counter to assign
 * tag values that is incremented at each collective start.  We can jump through
 * some hoops to make sure that the blocking collective code is left
 * undisturbed, but it's cleaner to just update them to use the new counter
 * mechanism as well.
 */

int MPID_Sched_next_tag(int *next_tag);

/* the device must provide a typedef for MPID_Sched_t in mpidpre.h */

/* creates a new opaque schedule object and returns a handle to it in (*sp) */
int MPID_Sched_create(MPID_Sched_t *sp);
/* clones orig and returns a handle to the new schedule in (*cloned) */
int MPID_Sched_clone(MPID_Sched_t orig, MPID_Sched_t *cloned);
/* sets (*sp) to MPID_SCHED_NULL and gives you back a request pointer in (*req).
 * The caller is giving up ownership of the opaque schedule object.
 *
 * comm should be the primary (user) communicator with which this collective is
 * associated, even if other hidden communicators are used for a subset of the
 * operations.  It will be used for error handling and similar operations. */
int MPID_Sched_start(MPID_Sched_t *sp, MPID_Comm *comm, int tag, MPID_Request **req);

/* send and recv take a comm ptr to enable hierarchical collectives */
int MPID_Sched_send(void *buf, int count, MPI_Datatype datatype, int dest, MPID_Comm *comm, MPID_Sched_t s);
int MPID_Sched_recv(void *buf, int count, MPI_Datatype datatype, int src, MPID_Comm *comm, MPID_Sched_t s);

int MPID_Sched_reduce(void *inbuf, void *inoutbuf, int count, MPI_Datatype datatype, MPI_Op op, MPID_Sched_t s);
/* packing/unpacking can be accomplished by passing MPI_PACKED as either intype
 * or outtype */
int MPID_Sched_copy(void *inbuf,  int incount,  MPI_Datatype intype,
                    void *outbuf, int outcount, MPI_Datatype outtype, MPID_Sched_t s);
/* require that all previously added ops are complete before subsequent ops
 * may begin to execute */
int MPID_Sched_barrier(MPID_Sched_t s);

/* Sched_cb_t funcitons take a comm parameter, the value of which will be the
 * comm passed to Sched_start */
/* callback entries must be used judiciously, otherwise they will prevent
 * caching opportunities */
typedef int (MPID_Sched_cb_t)(MPID_Comm *comm, int tag, void *state);
/* buffer management, fancy reductions, etc */
int MPID_Sched_cb(MPID_Sched_cb_t *cb_p, void *cb_state, MPID_Sched_t s);

/* TODO: develop a caching infrastructure for use by the upper level as well,
 * hopefully s.t. uthash can be used somehow */

/* common callback utility functions */
int MPIR_Sched_cb_free_buf(MPID_Comm *comm, int tag, void *state);

#endif /* !defined(MPIR_NBC_H_INCLUDED) */