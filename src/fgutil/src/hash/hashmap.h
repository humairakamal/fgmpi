/*
 *  (C) 2008 Humaira Kamal, The University of British Columbia.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdio.h>
#include <stdlib.h>
#include "hashlib.h"
#include "cokusmt.h"
#include "mpiu_uthash.h"

typedef struct ranktoworld {
    int fgrank; /* local rank of FGP in the communicator. Will be used as key */
    int worldrank; /* rank of FGP in the MPI_COMM_WORLD */
} ranktoworlditem, * ranktoworlditemptr;


extern inline hshtbl * RTWhashCreate(int mapsize);
extern inline int RTWhashFind(hshtbl *rtw_hshtbl, int key, int *worldrank_ptr); /*(IN, IN, OUT)*/
extern inline int RTWhashInsert(hshtbl *rtw_hshtbl, int key, int worldrank); /*(IN, IN, IN)*/
extern inline int RTWhashBlockInsert(hshtbl *rtw_hshtbl, int size, int *blockarray); /*(IN, IN, IN)*/
extern inline hshtbl * RTWhashDuplicate(hshtbl *orig, int size);
extern inline int RTWhashFindLeader(hshtbl *RTWhshtbl, int numentries_hsh);

extern inline int* RTWarrayWorldCreate(int mapsize);
extern inline int* RTWarrayCreate(int mapsize);
extern inline int RTWarrayFind(int *rtw_arraymap, int key, int *worldrank_ptr); /*(IN, IN, OUT)*/
extern inline int RTWarrayInsert(int *rtw_arraymap, int key, int worldrank); /*(IN, IN, IN)*/
extern int RTWarrayBlockInsert(int *rtw_arraymap, int size, int *blockarray); /*(IN, IN, IN)*/
extern inline int RTWarrayFindLeader(int *rtw_arraymap, int numentries_map);
extern inline void RTWarrayKill(int *rtw_arraymap);



/* FG: This may in some cases be similar to the kind of requests
   defined in MPID_Request_kind_t like Send, Recv, SSend etc.
   Need to define how Send and Recv requests will be handled.
   Will it be sufficient to put Send requests at end of
   runnable queue and Recv ones on blocked queue? It will be
   interesting to gather some statistics on how often send
   requests are unable to progress and must wait on the socket.

   However, there are other events too that will occur like
   barrier, split, init, finalize. Those need a collective
   handling mechanism based on unique event-id (some combination of
   context and tag?) and hashed into a single linked-list?).
   Need to check if info for context-id or tag etc are available
   through the request structure. TODO CHECK: Should not depend on
   tag as that info is likely abstracted away (especially
   for long message protocol (LMP). Also, other info like dest ranks
   etc. is abstracted in LMP - need to check what is available)
*/
typedef enum { /* FG: TODO. May be augmented. */
    INIT,
    SEND,
    RECV
} MPI_event_kind_t;

typedef enum {
    NONE,
    BLOCK,
    UNBLOCK
} Scheduler_action_kind_t;

typedef struct scheduler_event_queue {
    int worldrank; /* World rank of FG-MPI process in MPI_COMM_WORLD. Will be used as key */
    MPI_event_kind_t mpi_state_on_event; /* State of MPI process at the time of yield  */
    Scheduler_action_kind_t action_on_event; /* Action to be taken at the time of yield */
    void *sched_unit;
} schedQueue_item, *schedQueue_itemptr, scheduler_event;

extern inline hshtbl * SchedulerHashCreate(void);
extern inline int SchedulerHashFind(hshtbl *sched_hshtbl, int key, schedQueue_itemptr *stored); /*(IN, IN, OUT)*/
extern inline  int SchedulerHashInsert(hshtbl *sched_hshtbl, int key, MPI_event_kind_t mpi_state, Scheduler_action_kind_t action, void* sched_unit); /*(IN, IN, IN, IN, IN)*/
extern inline int SchedulerHashRemove(hshtbl *sched_hshtbl, int key, schedQueue_itemptr *stored); /*(IN, IN, OUT)*/




struct contextidLeaderKey{
    int context_id;
    int LeaderWorldRank;
};

typedef struct contextidLeader{
    struct contextidLeaderKey cLkey; /* Key for looking up the shared group map associated with a
                                        communciator's context-id and the leader's world rank. The
                                        leader is the proclet with the smallest world rank among all
                                        the proclets that are part of the communicator. */
    void * coproclet_shared_vars;    /* shared rtw map for the {context-id, Leader-world-rank} combination
                                        and  barrier variables for the {context-id, Leader-world-rank}
                                        combination
                                     */
} cLitem, *cLitemptr;


extern inline hshtbl * CL_LookupHashCreate(void);
extern inline int CL_LookupHashFind(hshtbl *CL_hshtbl, int context_id, int LeaderWorldRank, cLitemptr *stored); /* IN, IN, IN, OUT */
extern int CL_LookupHashInsert(hshtbl *CL_hshtbl, int context_id, int LeaderWorldRank, void* coproclet_shared_vars, cLitemptr *stored); /* IN, IN, IN, IN, OUT */


typedef struct contextidLookupHash{
    int context_id; /* Key for looking up the shared group map associated with a
                                        communciator's context-id */
    void * coproclet_shared_vars;  /*shared rtw map for the {context-id, Leader-world-rank} combination
                                     and  barrier variables for the {context-id, Leader-world-rank} combination
                                   */
} cidLookupHashItem, *cidLookupHashItemptr;


extern inline hshtbl * CidLookupHashCreate(void);
extern inline int CidLookupHashFind(hshtbl *cid_lookuphshtbl, int cid, cidLookupHashItemptr *stored); /*(IN, IN, OUT)*/
extern inline int CidLookupHashInsert(hshtbl *cid_lookuphshtbl, int cid, void* coproclet_shared_vars, cidLookupHashItemptr *stored); /* IN, IN, IN, OUT */

/*======================================*/
/* structs for nested communicators */

//#define COMM_COMMIT_USES_UTHASH 1

typedef struct Parent_to_Nested_comm_tables {
    /*** internode ***/
    int internode_comm_root; /* root of node (rank in roots_comm) */
    int internode_comm_external_rank; /* -1 if this process is not in node_roots_comm */

    /*** intranode ***/
    int intranode_comm_root; /* root of os-process (rank in node_comm) */
    int intranode_comm_local_rank; /* -1 if this process is not in node_comm */

    /*** intra os-process ***/
    int intra_osproc_fg_rank; /* intra os-process fg rank. -1 if this process is not colocated */
} Parent_to_Nested_comm_tables_t;


#if defined(COMM_COMMIT_USES_UTHASH)
typedef struct Nested_comm_rtwmap_hash {
    int rank;  /* key */
    int worldrank;
    UT_hash_handle hh;
} Nested_comm_rtwmap_uthash_t;
typedef Nested_comm_rtwmap_uthash_t Nested_comm_rtwmap_hash_t;
#else
typedef hshtbl Nested_comm_rtwmap_hash_t;
#endif

typedef struct Nestedcomm_uniform_vars {
    /*** internode ***/
    int external_size;
    Nested_comm_rtwmap_hash_t *internode_rtw_hash;

    /*** intranode ***/
    int local_size;
    Nested_comm_rtwmap_hash_t *intranode_rtw_hash;

    /*** intra os-process ***/
    int fg_local_size;
    Nested_comm_rtwmap_hash_t *intra_osproc_rtw_hash;
} Nestedcomm_uniform_vars_t;

/* Shared hash for inter and intra node comm hierarchy */
typedef struct Parent_to_Nested_comm_tables_coshared_hash {
    int parent_comm_rank;  /* key */
    Parent_to_Nested_comm_tables_t parent_to_nested;
#if defined(COMM_COMMIT_USES_UTHASH)
    UT_hash_handle hh;
#endif
} Parent_to_Nested_comm_tables_coshared_hash_t, ptnLookupHashItem, *ptnLookupHashItemptr;

#if defined(COMM_COMMIT_USES_UTHASH)
typedef Parent_to_Nested_comm_tables_coshared_hash_t ptn_comm_tables_hash_t;
#else
typedef hshtbl ptn_comm_tables_hash_t;
#endif


#if defined(COMM_COMMIT_USES_UTHASH)
#define RTW_HASH_INSERT(hash, localrank, worldrank)                     \
    do {                                                                \
        Nested_comm_rtwmap_hash_t *rtw_hash_entry = MPIU_Calloc(1,sizeof(struct Nested_comm_rtwmap_hash)); \
        MPIU_Assert(rtw_hash_entry);                                    \
        rtw_hash_entry->rank = localrank;                               \
        rtw_hash_entry->worldrank = worldrank;                          \
        HASH_ADD_INT(hash, rank, rtw_hash_entry);/*rank is variable name of key in Nested_comm_rtwmap_hash*/ \
    } while(0)

#define PTN_HASH_INSERT(hash, parent_rank, parent_to_nested_)           \
    do {                                                                \
        Parent_to_Nested_comm_tables_coshared_hash_t *ptn_tables_hash_entry = MPIU_Calloc(1,sizeof(struct Parent_to_Nested_comm_tables_coshared_hash)); \
        MPIU_Assert(ptn_tables_hash_entry);                             \
        ptn_tables_hash_entry->parent_comm_rank = parent_rank;          \
        ptn_tables_hash_entry->parent_to_nested.intra_osproc_fg_rank = parent_to_nested_.intra_osproc_fg_rank; \
        ptn_tables_hash_entry->parent_to_nested.internode_comm_external_rank = parent_to_nested_.internode_comm_external_rank; \
        ptn_tables_hash_entry->parent_to_nested.internode_comm_root = parent_to_nested_.internode_comm_root; \
        ptn_tables_hash_entry->parent_to_nested.intranode_comm_local_rank = parent_to_nested_.intranode_comm_local_rank; \
        ptn_tables_hash_entry->parent_to_nested.intranode_comm_root = parent_to_nested_.intranode_comm_root; \
        HASH_ADD_INT(hash, parent_comm_rank, ptn_tables_hash_entry);    \
    } while(0)

#define PTN_HASH_LOOKUP(hash, key, hash_entry) HASH_FIND_INT(hash, &(key), hash_entry) /* Note: &(key) */

#define FREE_HASH(hash,hashtype)                        \
    do {                                                \
        if (NULL != hash) {                             \
            hashtype *current_entry, *tmp;              \
            HASH_ITER(hh, hash, current_entry, tmp) {   \
                HASH_DEL(hash,current_entry);           \
                MPIU_Free(current_entry);               \
            }                                           \
        }                                               \
        MPIU_Assert(NULL == hash);                      \
    } while(0)

#define EXTRACT_ARRAY_FREE_HASH(hash,hashtype,fillarray,arraysize)      \
    do {                                                                \
        if (NULL != hash) {                                             \
            hashtype *current_entry, *tmp;                              \
            int count = 0;                                              \
            HASH_ITER(hh, hash, current_entry, tmp) {                   \
                MPIU_Assert((current_entry->rank < arraysize) && (current_entry->rank >= 0)) ; \
                fillarray[current_entry->rank] = current_entry->worldrank; \
                count++;                                                \
                HASH_DEL(hash,current_entry);                           \
                MPIU_Free(current_entry);                               \
            }                                                           \
            MPIU_Assert(count == arraysize);                            \
            MPIU_Assert(NULL == hash);                                  \
        }                                                               \
    } while(0)

#else

#define RTW_HASH_INSERT(hash, localrank, worldrank)             \
    do {                                                        \
        RTWhashInsert(hash, localrank, worldrank);              \
    } while(0)

#define PTN_HASH_INSERT(hash, parent_rank, parent_to_nested_)           \
    do {                                                                \
        ptnLookupHashItemptr stored = NULL;                             \
        ptnLookupHashInsert(hash, parent_rank, parent_to_nested_, &stored); \
        MPIU_Assert(NULL != stored);                                    \
    } while(0)

#define PTN_HASH_LOOKUP(hash, key, hash_entry)                  \
    do {                                                        \
        ptnLookupHashFind(hash, key, &(hash_entry));            \
    } while(0)

#define FREE_HASH(hash,hashtype)                \
    do {                                        \
        hshtblFree(&hash);                      \
    } while(0)

/* Note: following EXTRACT_ARRAY_FREE_HASH only works for RTWhash */
#define EXTRACT_ARRAY_FREE_HASH(hash,hashtype,fillarray,arraysize)      \
    do {                                                                \
        if (NULL != hash) {                                             \
            int count, worldrank = -1;                                  \
            for (count=0; count<arraysize; count++) {                   \
                RTWhashFind(hash, count, &worldrank);                   \
                fillarray[count] = worldrank;                           \
            }                                                           \
            FREE_HASH(hash,hashtype);                                   \
            MPIU_Assert(NULL == hash);                                  \
        }                                                               \
    } while(0)

#endif


extern inline hshtbl * ptnLookupHashCreate(void);
extern inline int ptnLookupHashFind(hshtbl *ptn_lookuphshtbl, int parent_rank, ptnLookupHashItemptr *stored); /*(IN, IN, OUT)*/
extern inline int ptnLookupHashInsert(hshtbl *ptn_lookuphshtbl, int parent_rank, Parent_to_Nested_comm_tables_t parent_to_nested, ptnLookupHashItemptr *stored); /* IN, IN, IN, OUT */

void hshtblFree(hshtbl **hash_dptr);

#endif /* HASHMAP_H */
