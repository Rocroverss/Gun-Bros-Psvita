/*
 * Release logging policy.
 *
 * This header is force-included by CMake when GUNBROS_ENABLE_LOGS is OFF.
 * Include the Vita declaration before replacing the call so later includes
 * cannot have their function prototypes damaged by the macro.
 */
#ifndef GUNBROS_QUIET_LOG_H
#define GUNBROS_QUIET_LOG_H

#include <psp2/kernel/clib.h>

#ifdef GUNBROS_QUIET_LOGS
#undef GUNBROS_ENABLE_PERF_TRACE
#undef DEBUG_SOLOADER
/* A function-like macro also removes evaluation of expensive log arguments. */
#define sceClibPrintf(...) ((void)0)
#endif

#endif /* GUNBROS_QUIET_LOG_H */
