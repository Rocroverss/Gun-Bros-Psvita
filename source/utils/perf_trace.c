#include "utils/perf_trace.h"

#ifdef GUNBROS_ENABLE_PERF_TRACE
#include <psp2/kernel/processmgr.h>
#include <pthread.h>
#include <string.h>

typedef struct PerfStage {
    uint64_t calls, total_us, max_us, bytes;
} PerfStage;

static PerfStage stages[GUNBROS_STAGE_COUNT];
static pthread_mutex_t stage_lock = PTHREAD_MUTEX_INITIALIZER;
static const char *const stage_names[GUNBROS_STAGE_COUNT] = {
    "resource", "image", "object", "read", "audio-wait", "audio-decode",
    "load-menus", "load-mission", "set-menu", "stack-load", "push-menu", "pop-menu",
    "asset-open", "pack-bind", "audio-file", "audio-hash",
    "registry-lookup", "resource-ref", "menu-provider", "menu-bind",
    "texture-upload", "inflate", "seek"
};

uint64_t gunbros_perf_now(void) {
    return sceKernelGetProcessTimeWide();
}

uint64_t gunbros_perf_menu_begin(int stage) {
    uint64_t begin = gunbros_perf_now();
    if (stage >= GUNBROS_STAGE_LOAD_MENUS && stage <= GUNBROS_STAGE_POP_MENU) {
        GUNBROS_PERF_LOG("[PERF-MENU-BEGIN] stage=%s begin_us=%llu\n",
            stage_names[stage], (unsigned long long)begin);
        gunbros_perf_log_flush();
    }
    return gunbros_perf_now(); /* Exclude the pre-call file flush. */
}

void gunbros_perf_stage_record(int stage, uint64_t begin, uint64_t bytes) {
    uint64_t end = gunbros_perf_now();
    if (stage < 0 || stage >= GUNBROS_STAGE_COUNT || end < begin) return;
    uint64_t elapsed = end - begin;
    pthread_mutex_lock(&stage_lock);
    PerfStage *s = &stages[stage];
    ++s->calls;
    s->total_us += elapsed;
    s->bytes += bytes;
    if (elapsed > s->max_us) s->max_us = elapsed;
    pthread_mutex_unlock(&stage_lock);
    if (stage >= GUNBROS_STAGE_LOAD_MENUS && stage <= GUNBROS_STAGE_POP_MENU) {
        GUNBROS_PERF_LOG("[PERF-MENU-CALL] stage=%s begin_us=%llu end_us=%llu elapsed_us=%llu\n",
            stage_names[stage], (unsigned long long)begin,
            (unsigned long long)end, (unsigned long long)elapsed);
        gunbros_perf_log_flush();
    }
}

void gunbros_perf_dump_stages(const char *reason) {
    PerfStage snapshot[GUNBROS_STAGE_COUNT];
    pthread_mutex_lock(&stage_lock);
    memcpy(snapshot, stages, sizeof(stages));
    memset(stages, 0, sizeof(stages));
    pthread_mutex_unlock(&stage_lock);
    /* Never hold the stats lock while writing to the debug transport. */
    for (int i = 0; i < GUNBROS_STAGE_COUNT; ++i) {
        const PerfStage *s = &snapshot[i];
        if (!s->calls) continue;
        GUNBROS_PERF_LOG("[PERF-STAGE] reason=%s stage=%s calls=%llu total_us=%llu max_us=%llu bytes=%llu\n",
            reason, stage_names[i], (unsigned long long)s->calls,
            (unsigned long long)s->total_us, (unsigned long long)s->max_us,
            (unsigned long long)s->bytes);
    }
    gunbros_perf_log_memory(reason);
    gunbros_perf_log_flush();
}
#endif
