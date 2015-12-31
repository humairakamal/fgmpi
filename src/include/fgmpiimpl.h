/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2014 FG-MPI: Fine-Grain MPI.
 *      See copyright in top-level directory.
 */

#ifndef FGMPIIMPL_H_INCLUDED
#define FGMPIIMPL_H_INCLUDED

#if defined(FINEGRAIN_MPI)
#include "threadlib_internal.h"
#include "hashlib.h"
#include "hashmap.h"

#define MAX_COLOCATED_YIELDS  2

typedef void* OPAQUE;

extern void * FG_MPIU_Malloc(size_t a, const char fcname[], int lineno);
#define MPIU_Malloc_loc(a_, fcname_, line_) FG_MPIU_Malloc(a_, (char*)fcname_, line_)

#ifdef MPIU_Malloc
#undef MPIU_Malloc
#define MPIU_Malloc(a) MPIU_Malloc_loc(a, __FUNCTION__, __LINE__)
#endif

#define FG_Panic exit(-1)

void FG_Init(void);
void FG_Finalize(void);

extern void MPIX_Yield(void);
extern void FG_yield(const char fcname[], int lineno);
#define FG_Yield() FG_yield((char*)__FUNCTION__, __LINE__)
extern void FG_yield_on_event(scheduler_event yld_event, const char fcname[], int lineno);
#define FG_Yield_on_event(tye)  FG_yield_on_event(tye, (char*)__FUNCTION__, __LINE__);
extern void FG_notify_on_event(int worldrank, int action, const char fcname[], int lineno);
#define FG_Notify_on_event(worldrank, action)  FG_notify_on_event(worldrank, action, (char*)__FUNCTION__, __LINE__);
extern void* FG_Scheduler_progress_loop(void* args);


#define IS_SPAWNER (((struct StateWrapper*)(CO_CURRENT->statevars))->is_spawner)
#define my_fgrank (((struct StateWrapper*)(CO_CURRENT->statevars))->fgrank)



#if !defined(COMP_MAP)
#define ARRAY_MAP 1
#endif


#if defined(ARRAY_MAP)
 typedef int RTWmap;
 #define RTWmapWorldCreate RTWarrayWorldCreate
 #define RTWmapCreate RTWarrayCreate
 #define RTWmapInsert RTWarrayInsert
 #define RTWmapBlockInsert RTWarrayBlockInsert
 #define RTWmapFind RTWarrayFind
 
 #define RTWmapKill(_rtw_map_dptr) {                         \
            RTWarrayKill((RTWmap*)(*_rtw_map_dptr));         \
            *(_rtw_map_dptr) = NULL;                     }

#define RTWmapFindLeader(map,size) ((map == worldcomm_rtw_map) ? 0 : RTWarrayFindLeader(map, size))

#elif defined(COMP_MAP)
#include "compmap.h"
 typedef CompressedMap RTWmap;
 #define RTWmapWorldCreate  RTWCompMapCreate
 #define RTWmapCreate       RTWCompMapCreate
 #define RTWmapInsert
 #define RTWmapBlockInsert  RTWCompMapBlockInsert
 #define RTWmapFind         RTWCompMapFind

 #define RTWmapKill(_rtw_map_dptr) {                         \
            RTWCompMapKill((RTWmap*)(*_rtw_map_dptr));       \
            *(_rtw_map_dptr) = NULL;                     }
 #define RTWmapFindLeader   RTWCompMapFindLeader /* FG: TODO special case for MPI_WORLD_COMM */

#else
 /* Default is always HASHMAP. In case no other representation is defined,
    then HASHMAP will be used.
 */
 typedef hshtbl RTWmap;
 #define RTWmapWorldCreate RTWhashCreate
 #define RTWmapCreate RTWhashCreate
 #define RTWmapInsert RTWhashInsert
 #define RTWmapBlockInsert RTWhashBlockInsert
 #define RTWmapFind RTWhashFind

 #define RTWmapKill(_rtw_map_dptr) {                         \
            hshkill((RTWmap*)(*_rtw_map_dptr));              \
            *(_rtw_map_dptr) = NULL;                     }
 #define RTWmapFindLeader RTWhashFindLeader /* FG: TODO special case for MPI_WORLD_COMM */
 #define RTWmapDuplicate RTWhashDuplicate  /* FG: TODO special case for MPI_WORLD_COMM */
#endif

extern RTWmap *worldcomm_rtw_map;

extern int curr_fgrank;
extern int PMI_totprocs;


typedef struct FGP_tuple{
    int fg_startrank;
    int numfgps;
} FGP_tuple_t;

typedef struct coproclet_barrier_vars {
    volatile int coproclet_signal;
    volatile int leader_signal;
    volatile int coproclet_counter;
} coproclet_barrier_vars_t;


typedef struct Coproclet_shared_vars {
    int *ref_withinComm_countptr; /* This is for variables shared among colocated
                          processes but not shared across communicators through e.g., MPI_Comm_dup */
    int *ref_acrossComm_countptr; /* This is for variables shared among colocated
                          processes as well as shared across communicators through e.g., MPI_Comm_dup
                          Making this a pointer so that it can be shared in MPIR_Comm_copy() */
    RTWmap *rtw_map;      /* This rtw_map provides mapping from local fgrank in this
                             communicator to the MPI_COMM_WORLD rank (referred to as worldrank). */
    ptn_comm_tables_hash_t *ptn_hash;

    Nestedcomm_uniform_vars_t nested_uniform_vars;

    coproclet_barrier_vars_t *co_barrier_vars; /* Shared SM barrier variable among co-proclets of same group.
                                                  This is uniquely identified by context-id and the Leader-world-rank.*/
} Coproclet_shared_vars_t;

#define MPIR_Comm_init_coshared_group_ref(_new_group_ptr)                 \
    { (_new_group_ptr)->ref_acrossCommGroup_countptr = NULL;                         \
    }

#define MPIR_Comm_add_coshared_group_ref(_group_ptr)                 \
    { (*((_group_ptr)->ref_acrossCommGroup_countptr))++;                  \
    }

#define MPIR_Comm_release_coshared_group_ref(_group_ptr)                 \
    { --(*((_group_ptr)->ref_acrossCommGroup_countptr));                  \
    }

#define MPIR_Comm_init_coshared_withinComm_ref(_coshared_vars)                 \
    { _coshared_vars->ref_withinComm_countptr = MPIU_Malloc(sizeof(int));      \
        *(_coshared_vars->ref_withinComm_countptr) = 1;}

#define MPIR_Comm_add_coshared_withinComm_ref(_coshared_vars)                 \
    { (*(((Coproclet_shared_vars_t *)(_coshared_vars))->ref_withinComm_countptr))++;  \
      MPIU_DBG_MSG_FMT(REFCOUNT,TYPICAL,(MPIU_DBG_FDEST,\
                                         "Incr comm coshared withinComm ref count to %d",*(((Coproclet_shared_vars_t *)(_coshared_vars))->ref_withinComm_countptr)));}

#define MPIR_Comm_release_coshared_withinComm_ref( _coshared_vars, inuse_ptr ) \
    { *(inuse_ptr) = --(*(((Coproclet_shared_vars_t *)(_coshared_vars))->ref_withinComm_countptr)); \
       MPIU_DBG_MSG_FMT(REFCOUNT,TYPICAL,(MPIU_DBG_FDEST,\
                                          "Decr comm coshared withinComm ref count to %d",*(((Coproclet_shared_vars_t *)(_coshared_vars))->ref_withinComm_countptr)));}


#define MPIR_Comm_init_coshared_acrossComm_ref(_coshared_vars)                 \
    { _coshared_vars->ref_acrossComm_countptr = MPIU_Malloc(sizeof(int));      \
        *(_coshared_vars->ref_acrossComm_countptr) = 1;}

#define MPIR_Comm_add_coshared_acrossComm_ref(_coshared_vars)                 \
    { (*(((Coproclet_shared_vars_t *)(_coshared_vars))->ref_acrossComm_countptr))++;  \
      MPIU_DBG_MSG_FMT(REFCOUNT,TYPICAL,(MPIU_DBG_FDEST,\
                                         "Incr comm coshared acrossComm ref count to %d",*(((Coproclet_shared_vars_t *)(_coshared_vars))->ref_acrossComm_countptr)));}

#define MPIR_Comm_release_coshared_acrossComm_ref( _coshared_vars, inuse_ptr ) \
    { *(inuse_ptr) = --(*(((Coproclet_shared_vars_t *)(_coshared_vars))->ref_acrossComm_countptr)); \
       MPIU_DBG_MSG_FMT(REFCOUNT,TYPICAL,(MPIU_DBG_FDEST,\
                                          "Decr comm coshared ref acrossComm count to %d",*(((Coproclet_shared_vars_t *)(_coshared_vars))->ref_acrossComm_countptr)));}


#define MPIR_Comm_init_coshared_all_ref(_coshared_vars)                   \
    {   MPIR_Comm_init_coshared_withinComm_ref(_coshared_vars);           \
        MPIR_Comm_init_coshared_acrossComm_ref(_coshared_vars);  }

#define MPIR_Comm_add_coshared_all_ref(_coshared_vars)                   \
    {   MPIR_Comm_add_coshared_withinComm_ref(_coshared_vars);           \
        MPIR_Comm_add_coshared_acrossComm_ref(_coshared_vars);  }

#define MPIR_Comm_release_coshared_all_ref( _coshared_vars, inuse_withinComm_ptr, inuse_acrossComm_ptr ) \
    {   MPIR_Comm_release_coshared_withinComm_ref( _coshared_vars, inuse_withinComm_ptr ); \
        MPIR_Comm_release_coshared_acrossComm_ref( _coshared_vars, inuse_acrossComm_ptr ); }



/* pid_to_fgps is a 1-many mapping from the HWP pid to the FG worldranks in MPI_COMM_WORLD. */
extern FGP_tuple_t *pid_to_fgps;
extern hshtbl *contextLeader_hshtbl; /* This will remain a hashtable. */
extern struct coproclet_barrier_vars *worldcomm_barrier_vars;
extern hshtbl *cidLookup_hshtbl;          /* cid lookup hashtable that will be used with
                                             the new CID = <LID,LBI> algorithm. */
extern Coproclet_shared_vars_t * world_co_shared_vars;

#define MPIDI_Comm_get_pid_worldrank(comm_, localrank_, pid_ptr_, worldrank_ptr_) \
    do {                                                                \
        if ( MPI_PROC_NULL == localrank_ ) {                            \
            FG_set_PROC_NULL(worldrank_ptr_, pid_ptr_);                 \
        } else {                                                        \
            RTWPmapFind((comm_->co_shared_vars)->rtw_map, localrank_, worldrank_ptr_, pid_ptr_); \
        }                                                               \
    } while (0)



#endif /* #if defined(FINEGRAIN_MPI) */

#endif /* FGMPIIMPL_H_INCLUDED */
