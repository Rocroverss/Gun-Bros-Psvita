#ifndef GUNBROS_PERF_TRACE_H
#define GUNBROS_PERF_TRACE_H

#include <psp2/kernel/clib.h>
#include <stdint.h>

enum GunBrosPerfStage {
    GUNBROS_STAGE_RESOURCE, GUNBROS_STAGE_IMAGE, GUNBROS_STAGE_OBJECT,
    GUNBROS_STAGE_READ, GUNBROS_STAGE_AUDIO_WAIT, GUNBROS_STAGE_AUDIO_DECODE,
    GUNBROS_STAGE_LOAD_MENUS, GUNBROS_STAGE_LOAD_MISSION,
    GUNBROS_STAGE_SET_MENU, GUNBROS_STAGE_STACK_LOAD,
    GUNBROS_STAGE_PUSH_MENU, GUNBROS_STAGE_POP_MENU,
    GUNBROS_STAGE_ASSET_OPEN, GUNBROS_STAGE_PACK_BIND,
    GUNBROS_STAGE_AUDIO_FILE, GUNBROS_STAGE_AUDIO_HASH,
    GUNBROS_STAGE_REGISTRY_LOOKUP, GUNBROS_STAGE_RESOURCE_REF,
    GUNBROS_STAGE_MENU_PROVIDER, GUNBROS_STAGE_MENU_BIND,
    GUNBROS_STAGE_TEXTURE_UPLOAD, GUNBROS_STAGE_INFLATE,
    GUNBROS_STAGE_SEEK,
    GUNBROS_STAGE_COUNT
};

#ifdef __cplusplus
extern "C" {
#endif
#ifdef GUNBROS_ENABLE_PERF_TRACE
uint64_t gunbros_perf_now(void);
void gunbros_perf_stage_record(int stage, uint64_t begin, uint64_t bytes);
void gunbros_perf_dump_stages(const char *reason);
void gunbros_perf_log(const char *format, ...);
void gunbros_perf_log_flush(void);
void gunbros_perf_log_memory(const char *reason);
uint64_t gunbros_perf_menu_begin(int stage);
#define GUNBROS_PERF_MENU_BEGIN(stage, name) uint64_t name = gunbros_perf_menu_begin(stage)
#define GUNBROS_PERF_BEGIN(name) uint64_t name = gunbros_perf_now()
#define GUNBROS_PERF_END(stage, name, bytes) gunbros_perf_stage_record(stage, name, bytes)
#else
#define GUNBROS_PERF_BEGIN(name) ((void)0)
#define GUNBROS_PERF_END(stage, name, bytes) ((void)0)
#define gunbros_perf_dump_stages(reason) ((void)0)
#define gunbros_perf_log_flush() ((void)0)
#define gunbros_perf_log_memory(reason) ((void)0)
#define GUNBROS_PERF_MENU_BEGIN(stage, name) ((void)0)
#endif
#ifdef __cplusplus
}
#endif

enum GunBrosPerfScene {
    GUNBROS_PERF_SCENE_BOOT = 0,
    GUNBROS_PERF_SCENE_MENU = 1,
    GUNBROS_PERF_SCENE_SHOP = 2,
    GUNBROS_PERF_SCENE_REFINERY = 3,
    GUNBROS_PERF_SCENE_GAMEPLAY = 4,
};

#ifdef GUNBROS_ENABLE_PERF_TRACE
#define GUNBROS_PERF_LOG(...) gunbros_perf_log(__VA_ARGS__)
#else
#define GUNBROS_PERF_LOG(...) ((void)0)
#endif

#ifdef GUNBROS_ENABLE_PERF_TRACE
void gunbros_perf_set_context(int scene, int screen, int branch, const char *source);
#else
#define gunbros_perf_set_context(scene, screen, branch, source) ((void)0)
#endif

extern volatile unsigned int g_gunbros_perf_sprite_draw_calls;
extern volatile unsigned int g_gunbros_perf_store_query_calls;
extern volatile unsigned int g_gunbros_perf_store_query_cache_hits;
extern volatile unsigned int g_gunbros_perf_store_touch_calls;
extern volatile unsigned int g_gunbros_perf_xp_query_calls;
extern volatile unsigned int g_gunbros_perf_refinery_refresh_calls;
extern volatile unsigned int g_gunbros_perf_refinery_refresh_runs;
extern volatile unsigned int g_gunbros_perf_resource_load_next_calls;
extern volatile unsigned int g_gunbros_perf_image_load_calls;
extern volatile unsigned int g_gunbros_perf_game_object_init_calls;
extern volatile unsigned int g_gunbros_perf_store_filtered_count_calls;
extern volatile unsigned int g_gunbros_perf_store_level_lock_calls;
extern volatile unsigned int g_gunbros_perf_store_status_calls;
extern volatile unsigned int g_gunbros_perf_store_cost_string_calls;
extern volatile unsigned int g_gunbros_perf_store_sale_string_calls;

#ifdef GUNBROS_ENABLE_PERF_TRACE
#define GUNBROS_PERF_COUNT(name) (g_gunbros_perf_##name++)
#else
#define GUNBROS_PERF_COUNT(name) ((void)0)
#endif

#endif /* GUNBROS_PERF_TRACE_H */
