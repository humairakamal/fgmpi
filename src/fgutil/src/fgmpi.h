/*
 *  (C) 2008 Humaira Kamal, The University of British Columbia.
 *      See COPYRIGHT in top-level directory.
 */


#ifndef FGMPI_H
#define FGMPI_H

#ifdef  __cplusplus
extern "C" {
#endif

#define MAP_INIT_ACTION -1
#define MAP_FINALIZE_ACTION -2

typedef int PROCESS;

/* FG_FunctionPtr_t will be used as a type. NOTE: This definition must not change. */
typedef int (*FG_ProcessPtr_t)( int argc, char** argv );
typedef FG_ProcessPtr_t (*FG_MapPtr_t)(int argc, char** argv, int);
typedef FG_MapPtr_t (*LookupMapPtr_t)(int argc, char** argv, char* str);

extern int FGmpiexec( int *argc, char ***argv, LookupMapPtr_t lookupFuncPtr );

int MPIX_Get_collocated_startrank(int * rank);
int MPIX_Get_collocated_size(int * size);
int MPIX_Get_n_size(int * size);
void MPIX_Yield(void);
void MPIX_Usleep(unsigned long long utime);


#ifdef  __cplusplus
}
#endif

#endif /* FGMPI_H */
