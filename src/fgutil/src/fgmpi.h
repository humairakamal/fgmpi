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

#ifdef  __cplusplus
}
#endif

#endif /* FGMPI_H */
