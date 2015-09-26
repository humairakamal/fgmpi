/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*  
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Function prototypes for communicator helper functions */
/* The MPIR_Get_contextid and void MPIR_Free_contextid routines are in
   mpiimpl.h so that the device may use them */
/* int MPIR_Get_contextid( MPID_Comm *, MPIR_Context_id_t * ); */
int MPIR_Get_intercomm_contextid( MPID_Comm *, MPIR_Context_id_t *, MPIR_Context_id_t * );
/* void MPIR_Free_contextid( MPIR_Context_id_t ); */

#if 0//defined(FINEGRAIN_MPI) /* FG: TODO TBR Move this to fgmpicomm.h?
                              and function definitions in fgcommutil.c
                              FG: TODO IMPORTANT lowestTag in commutil.c - Check
                              possibility not support MPICH native contextid algo?
                           */
/* FG: TODO IMPORTANT fix this.
   Following variables like initialize_context_mask, mask_in_use etc., are defined are static global in src/mpi/comm/commutil.c
   Ideally, I would like them to be only visible to that file, however, I am defining them here because they require
   initializors other than zero, and initialization is being done in MPI_Init().
   Direct assignment like this, for example, in MPI_Init:
   (((struct StateWrapper*)(CO_CURRENT->statevars))->  initialize_context_maskFG) = 1 will result in an lvalue error,
   therefore, they are being defined here in mpiimpl.h and then initialized as the following in MPI_Init():
   initialize_context_mask = 1;
 */

#define context_mask (((struct StateWrapper*)(CO_CURRENT->statevars))-> context_maskFG)
#define initialize_context_mask (((struct StateWrapper*)(CO_CURRENT->statevars))-> initialize_context_maskFG)
#define mask_in_use (((struct StateWrapper*)(CO_CURRENT->statevars))-> mask_in_useFG)
#define lowestContextId (((struct StateWrapper*)(CO_CURRENT->statevars))-> lowestContextIdFG)

#define LBI_mask (((struct StateWrapper*)(CO_CURRENT->statevars))-> LBI_maskFG)
#define initialize_LBI_mask (((struct StateWrapper*)(CO_CURRENT->statevars))-> initialize_LBI_maskFG)


#define USE_LID_LBI_CONTEXT
#define MAX_LBI_MASK 2 /* k=6, 2^k = 64 bits. One bit each for the times a proclet can be leader of a group */
#define MPIR_CONTEXT_INT_BITS ((sizeof(int)*8)) /* FG: Note: MPIR_Find_LBI_bit assumes a 32-bit integer */
static const unsigned char BitsSetTable256[256] =
{
#   define B2(n) n,     n+1,     n+1,     n+2
#   define B4(n) B2(n), B2(n+1), B2(n+1), B2(n+2)
#   define B6(n) B4(n), B4(n+1), B4(n+1), B4(n+2)
    B6(0), B6(1), B6(1), B6(2)
};
extern void MPIR_Get_LBI_count(unsigned int *lbi_mask, int *lbi_count);
extern int MPIR_Find_LBI_bit( unsigned int *lbi_mask );
extern int MPIR_Get_count_LBI_sndval(unsigned int *lbi_mask);
extern void MPIR_Separate_count_LBI(int lbi_count_sndval, short int* lbi_bit, short int* lbi_count);
extern int MPIR_Get_CID(int LID, short int maxLBI);
extern void MPIR_Separate_CID_into_LID_LBI(int cid, int* LID_ptr, short int* lbi_ptr);
extern void MPIR_Unsetbit_in_LBI_mask(unsigned int *lbi_mask, short int chosen_lbi_for_cid);
extern void MPIR_Setbit_in_LBI_mask(unsigned int *lbi_mask, short int chosen_lbi_for_cid);
extern void MPIR_Intra_newcomm_init(MPID_Comm *newcomm_ptr, int pid_rank, int new_totprocs);

#endif
