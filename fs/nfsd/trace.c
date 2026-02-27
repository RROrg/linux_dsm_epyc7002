#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif

#define CREATE_TRACE_POINTS
#include "trace.h"

#ifdef MY_ABC_HERE
EXPORT_TRACEPOINT_SYMBOL(syno_nfsd4_dispatch);
EXPORT_TRACEPOINT_SYMBOL(syno_nfsd_dispatch);
#endif /* MY_ABC_HERE */
