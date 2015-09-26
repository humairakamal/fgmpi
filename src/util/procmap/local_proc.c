/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#include "mpidimpl.h" /* FIXME this is including a ch3 include file!
                         Implement these functions at the device level. */

#if defined(HAVE_LIMITS_H)
#include <limits.h>
#endif
#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif
#if defined(HAVE_ERRNO_H)
#include <errno.h>
#endif

/* MPIU_Find_local_and_external -- from the list of processes in comm,
   builds a list of local processes, i.e., processes on this same
   node, and a list of external processes, i.e., one process from each
   node.

   Note that this will not work correctly for spawned or attached
   processes.

   external processes: For a communicator, there is one external
                       process per node.  You can think of this as the
                       root or master process for that node.

   OUT:
     local_size_p      - number of processes on this node
     local_rank_p      - rank of this processes among local processes
     local_ranks_p     - (*local_ranks_p)[i] = the rank in comm
                         of the process with local rank i.
                         This is of size (*local_size_p)
     external_size_p   - number of external processes
     external_rank_p   - rank of this process among the external
                         processes, or -1 if this process is not external
     external_ranks_p  - (*external_ranks_p)[i] = the rank in comm
                         of the process with external rank i.
                         This is of size (*external_size_p)
     intranode_table_p - (*internode_table_p)[i] gives the rank in
                         *local_ranks_p of rank i in comm or -1 if not
                         applicable.  It is of size comm->remote_size.
     internode_table_p - (*internode_table_p)[i] gives the rank in
                         *external_ranks_p of the root of the node
                         containing rank i in comm.  It is of size
                         comm->remote_size.
*/
#undef FUNCNAME
#define FUNCNAME MPIU_Find_local_and_external
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)

#if defined(MPID_USE_NODE_IDS)
int MPIU_Find_local_and_external(MPID_Comm *comm, int *local_size_p, int *local_rank_p, int **local_ranks_p,
                                 int *external_size_p, int *external_rank_p, int **external_ranks_p,
                                 int **intranode_table_p, int **internode_table_p)
{
    int mpi_errno = MPI_SUCCESS;
    int *nodes;
    int external_size;
    int external_rank;
    int *external_ranks;
    int local_size;
    int local_rank;
    int *local_ranks;
    int *internode_table;
    int *intranode_table;
    int i;
    MPID_Node_id_t max_node_id;
    MPID_Node_id_t node_id;
    MPID_Node_id_t my_node_id;
    MPIU_CHKLMEM_DECL(1);
    MPIU_CHKPMEM_DECL(4);

    /* Scan through the list of processes in comm and add one
       process from each node to the list of "external" processes.  We
       add the first process we find from each node.  nodes[] is an
       array where we keep track of whether we have already added that
       node to the list. */
    
    /* these two will be realloc'ed later to the appropriate size (currently unknown) */
    /* FIXME: realloc doesn't guarantee that the allocated area will be 
       shrunk - so using realloc is not an appropriate strategy. */
    MPIU_CHKPMEM_MALLOC (external_ranks, int *, sizeof(int) * comm->remote_size, mpi_errno, "external_ranks");
    MPIU_CHKPMEM_MALLOC (local_ranks, int *, sizeof(int) * comm->remote_size, mpi_errno, "local_ranks");

    MPIU_CHKPMEM_MALLOC (internode_table, int *, sizeof(int) * comm->remote_size, mpi_errno, "internode_table");
    MPIU_CHKPMEM_MALLOC (intranode_table, int *, sizeof(int) * comm->remote_size, mpi_errno, "intranode_table");

    mpi_errno = MPID_Get_max_node_id(comm, &max_node_id);
    if (mpi_errno) MPIU_ERR_POP (mpi_errno);
    MPIU_Assert(max_node_id >= 0);
    MPIU_CHKLMEM_MALLOC (nodes, int *, sizeof(int) * (max_node_id + 1), mpi_errno, "nodes");

    /* nodes maps node_id to rank in external_ranks of leader for that node */
    for (i = 0; i < (max_node_id + 1); ++i)
        nodes[i] = -1;

    for (i = 0; i < comm->remote_size; ++i)
        intranode_table[i] = -1;
    
    external_size = 0;

    mpi_errno = MPID_Get_node_id(comm, comm->rank, &my_node_id);
    if (mpi_errno) MPIU_ERR_POP (mpi_errno);
    MPIU_Assert(my_node_id >= 0);
    MPIU_Assert(my_node_id <= max_node_id);

    local_size = 0;
    local_rank = -1;
    external_rank = -1;
    
    for (i = 0; i < comm->remote_size; ++i)
    {
        mpi_errno = MPID_Get_node_id(comm, i, &node_id);
        if (mpi_errno) MPIU_ERR_POP (mpi_errno);

        /* The upper level can catch this non-fatal error and should be
           able to recover gracefully. */
        MPIU_ERR_CHKANDJUMP(node_id < 0, mpi_errno, MPI_ERR_OTHER, "**dynamic_node_ids");

        MPIU_Assert(node_id <= max_node_id);

        /* build list of external processes */
        if (nodes[node_id] == -1)
        {
            if (i == comm->rank)
                external_rank = external_size;
            nodes[node_id] = external_size;
            external_ranks[external_size] = i;
            ++external_size;
        }

        /* build the map from rank in comm to rank in external_ranks */
        internode_table[i] = nodes[node_id];

        /* build list of local processes */
        if (node_id == my_node_id)
        {
             if (i == comm->rank)
                 local_rank = local_size;

             intranode_table[i] = local_size;
             local_ranks[local_size] = i;
             ++local_size;
        }
    }

    /*
    printf("------------------------------------------------------------------------\n");
    printf("comm = %p\n", comm);
    printf("comm->size = %d\n", comm->remote_size); 
    printf("comm->rank = %d\n", comm->rank); 
    printf("local_size = %d\n", local_size); 
    printf("local_rank = %d\n", local_rank); 
    printf("local_ranks = %p\n", local_ranks);
    for (i = 0; i < local_size; ++i) 
        printf("  local_ranks[%d] = %d\n", i, local_ranks[i]); 
    printf("external_size = %d\n", external_size); 
    printf("external_rank = %d\n", external_rank); 
    printf("external_ranks = %p\n", external_ranks);
    for (i = 0; i < external_size; ++i) 
        printf("  external_ranks[%d] = %d\n", i, external_ranks[i]); 
    printf("intranode_table = %p\n", intranode_table);
    for (i = 0; i < comm->remote_size; ++i) 
        printf("  intranode_table[%d] = %d\n", i, intranode_table[i]); 
    printf("internode_table = %p\n", internode_table);
    for (i = 0; i < comm->remote_size; ++i) 
        printf("  internode_table[%d] = %d\n", i, internode_table[i]); 
    printf("nodes = %p\n", nodes);
    for (i = 0; i < (max_node_id + 1); ++i) 
        printf("  nodes[%d] = %d\n", i, nodes[i]); 
     */

    *local_size_p = local_size;
    *local_rank_p = local_rank;
    *local_ranks_p =  MPIU_Realloc (local_ranks, sizeof(int) * local_size);
    MPIU_ERR_CHKANDJUMP (*local_ranks_p == NULL, mpi_errno, MPI_ERR_OTHER, "**nomem2");

    *external_size_p = external_size;
    *external_rank_p = external_rank;
    *external_ranks_p = MPIU_Realloc (external_ranks, sizeof(int) * external_size);
    MPIU_ERR_CHKANDJUMP (*external_ranks_p == NULL, mpi_errno, MPI_ERR_OTHER, "**nomem2");

    /* no need to realloc */
    if (intranode_table_p)
        *intranode_table_p = intranode_table;
    if (internode_table_p)
        *internode_table_p = internode_table;
    
    MPIU_CHKPMEM_COMMIT();

 fn_exit:
    MPIU_CHKLMEM_FREEALL();
    return mpi_errno;
 fn_fail:
    MPIU_CHKPMEM_REAP();
    goto fn_exit;
}

#if defined(FINEGRAIN_MPI)
#undef FUNCNAME
#define FUNCNAME MPIU_Nested_maps_for_communicators
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIU_Nested_maps_for_communicators(MPID_Comm *comm)
{
    int mpi_errno = MPI_SUCCESS;
    int *nodes;
    int external_size;
    int local_size;
    int fg_local_size;
    int *osprocs;
    int i;
    MPID_Node_id_t max_node_id;
    MPID_Node_id_t node_id;
    MPID_Node_id_t my_node_id;
    int pid, worldrank, mypid;
    int parentcomm_rank = -1;
    MPIU_CHKLMEM_DECL(2);

#if defined(COMM_COMMIT_USES_UTHASH)
    ptn_comm_tables_hash_t *parent_to_nested_hash_p = NULL;
    Nested_comm_rtwmap_hash_t *internode_rtw_hash_p = NULL;
    Nested_comm_rtwmap_hash_t *intranode_rtw_hash_p = NULL;
    Nested_comm_rtwmap_hash_t *intra_osproc_rtw_hash_p = NULL;
#else /* hashlib.h */
    ptn_comm_tables_hash_t *parent_to_nested_hash_p = ptnLookupHashCreate();
    Nested_comm_rtwmap_hash_t *internode_rtw_hash_p = RTWhashCreate(0);
    Nested_comm_rtwmap_hash_t *intranode_rtw_hash_p = RTWhashCreate(0);
    Nested_comm_rtwmap_hash_t *intra_osproc_rtw_hash_p = RTWhashCreate(0);
#endif

    /* Scan through the list of processes in comm and add one
       process from each node to the list of "external" processes.  We
       add the first process we find from each node.  nodes[] is an
       array where we keep track of whether we have already added that
       node to the list. */

    mpi_errno = MPID_Get_max_node_id(comm, &max_node_id);
    if (mpi_errno) MPIU_ERR_POP (mpi_errno);
    MPIU_Assert(max_node_id >= 0);
    MPIU_CHKLMEM_MALLOC (nodes, int *, sizeof(int) * (max_node_id + 1), mpi_errno, "nodes");
    MPIU_CHKLMEM_MALLOC (osprocs, int *, sizeof(int) * (comm->local_size + 1), mpi_errno, "osprocs");
    /* nodes[] maps a process's node_id to internode_comm_root for its node */
    for (i = 0; i < (max_node_id + 1); ++i) {
        nodes[i] = -1;
    }
    /* osprocs[] maps a process's process_id to intranode_comm_local_rank for its os-process */
    for (i = 0; i < (comm->local_size + 1); ++i) {
        osprocs[i] = -1;
    }

    external_size = 0;
    fg_local_size = 0;
    local_size = 0;
    mypid = -1;
    worldrank = -1;
    MPIDI_Comm_get_pid_worldrank(comm, comm->rank, &mypid, &worldrank);
    mpi_errno = MPID_Get_node_id(comm, mypid, &my_node_id);

    if (mpi_errno) MPIU_ERR_POP (mpi_errno);
    MPIU_Assert(my_node_id >= 0);
    MPIU_Assert(my_node_id <= max_node_id);

    for (i = 0; i < comm->totprocs; ++i)
    {
        Parent_to_Nested_comm_tables_t parent_to_nested_entry;
        parentcomm_rank = i;
        pid = -1;
        MPIDI_Comm_get_pid_worldrank(comm, i, &pid, &worldrank);

        if (mypid == pid) {
            parent_to_nested_entry.intra_osproc_fg_rank = fg_local_size;
            RTW_HASH_INSERT(intra_osproc_rtw_hash_p, fg_local_size, worldrank);
            ++fg_local_size;
        } else {
            parent_to_nested_entry.intra_osproc_fg_rank = -1;
        }

        mpi_errno = MPID_Get_node_id(comm, pid, &node_id);
        if (mpi_errno) MPIU_ERR_POP (mpi_errno);

        /* The upper level can catch this non-fatal error and should be
           able to recover gracefully. */
        MPIU_ERR_CHKANDJUMP(node_id < 0, mpi_errno, MPI_ERR_OTHER, "**dynamic_node_ids");

        MPIU_Assert(node_id <= max_node_id);

        /* build list of external processes */
        if (nodes[node_id] == -1)
        {
            parent_to_nested_entry.internode_comm_external_rank = external_size;
            RTW_HASH_INSERT(internode_rtw_hash_p, external_size, worldrank);
            nodes[node_id] = external_size;
            ++external_size;
        } else {
            parent_to_nested_entry.internode_comm_external_rank = -1;
        }
        parent_to_nested_entry.internode_comm_root = nodes[node_id];

        /* build list of local processes */
        if (node_id == my_node_id)
        {
            if ( -1 == osprocs[pid] ) {
                parent_to_nested_entry.intranode_comm_local_rank = local_size;
                RTW_HASH_INSERT( intranode_rtw_hash_p, local_size, worldrank);
                osprocs[pid] = local_size;
                ++local_size;
            } else {
                parent_to_nested_entry.intranode_comm_local_rank = -1; /* this rank is not part of intranode comm */
            }
        } else {
            parent_to_nested_entry.intranode_comm_local_rank = -1; /* this rank is not part of intranode comm */
        }

        parent_to_nested_entry.intranode_comm_root = osprocs[pid]; /* leader of colocated processes inside my OS-process and -1 (as per initialization of osprocs[]) if this process is not on my node */
        PTN_HASH_INSERT( parent_to_nested_hash_p, parentcomm_rank, parent_to_nested_entry);
    }

    comm->co_shared_vars->nested_uniform_vars.fg_local_size = fg_local_size;
    comm->co_shared_vars->nested_uniform_vars.local_size = local_size;
    comm->co_shared_vars->nested_uniform_vars.external_size = external_size;

    comm->co_shared_vars->nested_uniform_vars.internode_rtw_hash = internode_rtw_hash_p;
    comm->co_shared_vars->nested_uniform_vars.intranode_rtw_hash = intranode_rtw_hash_p;
    comm->co_shared_vars->nested_uniform_vars.intra_osproc_rtw_hash = intra_osproc_rtw_hash_p;

    comm->co_shared_vars->ptn_hash = parent_to_nested_hash_p;

    /*
      for (i = 0; i < comm->totprocs; ++i)
      {
          Parent_to_Nested_comm_tables_coshared_hash_t *ptn_tables_hash_entry_stored = NULL;
          PTN_HASH_LOOKUP( parent_to_nested_hash_p, i, ptn_tables_hash_entry_stored );
          MPIU_Assert( ptn_tables_hash_entry_stored != NULL);

          printf("my_fgrank=%d:parent_comm_rank=%d:\n\tfg_local_size=%d\n\tintra_osproc_fg_rank=%d\n", comm->rank, i, fg_local_size, ptn_tables_hash_entry_stored->parent_to_nested.intra_osproc_fg_rank);

          printf("\tlocal_size=%d\n\tintranode_comm_local_rank=%d\n\tintranode_comm_root=%d\n", local_size, ptn_tables_hash_entry_stored->parent_to_nested.intranode_comm_local_rank, ptn_tables_hash_entry_stored->parent_to_nested.intranode_comm_root);

          printf("\texternal_size=%d\n\tinternode_comm_external_rank=%d\n\tinternode_comm_root=%d\n", external_size, ptn_tables_hash_entry_stored->parent_to_nested.internode_comm_external_rank, ptn_tables_hash_entry_stored->parent_to_nested.internode_comm_root);
          printf("--------------------------\n");

      }

     //*/


 fn_exit:
    MPIU_CHKLMEM_FREEALL();
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}
#endif /* matches #if defined(FINEGRAIN_MPI) */


#else /* !defined(MPID_USE_NODE_IDS) */
int MPIU_Find_local_and_external(MPID_Comm *comm, int *local_size_p, int *local_rank_p, int **local_ranks_p,
                                 int *external_size_p, int *external_rank_p, int **external_ranks_p,
                                 int **intranode_table_p, int **internode_table_p)
{
    int mpi_errno = MPI_SUCCESS;
    
    /* The upper level can catch this non-fatal error and should be
       able to recover gracefully. */
    MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**notimpl");
fn_fail:
    return mpi_errno;
}

#if defined(FINEGRAIN_MPI)
int MPIU_Nested_maps_for_communicators(MPID_Comm *comm)
{
    int mpi_errno = MPI_SUCCESS;

    MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**notimpl");
fn_fail:
    return mpi_errno;
}
#endif

#endif


/* maps rank r in comm_ptr to the rank of the leader for r's node in
   comm_ptr->node_roots_comm and returns this value.
  
   This function does NOT use mpich error handling.
 */
#undef FUNCNAME
#define FUNCNAME MPIU_Get_internode_rank
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIU_Get_internode_rank(MPID_Comm *comm_ptr, int r)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm_valid_ptr( comm_ptr, mpi_errno, TRUE );
    MPIU_Assert(mpi_errno == MPI_SUCCESS);
#if defined(FINEGRAIN_MPI)
    MPIU_Assert(r < comm_ptr->totprocs);
#else
    MPIU_Assert(r < comm_ptr->remote_size);
#endif
    MPIU_Assert(comm_ptr->comm_kind == MPID_INTRACOMM);

#if defined(FINEGRAIN_MPI)
    Parent_to_Nested_comm_tables_coshared_hash_t *ptn_tables_hash_entry_stored = NULL;
    MPIU_Assert(comm_ptr->co_shared_vars != NULL);
    MPIU_Assert(comm_ptr->co_shared_vars->ptn_hash != NULL);

    PTN_HASH_LOOKUP(comm_ptr->co_shared_vars->ptn_hash, r, ptn_tables_hash_entry_stored );
    MPIU_Assert(ptn_tables_hash_entry_stored != NULL);
    /* Map rank r in comm_ptr to the rank of the leader for r's node
       in comm_ptr->node_roots_comm.
       Return value is never -1 if comm_ptr->node_roots_comm != NULL */
    return (ptn_tables_hash_entry_stored->parent_to_nested.internode_comm_root);

#else
    MPIU_Assert(comm_ptr->internode_table != NULL);

    return comm_ptr->internode_table[r];
#endif
}

/* maps rank r in comm_ptr to the rank in comm_ptr->node_comm or -1 if r is not
   a member of comm_ptr->node_comm.
  
   This function does NOT use mpich error handling.
 */
#undef FUNCNAME
#define FUNCNAME MPIU_Get_intranode_rank
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIU_Get_intranode_rank(MPID_Comm *comm_ptr, int r)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm_valid_ptr( comm_ptr, mpi_errno, TRUE );
    MPIU_Assert(mpi_errno == MPI_SUCCESS);
#if defined(FINEGRAIN_MPI)
    MPIU_Assert(r < comm_ptr->totprocs);
#else
    MPIU_Assert(r < comm_ptr->remote_size);
#endif
    MPIU_Assert(comm_ptr->comm_kind == MPID_INTRACOMM);

#if defined(FINEGRAIN_MPI)
    Parent_to_Nested_comm_tables_coshared_hash_t *ptn_tables_hash_entry_stored = NULL;
    MPIU_Assert(comm_ptr->co_shared_vars != NULL);
    MPIU_Assert(comm_ptr->co_shared_vars->ptn_hash != NULL);

    PTN_HASH_LOOKUP(comm_ptr->co_shared_vars->ptn_hash, r, ptn_tables_hash_entry_stored );
    MPIU_Assert(ptn_tables_hash_entry_stored != NULL);
    /* If process r is on the same node as the process calling this
       function, then map rank r in comm_ptr to r's os-process's
       leader's rank in comm_ptr->node_comm. Else return -1 */
    return (ptn_tables_hash_entry_stored->parent_to_nested.intranode_comm_root);

#else
    MPIU_Assert(comm_ptr->intranode_table != NULL);

    /* FIXME this could/should be a list of ranks on the local node, which
       should take up much less space on a typical thin(ish)-node system. */
    return comm_ptr->intranode_table[r];
#endif
}


#if defined(FINEGRAIN_MPI)
/* If process r is colocated to the process calling this function,
   then map rank r in comm_ptr to r's rank in comm_ptr->osproc_colocated_comm.
   Else return -1.

   This function does NOT use mpich error handling.
 */
#undef FUNCNAME
#define FUNCNAME MPIU_Get_intra_osproc_rank
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIU_Get_intra_osproc_rank(MPID_Comm *comm_ptr, int r)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm_valid_ptr( comm_ptr, mpi_errno, TRUE );
    MPIU_Assert(mpi_errno == MPI_SUCCESS);
#if defined(FINEGRAIN_MPI)
    MPIU_Assert(r < comm_ptr->totprocs);
#else
    MPIU_Assert(r < comm_ptr->remote_size);
#endif
    MPIU_Assert(comm_ptr->comm_kind == MPID_INTRACOMM);

    Parent_to_Nested_comm_tables_coshared_hash_t *ptn_tables_hash_entry_stored = NULL;
    MPIU_Assert(comm_ptr->co_shared_vars != NULL);
    MPIU_Assert(comm_ptr->co_shared_vars->ptn_hash != NULL);

    PTN_HASH_LOOKUP(comm_ptr->co_shared_vars->ptn_hash, r, ptn_tables_hash_entry_stored );
    MPIU_Assert(ptn_tables_hash_entry_stored != NULL);
    return (ptn_tables_hash_entry_stored->parent_to_nested.intra_osproc_fg_rank);
}
#endif /* matches #if defined (FINEGRAIN_MPI) */
