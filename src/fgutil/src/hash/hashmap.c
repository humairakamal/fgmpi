/*
 *  (C) 2008 Humaira Kamal, The University of British Columbia.
 *      See COPYRIGHT in top-level directory.
 */

#include "hashmap.h"
#include "assert.h"
#include <string.h>

/*************RTW (local rank to world rank) Hash Map*************/

static unsigned long ranktoworldhash(void *item)
{
    ranktoworlditemptr rtw_ptr = (ranktoworlditemptr) item;

   return rtw_ptr->fgrank;
} /* hash */


static unsigned long ranktoworldrehash(void *item)
{
   ranktoworlditemptr rtw_ptr = (ranktoworlditemptr) item;

   return rtw_ptr->fgrank >> 3;
} /* rehash */


static int ranktoworldcmp(void *litem, void *ritem)
{
   ranktoworlditemptr rtw_lptr = (ranktoworlditemptr) litem,
             rtw_rptr = (ranktoworlditemptr) ritem;

   if     (rtw_lptr->fgrank == rtw_rptr->fgrank) return 0;
   else if (rtw_lptr->fgrank > rtw_rptr->fgrank) return 1;
   else                                      return -1;
} /* cmp */


static void *ranktoworlddupe(void *item)
{
   ranktoworlditemptr rtw_ptr = (ranktoworlditemptr) item,
             rtw_dupe;

   if ((rtw_dupe = (ranktoworlditemptr) malloc(sizeof *rtw_dupe))){
       *rtw_dupe = *rtw_ptr;
   }else{
       printf("Cannot allocate memory in %s\n", __FUNCTION__);
       exit(-1);
   }
   return rtw_dupe;
} /* dupe */


static void ranktoworldundupe(void *item)
{
    ranktoworlditemptr rtw_ptr = (ranktoworlditemptr) item;
    free(rtw_ptr);
    rtw_ptr = NULL;
} /* undupe */


inline hshtbl * RTWhashCreate(int mapsize)
{
    hshtbl   *h;
    h = hshinit(ranktoworldhash,             /* hash function */
                ranktoworldrehash,           /* rehash function */
                ranktoworldcmp,              /* compare function */
                ranktoworlddupe,             /* dupe function */
                ranktoworldundupe,           /* hshfree function */
                0);                        /* use debug output */
    return h;

}

inline int RTWhashFind(hshtbl *rtw_hshtbl, int key, int *worldrank_ptr) /*(IN, IN, OUT)*/
{
    ranktoworlditem find_item;
    ranktoworlditemptr stored = NULL;
    find_item.fgrank = key;
    if (!(stored = (ranktoworlditemptr)hshfind(rtw_hshtbl, &find_item))) {
        printf("%s: find_item.fgrank=%d not found in hashtable\n", __FUNCTION__, find_item.fgrank);
        exit(-1);
    }
    *worldrank_ptr = stored->worldrank;
    assert((*worldrank_ptr)>=0);
    return (0);
}

inline int RTWhashInsert(hshtbl *rtw_hshtbl, int key, int worldrank) /*(IN, IN, IN)*/
{
    ranktoworlditem rtw_item;
    ranktoworlditemptr inserted = NULL;
    rtw_item.fgrank = key;
    rtw_item.worldrank = worldrank;
    if (!(inserted = (ranktoworlditemptr)hshinsert(rtw_hshtbl, &rtw_item))) {
        printf("%s: Store failure in rtw_hshtbl\n", __FUNCTION__);
        exit(-1);
    }
    return (0);
}

inline int RTWhashBlockInsert(hshtbl *rtw_hshtbl, int size, int *blockarray) /*(IN, IN, IN)*/
{
    ranktoworlditem rtw_item;
    ranktoworlditemptr inserted = NULL;
    int i;
    for(i=0; i<size; i++){
        rtw_item.fgrank = i;
        rtw_item.worldrank = blockarray[i];
        if (!(inserted = (ranktoworlditemptr)hshinsert(rtw_hshtbl, &rtw_item))) {
            printf("%s: Store failure in rtw_hshtbl\n", __FUNCTION__);
            exit(-1);
        }
        inserted = NULL;
    }
    return (0);
}


static inline void print_rtwItem(ranktoworlditemptr p)
{
    printf("fgrank=%d, worldrank=%d\n", p->fgrank, p->worldrank);

}


inline hshtbl * RTWhashDuplicate(hshtbl *orig_RTWhshtbl, int numentries_hsh){
    int i;
    hshtbl * duphshtbl = RTWhashCreate(numentries_hsh);
    ranktoworlditem find_item;
    ranktoworlditemptr stored = NULL, newstored = NULL;
    for (i=0; i<numentries_hsh; i++)
    {
        find_item.fgrank = i; /* This is the local fgrank (range from 0 to numentries_hsh) */
        if (!(stored = (ranktoworlditemptr)hshfind(orig_RTWhshtbl, &find_item))) {
            printf("%s: find_item.fgrank=%d not found in hashtable\n", __FUNCTION__, find_item.fgrank);
            exit(-1);
        }

        /* Store in the duplicate hash */
        if (!(newstored = (ranktoworlditemptr)hshinsert(duphshtbl, stored))) {
            printf("%s: Store failure in duphshtbl\n", __FUNCTION__);
            exit(-1);
        }

    }
    return (duphshtbl);

}


inline int RTWhashFindLeader(hshtbl *RTWhshtbl, int numentries_hsh){
    int i;
    int leaderWorldRank = -1;
    ranktoworlditem find_item;
    ranktoworlditemptr stored = NULL;
    for (i=0; i<numentries_hsh; i++)
    {
        find_item.fgrank = i; /* This is the local fgrank (range from 0 to numentries_hsh) */
        if (!(stored = (ranktoworlditemptr)hshfind(RTWhshtbl, &find_item))) {
            printf("%s: find_item.fgrank=%d not found in hashtable\n", __FUNCTION__, find_item.fgrank);
            exit(-1);
        }
        if ((-1 == leaderWorldRank) || (leaderWorldRank > stored->worldrank)){
            leaderWorldRank = stored->worldrank;
        }
    }
    assert(leaderWorldRank >= 0);
    return (leaderWorldRank);

}

static void showstate(hshtbl *m)
{
   hshstats hs;

   hs = hshstatus(m);
   printf("Status: Entries=%lu, Deleted=%lu, Probes=%lu, Misses=%lu",
           hs.hentries, hs.hdeleted, hs.probes, hs.misses);
   if (hs.herror) {
      if (hs.herror & hshNOMEM)   printf(", NOMEM");
      if (hs.herror & hshTBLFULL) printf(", TBLFULL");
      if (hs.herror & hshINTERR)  printf(", INTERR");
   }
   printf("\n");
} /* showstate */




/*****************RTW Array Map*************************/

inline int* RTWarrayWorldCreate(int mapsize)
{
    int *arraymap;
    if( !(arraymap = (int *) malloc(sizeof(int)) ) ) /* Ignoring the mapsize for MPI_WORLD_COMM array map */
    {
        printf("Cannot allocate memory in %s\n", __FUNCTION__);
        exit(-1);
    }
    arraymap[0] = -2; /* Adding a sentinel */
    return arraymap;

}
inline int* RTWarrayCreate(int mapsize)
{
    int *arraymap;
    if( !(arraymap = (int *) malloc((mapsize+1) * sizeof(int))) )
    {
        printf("Cannot allocate memory in %s\n", __FUNCTION__);
        exit(-1);
    }
    arraymap[mapsize] = -2; /* Adding a sentinel */
    return arraymap;
}


inline int RTWarrayFind(int *rtw_arraymap, int key, int *worldrank_ptr) /*(IN, IN, OUT)*/
{
    *worldrank_ptr = rtw_arraymap[key];
    assert((*worldrank_ptr)>=0);
    return (0);
}

inline int RTWarrayInsert(int *rtw_arraymap, int key, int worldrank) /*(IN, IN, IN)*/
{
    rtw_arraymap[key] = worldrank;
    return (0);
}

inline int RTWarrayBlockInsert(int *rtw_arraymap, int size, int *blockarray) /*(IN, IN, IN)*/
{
    memcpy(rtw_arraymap, blockarray, size*sizeof(int));
    return (0);
}


inline int RTWarrayFindLeader(int *rtw_arraymap, int numentries_map) {
    int i = 0;
    int leaderWorldRank = -1;
    int stored_worldrank;

    for (i=0; i<numentries_map; i++) {
        stored_worldrank = rtw_arraymap[i];
        if ((-1 == leaderWorldRank) || (leaderWorldRank > stored_worldrank)) {
            leaderWorldRank = stored_worldrank;
        }
    }
    assert(leaderWorldRank >= 0);
    return (leaderWorldRank);
}

inline void RTWarrayKill(int *rtw_arraymap){
    if(rtw_arraymap){
        free(rtw_arraymap);
        rtw_arraymap = NULL;
    }
}



/*****************Scheduler Queue Hashtable*************************/

static unsigned long SchedulerQueueItem_hash(void *item)
{
    schedQueue_itemptr SQitem_ptr = (schedQueue_itemptr) item;

    return (SQitem_ptr->worldrank);
} /* hash */


static unsigned long SchedulerQueueItem_rehash(void *item)
{
    schedQueue_itemptr SQitem_ptr = (schedQueue_itemptr) item;

    return (SQitem_ptr->worldrank)>>3;
} /* rehash */


static int  SchedulerQueueItem_cmp(void *litem, void *ritem)
{
   schedQueue_itemptr SQitem_lptr = (schedQueue_itemptr) litem,
             SQitem_rptr = (schedQueue_itemptr) ritem;
   if     (SQitem_lptr->worldrank == SQitem_rptr->worldrank) return 0;
   else if (SQitem_lptr->worldrank > SQitem_rptr->worldrank) return 1;
   else                                      return -1;
} /* cmp */


static void *SchedulerQueueItem_dupe(void *item)
{
   schedQueue_itemptr SQitem_ptr = (schedQueue_itemptr) item,
             SQitem_dupe;

   if ((SQitem_dupe = (schedQueue_itemptr) malloc(sizeof *SQitem_dupe))){
       *SQitem_dupe = *SQitem_ptr;
   } else {
       printf("Cannot allocate memory in %s\n", __FUNCTION__);
       exit(-1);
   }
   return SQitem_dupe;
} /* dupe */


static void SchedulerQueueItem_undupe(void *item)
{
    schedQueue_itemptr SQitem_ptr = (schedQueue_itemptr) item;
    free(SQitem_ptr);
} /* undupe */


inline hshtbl * SchedulerHashCreate(void)
{
    hshtbl   *h;
    h = hshinit(SchedulerQueueItem_hash,             /* hash function */
                SchedulerQueueItem_rehash,           /* rehash function */
                SchedulerQueueItem_cmp,              /* compare function */
                SchedulerQueueItem_dupe,             /* dupe function */
                SchedulerQueueItem_undupe,           /* hshfree function */
                0);                        /* use debug output */
    return h;

}

static void print_SchedulerHashItem(schedQueue_itemptr p)
{
    printf("worldrank=%d, mpi_state_on_event=%d, action_on_event=%d\n", p->worldrank, p->mpi_state_on_event, p->action_on_event);

}

inline int SchedulerHashFind(hshtbl* sched_hshtbl, int key, schedQueue_itemptr* storedptrptr) /*(IN, IN, OUT)*/
{
    schedQueue_item SQitem;
    *storedptrptr = NULL;
    SQitem.worldrank = key;
    *storedptrptr = (schedQueue_itemptr)hshfind(sched_hshtbl, &SQitem);
    if (NULL != *storedptrptr){
        return (1);
    } else {
        return (0);
    }
}

inline int SchedulerHashInsert(hshtbl *sched_hshtbl, int key, MPI_event_kind_t mpi_state, Scheduler_action_kind_t action, void* sched_unit) /*(IN, IN, IN, IN, IN)*/
{
    schedQueue_item SQitem;
    schedQueue_itemptr stored = NULL;
    SQitem.worldrank = key;
    SQitem.mpi_state_on_event = mpi_state;
    SQitem.action_on_event = action;
    SQitem.sched_unit = sched_unit;
    stored = (schedQueue_itemptr)hshinsert(sched_hshtbl, &SQitem); /* Calls SchedulerQueueItem_dupe  to store in hashtable*/
    if ( NULL == stored ) {
        printf("%s: Store failure in sched_hshtbl\n", __FUNCTION__);
        exit(-1);
    }
    return (0);
}

static inline void* SchedulerHashDelete(hshtbl *sched_hshtbl, schedQueue_itemptr stored) /*(IN, IN)*/
{
    schedQueue_itemptr removed = NULL;
    removed = (schedQueue_itemptr) hshdelete(sched_hshtbl, stored);
    return (removed);
}

inline int SchedulerHashRemove(hshtbl *sched_hshtbl, int key, schedQueue_itemptr *stored) /*(IN, IN, OUT). IMPORTANT: De-allocating the removed item in sched_block_queue_unblock_thread() */
{
    int found = 0;
    *stored = NULL;
    found = SchedulerHashFind(sched_hshtbl, key, stored);

    if (found) {
        assert (*stored);
        schedQueue_itemptr removed = NULL;
        if ( !(removed = (schedQueue_itemptr) SchedulerHashDelete(sched_hshtbl, *stored)) )
            {
                printf("%s: Trying to delete a non-existent item from Scheduler hash Queue. This should not happen\n", __FUNCTION__);
                assert(0); /* TODO double check if NULL is allowed, i.e. notification arrives before blocking */
            }
        *stored = removed;
    }

    return (0);
}


/*****************Context_id Leader Hashtable*************************/

static unsigned long contextidLeaderhash(void *item)
{
    cLitemptr cL_ptr = (cLitemptr) item;

    return (cL_ptr->cLkey.context_id + cL_ptr->cLkey.LeaderWorldRank);
} /* hash */


static unsigned long contextidLeaderrehash(void *item)
{
    cLitemptr cL_ptr = (cLitemptr) item;

    return (cL_ptr->cLkey.context_id + cL_ptr->cLkey.LeaderWorldRank)>>3;
} /* rehash */


static int  contextidLeadercmp(void *litem, void *ritem)
{
   cLitemptr cL_lptr = (cLitemptr) litem,
             cL_rptr = (cLitemptr) ritem;
   if     ((cL_lptr->cLkey.context_id == cL_rptr->cLkey.context_id) && (cL_lptr->cLkey.LeaderWorldRank == cL_rptr->cLkey.LeaderWorldRank)) return 0;
   else if (cL_lptr->cLkey.context_id > cL_rptr->cLkey.context_id) return 1;
   else                                      return -1;
} /* cmp */


static void *contextidLeaderdupe(void *item)
{
   cLitemptr cL_ptr = (cLitemptr) item,
             cL_dupe;

   if ((cL_dupe = (cLitemptr) malloc(sizeof *cL_dupe))){
       *cL_dupe = *cL_ptr;
   }else{
       printf("Cannot allocate memory in %s\n", __FUNCTION__);
       exit(-1);
   }
   return cL_dupe;
} /* dupe */


static void contextidLeaderundupe(void *item)
{
    cLitemptr cL_ptr = (cLitemptr) item;
    free(cL_ptr);
} /* undupe */


inline hshtbl * CL_LookupHashCreate(void)
{
    hshtbl   *h;
    h = hshinit(contextidLeaderhash,             /* hash function */
                contextidLeaderrehash,           /* rehash function */
                contextidLeadercmp,              /* compare function */
                contextidLeaderdupe,             /* dupe function */
                contextidLeaderundupe,           /* hshfree function */
                0);                        /* use debug output */
    return h;

}

static void print_cLItem(cLitemptr p)
{
    printf("context-id=%d, LeaderWorldRank=%d, coproclet_shared_vars=%p\n", p->cLkey.context_id, p->cLkey.LeaderWorldRank, p->coproclet_shared_vars);

}

inline int CL_LookupHashFind(hshtbl *CL_hshtbl, int context_id, int LeaderWorldRank, cLitemptr *stored) /* IN, IN, IN, OUT */
{
    cLitem CL_item;
    *stored = NULL;
    CL_item.cLkey.context_id = context_id;
    CL_item.cLkey.LeaderWorldRank = LeaderWorldRank;
    *stored = (cLitemptr)hshfind(CL_hshtbl, &CL_item);
    return (0);
}

inline int CL_LookupHashInsert(hshtbl *CL_hshtbl, int context_id, int LeaderWorldRank, void* co_shared_vars, cLitemptr *stored) /* IN, IN, IN, IN, OUT */
{
    cLitem CL_item;
    *stored = NULL;
    CL_item.cLkey.context_id = context_id;
    CL_item.cLkey.LeaderWorldRank = LeaderWorldRank;
    CL_item.coproclet_shared_vars = co_shared_vars;
    *stored = (cLitemptr)hshinsert(CL_hshtbl, &CL_item);
    return (0);
}

inline int CL_DeleteHashEntry(hshtbl *CL_hshtbl, cLitemptr stored)
{
    cLitemptr removed = NULL;
    removed = hshdelete(CL_hshtbl, stored);
    assert(removed != NULL);
    free(removed); /* Need to call free(removed) here as hshdelete does not free user data */
    return (0);
}

/*****************Context_id Lookup Hashtable*************************/

static unsigned long cidLookupHash(void *item)
{
    cidLookupHashItemptr c_ptr = (cidLookupHashItemptr) item;

    return (c_ptr->context_id);
} /* hash */


static unsigned long cidLookupRehash(void *item)
{
    cidLookupHashItemptr c_ptr = (cidLookupHashItemptr) item;

    return (c_ptr->context_id)>>3;
} /* rehash */


static int cidLookupCmp(void *litem, void *ritem)
{
    cidLookupHashItemptr c_lptr = (cidLookupHashItemptr) litem,
        c_rptr = (cidLookupHashItemptr)ritem;

   if     ((c_lptr->context_id == c_rptr->context_id)) return 0;
   else if (c_lptr->context_id > c_rptr->context_id)   return 1;
   else                                                return -1;
} /* cmp */


static void *cidLookupDupe(void *item)
{
    cidLookupHashItemptr c_ptr = (cidLookupHashItemptr) item,
                        c_dupe;

   if ((c_dupe = (cidLookupHashItemptr) malloc(sizeof *c_dupe))){
       *c_dupe = *c_ptr;
   }else{
       printf("Cannot allocate memory in %s\n", __FUNCTION__);
       exit(-1);
   }
   return c_dupe;
} /* dupe */


static void cidLookupUndupe(void *item)
{
    cidLookupHashItemptr c_ptr = (cidLookupHashItemptr) item;
    free(c_ptr);
} /* undupe */


inline hshtbl * CidLookupHashCreate(void)
{
    hshtbl   *h;
    h = hshinit(cidLookupHash,             /* hash function */
                cidLookupRehash,           /* rehash function */
                cidLookupCmp,              /* compare function */
                cidLookupDupe,             /* dupe function */
                cidLookupUndupe,           /* hshfree function */
                0);                        /* use debug output */
    return h;

}

static void print_cidLookupHashItem(cidLookupHashItemptr p)
{
    printf("context-id=%d, coproclet_shared_vars=%p\n", p->context_id, p->coproclet_shared_vars);
}

inline int CidLookupHashFind(hshtbl *cid_lookuphshtbl, int cid, cidLookupHashItemptr *stored) /*(IN, IN, OUT)*/
{
     cidLookupHashItem c_item;
     *stored = NULL;
     c_item.context_id = cid;
     *stored = (cidLookupHashItemptr)hshfind(cid_lookuphshtbl, &c_item);
     return(0);
}

inline int CidLookupHashInsert(hshtbl *cid_lookuphshtbl, int cid, void* co_shared_vars, cidLookupHashItemptr *stored) /* IN, IN, IN, OUT */
{
    cidLookupHashItem c_item;
    *stored = NULL;
    c_item.context_id = cid;
    c_item.coproclet_shared_vars = co_shared_vars;
    *stored = (cidLookupHashItemptr)hshinsert(cid_lookuphshtbl, &c_item);
    return (0);
}


/*****************ptn Lookup Hashtable*************************/


static unsigned long ptnLookupHash(void *item)
{
    ptnLookupHashItemptr p_ptr = (ptnLookupHashItemptr) item;

    return (p_ptr->parent_comm_rank);
} /* hash */


static unsigned long ptnLookupRehash(void *item)
{
    ptnLookupHashItemptr p_ptr = (ptnLookupHashItemptr) item;

    return (p_ptr->parent_comm_rank)>>3;
} /* rehash */


static int ptnLookupCmp(void *litem, void *ritem)
{
    ptnLookupHashItemptr p_lptr = (ptnLookupHashItemptr) litem,
        p_rptr = (ptnLookupHashItemptr)ritem;

   if     ((p_lptr->parent_comm_rank == p_rptr->parent_comm_rank)) return  0;
   else if (p_lptr->parent_comm_rank > p_rptr->parent_comm_rank)   return  1;
   else                                                            return -1;
} /* cmp */


static void *ptnLookupDupe(void *item)
{
    ptnLookupHashItemptr p_ptr = (ptnLookupHashItemptr) item,
                         p_dupe;

    if ((p_dupe = (ptnLookupHashItemptr) malloc(sizeof *p_dupe))){
       *p_dupe = *p_ptr;
   }else{
       printf("Cannot allocate memory in %s\n", __FUNCTION__);
       exit(-1);
   }
   return p_dupe;
} /* dupe */


static void ptnLookupUndupe(void *item)
{
    ptnLookupHashItemptr p_ptr = (ptnLookupHashItemptr) item;
    free(p_ptr);
} /* undupe */


inline hshtbl * ptnLookupHashCreate(void)
{
    hshtbl   *h;
    h = hshinit(ptnLookupHash,             /* hash function */
                ptnLookupRehash,           /* rehash function */
                ptnLookupCmp,              /* compare function */
                ptnLookupDupe,             /* dupe function */
                ptnLookupUndupe,           /* hshfree function */
                0);                        /* use debug output */
    return h;

}

static void print_ptnLookupHashItem(ptnLookupHashItemptr p)
{
    printf("parent_comm_rank=%d\n", p->parent_comm_rank);
}


inline int ptnLookupHashFind(hshtbl *ptn_lookuphshtbl, int parent_rank, ptnLookupHashItemptr *stored) /*(IN, IN, OUT)*/
{
    ptnLookupHashItem p_item;
    *stored = NULL;
    p_item.parent_comm_rank = parent_rank;
    *stored = (ptnLookupHashItemptr)hshfind(ptn_lookuphshtbl, &p_item);
    return(0);
}

inline int ptnLookupHashInsert(hshtbl *ptn_lookuphshtbl, int parent_rank, Parent_to_Nested_comm_tables_t parent_to_nested, ptnLookupHashItemptr *stored) /* IN, IN, IN, OUT */
{
    ptnLookupHashItem p_item;
    *stored = NULL;
    p_item.parent_comm_rank = parent_rank;
    p_item.parent_to_nested.internode_comm_root = parent_to_nested.internode_comm_root;
    p_item.parent_to_nested.internode_comm_external_rank = parent_to_nested.internode_comm_external_rank;
    p_item.parent_to_nested.intranode_comm_root = parent_to_nested.intranode_comm_root;
    p_item.parent_to_nested.intranode_comm_local_rank = parent_to_nested.intranode_comm_local_rank;
    p_item.parent_to_nested.intra_osproc_fg_rank = parent_to_nested.intra_osproc_fg_rank;
    *stored = (ptnLookupHashItemptr)hshinsert(ptn_lookuphshtbl, &p_item);
    return (0);
}


inline int hshtblFree(hshtbl **hash_dptr) /*(IN)*/
{
    hshkill(*hash_dptr);
    *hash_dptr = NULL;
}

