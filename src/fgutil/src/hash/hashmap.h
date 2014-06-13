#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdio.h>
#include <stdlib.h>
#include "hashlib.h"
#include "cokusmt.h"


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
extern inline int RTWarrayBlockInsert(int *rtw_arraymap, int size, int *blockarray); /*(IN, IN, IN)*/
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
extern inline int CL_LookupHashInsert(hshtbl *CL_hshtbl, int context_id, int LeaderWorldRank, void* coproclet_shared_vars, cLitemptr *stored); /* IN, IN, IN, IN, IN, OUT */


typedef struct contextidLookupHash{
    int context_id; /* Key for looking up the shared group map associated with a
                                        communciator's context-id */
    void * coproclet_shared_vars;  /*shared rtw map for the {context-id, Leader-world-rank} combination
                                     and  barrier variables for the {context-id, Leader-world-rank} combination
                                   */
} cidLookupHashItem, *cidLookupHashItemptr;


extern inline hshtbl * CidLookupHashCreate(void);
extern inline int CidLookupHashFind(hshtbl *cid_lookuphshtbl, int cid, cidLookupHashItemptr *stored); /*(IN, IN, OUT)*/
extern inline int CidLookupHashInsert(hshtbl *cid_lookuphshtbl, int cid, void* coproclet_shared_vars, cidLookupHashItemptr *stored); /* IN, IN, IN, IN, OUT */


#endif /* HASHMAP_H */
