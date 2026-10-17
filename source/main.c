#include "utils/glutil.h"
#include "utils/init.h"
#include "utils/classic_dialog.h"
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include "utils/data_check.h"
#include "utils/perf_trace.h"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <falso_jni/FalsoJNI.h>
#include <falso_jni/jni.h>
#include <so_util/so_util.h>

#include "reimpl/controls.h"
#include "audio.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENV ((JNIEnv *)&jni)
#define GUNBROS_LOADER_BUILD "2026-09-08-launcher-classic-62"
#ifndef GUNBROS_INPUT_TRACE
#define GUNBROS_INPUT_TRACE 0
#endif
#ifndef GUNBROS_FRAME_TRACE
#define GUNBROS_FRAME_TRACE 0
#endif

so_module so_mod;

typedef struct GunBrosGameplayInputState {
    int move_active;
    int fire_active;
    float move_x;
    float move_y;
    float fire_x;
    float fire_y;
    int move_source; /* 1=physical touch, 2=analog/synthetic */
    int fire_source; /* 1=physical touch, 2=analog/synthetic */
    unsigned int seq;
} GunBrosGameplayInputState;

volatile GunBrosGameplayInputState g_gunbros_gameplay_input;
volatile uint32_t g_gunbros_render_frame;
float g_gunbros_gameplay_speed = 1.0f;
unsigned int g_gunbros_gameplay_max_delta_ms = 33u;
extern void gunbros_request_pause_menu(void);
extern void gunbros_request_player_gun_swap(void);
extern void soloader_init_all(void);
extern int gunbros_tick_rate_ms;
extern int gunbros_finish_requested;
extern void gunbros_java_init(void);
extern void gunbros_preload_offline_profile(void);
extern void gunbros_flush_offline_profile(void);
extern void gunbros_flush_offline_refinery(void);

typedef struct GunBrosPerfWindow {
    uint64_t start_us;
    uint64_t work_total_us;
    uint64_t native_total_us;
    uint64_t swap_total_us;
    uint64_t work_max_us;
    uint64_t native_max_us;
    uint64_t swap_max_us;
    uint32_t frames;
    uint32_t over_16ms;
    uint32_t over_25ms;
    uint32_t over_33ms;
    uint32_t context_serial;
} GunBrosPerfWindow;

static volatile int g_gunbros_perf_scene = GUNBROS_PERF_SCENE_BOOT;
static volatile int g_gunbros_perf_screen = -1;
static volatile int g_gunbros_perf_branch = -1;
static volatile uint32_t g_gunbros_perf_context_serial = 1u;
volatile unsigned int g_gunbros_perf_sprite_draw_calls;
volatile unsigned int g_gunbros_perf_store_query_calls;
volatile unsigned int g_gunbros_perf_store_query_cache_hits;
volatile unsigned int g_gunbros_perf_store_touch_calls;
volatile unsigned int g_gunbros_perf_xp_query_calls;
volatile unsigned int g_gunbros_perf_refinery_refresh_calls;
volatile unsigned int g_gunbros_perf_refinery_refresh_runs;
volatile unsigned int g_gunbros_perf_resource_load_next_calls;
volatile unsigned int g_gunbros_perf_image_load_calls;
volatile unsigned int g_gunbros_perf_game_object_init_calls;
volatile unsigned int g_gunbros_perf_store_filtered_count_calls;
volatile unsigned int g_gunbros_perf_store_level_lock_calls;
volatile unsigned int g_gunbros_perf_store_status_calls;
volatile unsigned int g_gunbros_perf_store_cost_string_calls;
volatile unsigned int g_gunbros_perf_store_sale_string_calls;

static void gunbros_perf_reset_call_counters(void) {
    g_gunbros_perf_sprite_draw_calls = 0u;
    g_gunbros_perf_store_query_calls = 0u;
    g_gunbros_perf_store_query_cache_hits = 0u;
    g_gunbros_perf_store_touch_calls = 0u;
    g_gunbros_perf_xp_query_calls = 0u;
    g_gunbros_perf_refinery_refresh_calls = 0u;
    g_gunbros_perf_refinery_refresh_runs = 0u;
    g_gunbros_perf_resource_load_next_calls = 0u;
    g_gunbros_perf_image_load_calls = 0u;
    g_gunbros_perf_game_object_init_calls = 0u;
    g_gunbros_perf_store_filtered_count_calls = 0u;
    g_gunbros_perf_store_level_lock_calls = 0u;
    g_gunbros_perf_store_status_calls = 0u;
    g_gunbros_perf_store_cost_string_calls = 0u;
    g_gunbros_perf_store_sale_string_calls = 0u;
}

static const char *gunbros_perf_scene_name(int scene) {
    switch (scene) {
    case GUNBROS_PERF_SCENE_MENU:
        return "menu";
    case GUNBROS_PERF_SCENE_SHOP:
        return "shop";
    case GUNBROS_PERF_SCENE_REFINERY:
        return "refinery";
    case GUNBROS_PERF_SCENE_GAMEPLAY:
        return "gameplay";
    default:
        return "boot";
    }
}

void gunbros_perf_set_context(int scene, int screen, int branch,
                              const char *source) {
    if (scene < GUNBROS_PERF_SCENE_BOOT ||
        scene > GUNBROS_PERF_SCENE_GAMEPLAY) {
        scene = GUNBROS_PERF_SCENE_MENU;
    }
    if (g_gunbros_perf_scene == scene &&
        g_gunbros_perf_screen == screen &&
        g_gunbros_perf_branch == branch) {
        return;
    }

    g_gunbros_perf_scene = scene;
    g_gunbros_perf_screen = screen;
    g_gunbros_perf_branch = branch;
    g_gunbros_perf_context_serial++;
    gunbros_perf_reset_call_counters();
    GUNBROS_PERF_LOG("[PERF-CONTEXT] build=%s scene=%s screen=%d branch=%d source=%s\n",
                     GUNBROS_LOADER_BUILD,
                     gunbros_perf_scene_name(scene), screen, branch,
                     source ? source : "?");
}

static void gunbros_perf_record_frame(uint64_t frame_begin_us,
                                      uint64_t native_begin_us,
                                      uint64_t native_end_us,
                                      uint64_t frame_end_us) {
#ifdef GUNBROS_ENABLE_PERF_TRACE
    static GunBrosPerfWindow window;
    uint64_t work_us;
    uint64_t native_us;
    uint64_t swap_us;
    uint64_t elapsed_us;
    uint64_t fps_x10;
    uint32_t serial = g_gunbros_perf_context_serial;

    if (frame_end_us < frame_begin_us ||
        native_begin_us < frame_begin_us ||
        native_end_us < native_begin_us ||
        frame_end_us < native_end_us) {
        return;
    }
    if (window.context_serial != serial) {
        memset(&window, 0, sizeof(window));
        window.start_us = frame_begin_us;
        window.context_serial = serial;
    }

    work_us = frame_end_us - frame_begin_us;
    native_us = native_end_us - native_begin_us;
    swap_us = frame_end_us - native_end_us;
    window.work_total_us += work_us;
    window.native_total_us += native_us;
    window.swap_total_us += swap_us;
    if (work_us > window.work_max_us) {
        window.work_max_us = work_us;
    }
    if (native_us > window.native_max_us) {
        window.native_max_us = native_us;
    }
    if (swap_us > window.swap_max_us) {
        window.swap_max_us = swap_us;
    }
    window.over_16ms += work_us >= 16667u;
    window.over_25ms += work_us >= 25000u;
    window.over_33ms += work_us >= 33333u;
    window.frames++;

    if (window.frames < 120u) {
        return;
    }
    elapsed_us = frame_end_us - window.start_us;
    if (elapsed_us == 0u) {
        return;
    }
    fps_x10 = (uint64_t)window.frames * 10000000u / elapsed_us;
    GUNBROS_PERF_LOG("[PERF-MENU] build=%s scene=%s screen=%d branch=%d frames=%u fps=%llu.%llu work_avg_us=%llu work_max_us=%llu native_avg_us=%llu native_max_us=%llu swap_avg_us=%llu swap_max_us=%llu over16=%u over25=%u over33=%u calls(sprite=%u store_query=%u/%u store_touch=%u xp=%u refinery_refresh=%u/%u load=%u image=%u gobj_init=%u store_hot=%u/%u/%u/%u/%u)\n",
                     GUNBROS_LOADER_BUILD,
                     gunbros_perf_scene_name(g_gunbros_perf_scene),
                     g_gunbros_perf_screen, g_gunbros_perf_branch,
                     window.frames,
                     (unsigned long long)(fps_x10 / 10u),
                     (unsigned long long)(fps_x10 % 10u),
                     (unsigned long long)(window.work_total_us / window.frames),
                     (unsigned long long)window.work_max_us,
                     (unsigned long long)(window.native_total_us / window.frames),
                     (unsigned long long)window.native_max_us,
                     (unsigned long long)(window.swap_total_us / window.frames),
                     (unsigned long long)window.swap_max_us,
                     window.over_16ms, window.over_25ms, window.over_33ms,
                     g_gunbros_perf_sprite_draw_calls,
                     g_gunbros_perf_store_query_calls,
                     g_gunbros_perf_store_query_cache_hits,
                     g_gunbros_perf_store_touch_calls,
                     g_gunbros_perf_xp_query_calls,
                     g_gunbros_perf_refinery_refresh_calls,
                     g_gunbros_perf_refinery_refresh_runs,
                     g_gunbros_perf_resource_load_next_calls,
                     g_gunbros_perf_image_load_calls,
                     g_gunbros_perf_game_object_init_calls,
                     g_gunbros_perf_store_filtered_count_calls,
                     g_gunbros_perf_store_level_lock_calls,
                     g_gunbros_perf_store_status_calls,
                     g_gunbros_perf_store_cost_string_calls,
                     g_gunbros_perf_store_sale_string_calls);

    memset(&window, 0, sizeof(window));
    window.start_us = frame_end_us;
    window.context_serial = serial;
    gunbros_perf_reset_call_counters();
#else
    (void)frame_begin_us;
    (void)native_begin_us;
    (void)native_end_us;
    (void)frame_end_us;
#endif
}

typedef struct GunBrosSpeedConfig {
    float speed;
    unsigned int max_delta_ms;
} GunBrosSpeedConfig;

static const char g_gunbros_default_speed_file[] =
    "# Gun Bros Vita internal game-logic clock\n"
    "# Restart the game after changing this file.\n"
    "# 1.0 = original speed, 0.5 = half speed, 1.25 = 25% faster.\n"
    "speed=1.0\n"
    "# Caps one gameplay update after a loading/render stall. 33 is recommended.\n"
    "# This setting does not change menus, the shop, or refinery timing.\n"
    "max_delta_ms=33\n";

static GunBrosSpeedConfig gunbros_load_speed_config(void) {
    static const char path[] = DATA_PATH "speed.txt";
    GunBrosSpeedConfig config = { 1.0f, 33u };
    FILE *fp = fopen(path, "rb");
    char line[160];

    if (!fp) {
        fp = fopen(path, "wb");
        if (fp) {
            (void)fwrite(g_gunbros_default_speed_file, 1,
                         sizeof(g_gunbros_default_speed_file) - 1u, fp);
            (void)fclose(fp);
        }
        return config;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *text = line;
        float speed;
        unsigned int max_delta;

        while (*text == ' ' || *text == '\t') {
            ++text;
        }
        if (*text == '\0' || *text == '\r' || *text == '\n' ||
            *text == '#' || *text == ';') {
            continue;
        }

        if (sscanf(text, "speed = %f", &speed) == 1 ||
            sscanf(text, "speed=%f", &speed) == 1) {
            if (speed >= 0.05f && speed <= 4.0f) {
                config.speed = speed;
            }
            continue;
        }
        if (sscanf(text, "max_delta_ms = %u", &max_delta) == 1 ||
            sscanf(text, "max_delta_ms=%u", &max_delta) == 1) {
            if (max_delta >= 16u && max_delta <= 250u) {
                config.max_delta_ms = max_delta;
            }
            continue;
        }
        /* A file containing only a number remains a convenient shorthand. */
        if (sscanf(text, "%f", &speed) == 1 &&
            speed >= 0.05f && speed <= 4.0f) {
            config.speed = speed;
        }
    }
    (void)fclose(fp);
    return config;
}

static inline void dump_fn(const char *name, void *raw, void *callable, const char *mode) {
    sceClibPrintf("  %-16s raw=%p -> call=%p [%s]\n", name, raw, callable, mode);
}

static void *autoselect_arm_thumb(void *raw, const char **mode) {
    uintptr_t p;
    uint16_t h;
    uint32_t w;
    int looks_thumb;
    int looks_arm;

    if (!raw) {
        *mode = "null";
        return NULL;
    }

    p = (uintptr_t)raw & ~(uintptr_t)1;
    h = *(volatile uint16_t *)p;
    w = *(volatile uint32_t *)p;
    looks_thumb = ((h & 0xFF00) == 0xB500) || ((h & 0xFF00) == 0xB000) || ((h & 0xF800) == 0xE800);
    looks_arm = ((w & 0xFFFF0000) == 0xE92D0000) || ((w & 0xFFFFF000) == 0xE24DD000);

    if (looks_thumb && !looks_arm) {
        *mode = "Thumb";
        return (void *)(p | 1);
    }

    if (looks_arm && !looks_thumb) {
        *mode = "ARM";
        return (void *)p;
    }

    *mode = "ARM?";
    return (void *)p;
}

static inline void dump_prologue(const char *tag, void *call) {
    uintptr_t p = ((uintptr_t)call) & ~(uintptr_t)1;
    uint8_t b[8];
    int i;

    for (i = 0; i < 8; i++) {
        b[i] = *(volatile uint8_t *)(p + i);
    }

    sceClibPrintf("%s addr=%p Tbit=%d bytes=%02X %02X %02X %02X %02X %02X %02X %02X\n",
                  tag, call, ((uintptr_t)call) & 1,
                  b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
}

#ifdef GUNBROS_QUIET_LOGS
#define BEFORE(name, callptr) ((void)0)
#define AFTER(name) ((void)0)
#else
#define BEFORE(name, callptr) do { dump_prologue("[CALL]", (void *)(callptr)); sceClibPrintf("[BEFORE] %s\n", name); } while (0)
#define AFTER(name) sceClibPrintf("[AFTER ] %s OK\n", name)
#endif

typedef jint (*jni_on_load_t)(JavaVM *, void *);
typedef void (*jni_initialise_t)(JNIEnv *, jobject, jobject, jobject,
                                 jstring, jstring, jstring, jstring, jstring, jstring,
                                 jboolean, jboolean, jboolean,
                                 jstring, jstring, jstring, jstring,
                                 jint, jint, jint, jint, jstring);
typedef void (*jni_set_window_t)(JNIEnv *, jobject, jobject);
typedef jint (*jni_on_create_t)(JNIEnv *, jobject, jbyteArray, jobject);
typedef void (*jni_handle_void_t)(JNIEnv *, jobject, jint);
typedef void (*jni_handle_ii_t)(JNIEnv *, jobject, jint, jint, jint);
typedef void (*jni_handle_ifff_t)(JNIEnv *, jobject, jint, jfloat, jfloat, jfloat);
typedef void (*jni_handle_jz_t)(JNIEnv *, jobject, jint, jlong, jboolean);
typedef jboolean (*jni_handle_key_t)(JNIEnv *, jobject, jint, jint);
typedef void (*jni_handle_touch_t)(JNIEnv *, jobject, jint, jint, jint, jint);
typedef void (*so_input_touch_t)(jint x, jint y, jint id);
typedef void (*so_pcm_feed_t)(void *self, unsigned char *buffer, unsigned int byte_count);
typedef void (*jni_handle_surface_changed_t)(JNIEnv *, jobject, jint, jint, jint, jint);
typedef void (*jni_void_t)(JNIEnv *, jobject);

typedef struct GunBrosExports {
    jobject thiz;
    jint native_handle;
    jni_initialise_t initialise;
    jni_set_window_t set_window;
    jni_on_create_t on_create;
    jni_handle_void_t on_start;
    jni_handle_void_t on_resume;
    jni_handle_void_t on_pause;
    jni_handle_void_t on_stop;
    jni_handle_void_t on_restart;
    jni_handle_void_t on_destroy;
    jni_handle_void_t on_low_memory;
    jni_handle_void_t on_surface_created;
    jni_handle_ii_t on_surface_changed;
    jni_handle_jz_t on_draw_frame;
    jni_handle_key_t on_key_down;
    jni_handle_key_t on_key_up;
    jni_handle_touch_t touch_began;
    jni_handle_touch_t touch_moved;
    jni_handle_touch_t touch_ended;
    jni_handle_touch_t touch_cancelled;
    so_input_touch_t input_touch_down;
    so_input_touch_t input_touch_move;
    so_input_touch_t input_touch_release;
    so_pcm_feed_t feed_pcm;
    jni_handle_ifff_t push_accelerometer;
    jni_handle_surface_changed_t surface_changed;
    jni_handle_void_t surface_created;
    jni_handle_void_t surface_destroyed;
    jni_handle_void_t tidy;
    jni_void_t uninitialise;
} GunBrosExports;

static GunBrosExports g_game = {
    .thiz = (jobject)0x42424242,
};

static void *resolve_export(const char *symbol, const char **mode_out) {
    void *raw = (void *)(uintptr_t)so_symbol(&so_mod, symbol);
    void *call = autoselect_arm_thumb(raw, mode_out);

    dump_fn(symbol, raw, call, *mode_out);
    return call;
}

static void *resolve_so_function(const char *symbol, const char **mode_out) {
    void *raw = (void *)(uintptr_t)so_symbol(&so_mod, symbol);
    void *call = autoselect_arm_thumb(raw, mode_out);

    dump_fn(symbol, raw, call, *mode_out);
    return call;
}

static void resolve_exports(void) {
    const char *mode;

    sceClibPrintf("JNI exports (autodetect):\n");

    g_game.initialise = (jni_initialise_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_initialise", &mode);
    g_game.set_window = (jni_set_window_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_setWindow", &mode);
    g_game.on_create = (jni_on_create_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_onCreate", &mode);
    g_game.on_start = (jni_handle_void_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_onStart", &mode);
    g_game.on_resume = (jni_handle_void_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_onResume", &mode);
    g_game.on_pause = (jni_handle_void_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_onPause", &mode);
    g_game.on_stop = (jni_handle_void_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_onStop", &mode);
    g_game.on_restart = (jni_handle_void_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_onRestart", &mode);
    g_game.on_destroy = (jni_handle_void_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_onDestroy", &mode);
    g_game.on_low_memory = (jni_handle_void_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_onLowMemory", &mode);
    g_game.on_surface_created = (jni_handle_void_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_onSurfaceCreated", &mode);
    g_game.on_surface_changed = (jni_handle_ii_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_onSurfaceChanged", &mode);
    g_game.on_draw_frame = (jni_handle_jz_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_onDrawFrame", &mode);
    g_game.on_key_down = (jni_handle_key_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_onKeyDown", &mode);
    g_game.on_key_up = (jni_handle_key_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_onKeyUp", &mode);
    g_game.touch_began = (jni_handle_touch_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_TouchBegan", &mode);
    g_game.touch_moved = (jni_handle_touch_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_TouchMoved", &mode);
    g_game.touch_ended = (jni_handle_touch_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_TouchEnded", &mode);
    g_game.touch_cancelled = (jni_handle_touch_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_TouchCancelled", &mode);
    g_game.input_touch_down = (so_input_touch_t)resolve_so_function("_ZN6CInput15HandleTouchDownEiii", &mode);
    g_game.input_touch_move = (so_input_touch_t)resolve_so_function("_ZN6CInput15HandleTouchMoveEiii", &mode);
    g_game.input_touch_release = (so_input_touch_t)resolve_so_function("_ZN6CInput18HandleTouchReleaseEiii", &mode);
    g_game.feed_pcm = (so_pcm_feed_t)resolve_so_function("_ZN7CApplet18feedPCMMediaPlayerEPhj", &mode);
    g_game.push_accelerometer = (jni_handle_ifff_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_pushAccelerometerValues", &mode);
    g_game.surface_changed = (jni_handle_surface_changed_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_surfaceChanged", &mode);
    g_game.surface_created = (jni_handle_void_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_surfaceCreated", &mode);
    g_game.surface_destroyed = (jni_handle_void_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_surfaceDestroyed", &mode);
    g_game.tidy = (jni_handle_void_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_tidy", &mode);
    g_game.uninitialise = (jni_void_t)resolve_export("Java_com_glu_platform_android_GluPlatformActivityJNI_uninitialise", &mode);
}

static void run_jni_on_load(void) {
    const char *mode = "?";
    jni_on_load_t on_load = (jni_on_load_t)resolve_export("JNI_OnLoad", &mode);
    jint result;

    if (!on_load) {
        sceClibPrintf("[WARN] JNI_OnLoad not found\n");
        return;
    }

    BEFORE("JNI_OnLoad", on_load);
    result = on_load(&jvm, NULL);
    sceClibPrintf("JNI_OnLoad returned %d\n", (int)result);
    (void)result;
    AFTER("JNI_OnLoad");
}

static void run_game_bootstrap(void) {
    jstring internal_path = jni->NewStringUTF(ENV, DATA_PATH);
    jstring external_path = jni->NewStringUTF(ENV, DATA_PATH);
    jstring separator = jni->NewStringUTF(ENV, "/");
    jstring language = jni->NewStringUTF(ENV, "en");
    jstring country = jni->NewStringUTF(ENV, "US");
    jstring display_language = jni->NewStringUTF(ENV, "English");
    jstring model = jni->NewStringUTF(ENV, "PlayStation Vita");
    jstring android_id = jni->NewStringUTF(ENV, "PSVITA");
    jstring release = jni->NewStringUTF(ENV, "4.4.4");
    jstring sdk_string = jni->NewStringUTF(ENV, "19");
    jstring push_id = jni->NewStringUTF(ENV, "");
    jobject dummy_handler = (jobject)0x51515151;
    jobject dummy_surface_holder = (jobject)0x52525252;
    jobject dummy_callback = (jobject)0x53535353;

    if (g_game.initialise) {
        BEFORE("initialise", g_game.initialise);
        g_game.initialise(ENV, g_game.thiz, g_game.thiz, dummy_handler,
                          internal_path, external_path, separator,
                          language, country, display_language,
                          JNI_TRUE, JNI_TRUE, JNI_FALSE,
                          model, android_id, release, sdk_string,
                          19, 0, 960, 544, push_id);
        AFTER("initialise");
    }

    if (g_game.set_window) {
        BEFORE("setWindow", g_game.set_window);
        g_game.set_window(ENV, g_game.thiz, dummy_surface_holder);
        AFTER("setWindow");
    }

    if (g_game.on_create) {
        BEFORE("onCreate", g_game.on_create);
        g_game.native_handle = g_game.on_create(ENV, g_game.thiz, NULL, dummy_callback);
        sceClibPrintf("Native handle: %d\n", g_game.native_handle);
        AFTER("onCreate");
    }

    if (g_game.on_start) {
        BEFORE("onStart", g_game.on_start);
        g_game.on_start(ENV, g_game.thiz, g_game.native_handle);
        AFTER("onStart");
    }

    if (g_game.on_resume) {
        BEFORE("onResume", g_game.on_resume);
        g_game.on_resume(ENV, g_game.thiz, g_game.native_handle);
        AFTER("onResume");
    }

    if (g_game.surface_created) {
        BEFORE("surfaceCreated", g_game.surface_created);
        g_game.surface_created(ENV, g_game.thiz, g_game.native_handle);
        AFTER("surfaceCreated");
    }

    if (g_game.on_surface_created) {
        BEFORE("onSurfaceCreated", g_game.on_surface_created);
        g_game.on_surface_created(ENV, g_game.thiz, g_game.native_handle);
        AFTER("onSurfaceCreated");
    }

    if (g_game.surface_changed) {
        BEFORE("surfaceChanged", g_game.surface_changed);
        g_game.surface_changed(ENV, g_game.thiz, g_game.native_handle, 0, 960, 544);
        AFTER("surfaceChanged");
    }

    if (g_game.on_surface_changed) {
        BEFORE("onSurfaceChanged", g_game.on_surface_changed);
        g_game.on_surface_changed(ENV, g_game.thiz, g_game.native_handle, 960, 544);
        AFTER("onSurfaceChanged");
    }
}

static void shutdown_game(void) {
    gunbros_flush_offline_profile();
    gunbros_flush_offline_refinery();

    if (g_game.on_pause && g_game.native_handle) {
        g_game.on_pause(ENV, g_game.thiz, g_game.native_handle);
    }

    if (g_game.on_stop && g_game.native_handle) {
        g_game.on_stop(ENV, g_game.thiz, g_game.native_handle);
    }

    if (g_game.on_destroy && g_game.native_handle) {
        g_game.on_destroy(ENV, g_game.thiz, g_game.native_handle);
    }

    if (g_game.tidy && g_game.native_handle) {
        g_game.tidy(ENV, g_game.thiz, g_game.native_handle);
    }

    if (g_game.uninitialise) {
        g_game.uninitialise(ENV, g_game.thiz);
    }

    gunbros_audio_destroy();
}

void gunbros_fill_game_pcm(int16_t *samples, unsigned int byte_count) {
    if (!samples || byte_count == 0) {
        return;
    }
    memset(samples, 0, byte_count);
    if (g_game.feed_pcm && g_game.native_handle) {
        g_game.feed_pcm((void *)(uintptr_t)(uint32_t)g_game.native_handle,
                        (unsigned char *)samples, byte_count);
    }
}

static void push_jni_local_frame(jint capacity) {
    if (jni && jni->PushLocalFrame) {
        jni->PushLocalFrame(ENV, capacity);
    }
}

static void pop_jni_local_frame(void) {
    if (jni && jni->PopLocalFrame) {
        jni->PopLocalFrame(ENV, NULL);
    }
}

static const char *control_action_name(ControlsAction action) {
    switch (action) {
    case CONTROLS_ACTION_DOWN:
        return "down";
    case CONTROLS_ACTION_MOVE:
        return "move";
    case CONTROLS_ACTION_UP:
        return "up";
    default:
        return "?";
    }
}

static int32_t game_keycode_for_android(int32_t keycode) {
    switch (keycode) {
    case AKEYCODE_BACK:
    case AKEYCODE_DPAD_CENTER:
    case AKEYCODE_BUTTON_X:
    case AKEYCODE_BUTTON_Y:
    case AKEYCODE_BUTTON_L1:
    case AKEYCODE_BUTTON_R1:
    case AKEYCODE_BUTTON_START:
    case AKEYCODE_BUTTON_SELECT:
        return keycode | 0x10000000;
    default:
        return keycode;
    }
}

void controls_handler_key(int32_t keycode, ControlsAction action) {
    static int key_log_count = 0;
    int direct_only = (keycode == AKEYCODE_BUTTON_Y ||
                       keycode == AKEYCODE_BUTTON_START);
    int32_t game_keycode = game_keycode_for_android(keycode);

    if (!g_game.native_handle) {
        return;
    }

    if (GUNBROS_INPUT_TRACE && key_log_count < 48) {
        sceClibPrintf("[INPUT] key code=%d action=%s\n", keycode, control_action_name(action));
        key_log_count++;
    }

    if (keycode == AKEYCODE_BUTTON_START && action == CONTROLS_ACTION_DOWN) {
        if (GUNBROS_INPUT_TRACE) {
            sceClibPrintf("[INPUT] start -> request pause menu\n");
        }
        gunbros_request_pause_menu();
    }
    if (keycode == AKEYCODE_BUTTON_Y && action == CONTROLS_ACTION_DOWN) {
        if (GUNBROS_INPUT_TRACE) {
            sceClibPrintf("[INPUT] triangle -> request gun swap\n");
        }
        gunbros_request_player_gun_swap();
    }

    push_jni_local_frame(64);
    if (!direct_only && action == CONTROLS_ACTION_DOWN && g_game.on_key_down) {
        g_game.on_key_down(ENV, g_game.thiz, g_game.native_handle, (jint)game_keycode);
    } else if (!direct_only && action == CONTROLS_ACTION_UP && g_game.on_key_up) {
        g_game.on_key_up(ENV, g_game.thiz, g_game.native_handle, (jint)game_keycode);
    }
    pop_jni_local_frame();

    if (keycode == AKEYCODE_DPAD_CENTER &&
            (action == CONTROLS_ACTION_DOWN || action == CONTROLS_ACTION_UP)) {
        if (GUNBROS_INPUT_TRACE) {
            sceClibPrintf("[INPUT] key-as-touch code=%d action=%s -> center tap\n",
                          keycode,
                          control_action_name(action));
        }
        controls_handler_touch(0x7f00, 480.0f, 272.0f, action);
    }
}

static int controls_handler_touch_routed(int32_t id, float x, float y,
                                         ControlsAction action,
                                         int allow_direct_native) {
    static int touch_log_count = 0;
    static int direct_log_count = 0;
    jint ix = (jint)x;
    jint iy = (jint)y;
    jint pid = (jint)id;
    int synthetic_gameplay_touch = (id >= 0x7f00);
    int synthetic_stick_touch = (id == 0x7f10 || id == 0x7f11);
    int direct_ready =
        action == CONTROLS_ACTION_DOWN ? g_game.input_touch_down != NULL :
        action == CONTROLS_ACTION_MOVE ? g_game.input_touch_move != NULL :
                                         g_game.input_touch_release != NULL;
    int feed_native_direct = allow_direct_native && synthetic_stick_touch &&
                             direct_ready;
    int feed_jni = !feed_native_direct;
    int delivered = 0;
    if (!g_game.native_handle && !feed_native_direct) {
        return 0;
    }

    if (GUNBROS_INPUT_TRACE && touch_log_count < 96) {
        sceClibPrintf("[INPUT] touch id=%d x=%d y=%d action=%s route=%s%s\n",
                      id, ix, iy, control_action_name(action),
                      feed_native_direct ? "native" :
                       (feed_jni ? "jni-only" : "none"),
                      synthetic_gameplay_touch ? " synthetic" :
                      "");
        touch_log_count++;
    }
    if (feed_native_direct) {
        if (GUNBROS_INPUT_TRACE && direct_log_count < 48) {
            sceClibPrintf("[INPUT-DIRECT] synthetic-gameplay id=%d x=%d y=%d action=%s\n",
                          id, ix, iy, control_action_name(action));
            direct_log_count++;
        }
        if (action == CONTROLS_ACTION_DOWN && g_game.input_touch_down) {
            g_game.input_touch_down(ix, iy, pid);
            delivered = 1;
        } else if (action == CONTROLS_ACTION_MOVE && g_game.input_touch_move) {
            g_game.input_touch_move(ix, iy, pid);
            delivered = 1;
        } else if (action == CONTROLS_ACTION_UP && g_game.input_touch_release) {
            g_game.input_touch_release(ix, iy, pid);
            delivered = 1;
        }
    }
    if (feed_jni && g_game.native_handle) {
        push_jni_local_frame(64);
        if (action == CONTROLS_ACTION_DOWN && g_game.touch_began) {
            g_game.touch_began(ENV, g_game.thiz, g_game.native_handle,
                               ix, iy, pid);
            delivered = 1;
        } else if (action == CONTROLS_ACTION_MOVE && g_game.touch_moved) {
            g_game.touch_moved(ENV, g_game.thiz, g_game.native_handle,
                               ix, iy, pid);
            delivered = 1;
        } else if (action == CONTROLS_ACTION_UP && g_game.touch_ended) {
            g_game.touch_ended(ENV, g_game.thiz, g_game.native_handle,
                               ix, iy, pid);
            delivered = 1;
        }
        pop_jni_local_frame();
    }
    return delivered;
}

void controls_handler_touch(int32_t id, float x, float y, ControlsAction action) {
    controls_handler_touch_routed(id, x, y, action, 1);
}

typedef struct VirtualStickState {
    int active;
    int id;
    float cx;
    float cy;
    float radius;
    float last_x;
    float last_y;
    const char *name;
} VirtualStickState;

static VirtualStickState g_virtual_sticks[2] = {
    { 0, 0x7f10, 145.0f, 415.0f, 82.0f, 145.0f, 415.0f,
      "left-move" },
    { 0, 0x7f11, 815.0f, 415.0f, 82.0f, 815.0f, 415.0f,
      "right-fire" },
};

static void gunbros_publish_analog_stick_intent(ControlsStickId which,
                                                float x, float y,
                                                int active) {
    GunBrosGameplayInputState next = g_gunbros_gameplay_input;
    int changed = 0;

    if (which == CONTROLS_STICK_LEFT) {
        if (active) {
            changed = !next.move_active || next.move_source != 2 ||
                      next.move_x != x || next.move_y != y;
            next.move_active = 1;
            next.move_x = x;
            next.move_y = y;
            next.move_source = 2;
        } else if (next.move_source == 2) {
            changed = next.move_active || next.move_x != 0.0f ||
                      next.move_y != 0.0f;
            next.move_active = 0;
            next.move_x = 0.0f;
            next.move_y = 0.0f;
            next.move_source = 0;
        }
    } else if (which == CONTROLS_STICK_RIGHT) {
        if (active) {
            changed = !next.fire_active || next.fire_source != 2 ||
                      next.fire_x != x || next.fire_y != y;
            next.fire_active = 1;
            next.fire_x = x;
            next.fire_y = y;
            next.fire_source = 2;
        } else if (next.fire_source == 2) {
            changed = next.fire_active || next.fire_x != 0.0f ||
                      next.fire_y != 0.0f;
            next.fire_active = 0;
            next.fire_x = 0.0f;
            next.fire_y = 0.0f;
            next.fire_source = 0;
        }
    }

    if (changed) {
        next.seq++;
        g_gunbros_gameplay_input = next;
    }
}

void gunbros_set_virtual_stick_layout(unsigned int which,
                                      float cx, float cy, float radius) {
    VirtualStickState *st;

    if (which >= 2u || cx < 0.0f || cx > 959.0f ||
        cy < 0.0f || cy > 543.0f || radius < 8.0f || radius > 544.0f) {
        return;
    }

    st = &g_virtual_sticks[which];

    if (st->active) {
        (void)controls_handler_touch_routed(st->id, st->last_x, st->last_y,
                                            CONTROLS_ACTION_UP, 1);
        st->active = 0;
    }
    gunbros_publish_analog_stick_intent(
        which == 0u ? CONTROLS_STICK_LEFT : CONTROLS_STICK_RIGHT,
        0.0f, 0.0f, 0);
    st->cx = cx;
    st->cy = cy;
    st->radius = radius;
    st->last_x = cx;
    st->last_y = cy;
    if (GUNBROS_INPUT_TRACE) {
        sceClibPrintf("[INPUT-LAYOUT] %s center=%.1f,%.1f radius=%.1f active=%d\n",
                      st->name, cx, cy, radius, st->active);
    }
}

static void analog_to_virtual_stick_touch(ControlsStickId which, float x, float y, ControlsAction action) {
    static int analog_log_count = 0;
    static int edge_diag_count = 0;
    VirtualStickState *st;
    float mag2;
    float tx;
    float ty;
    ControlsAction out_action;
    int delivered;

    if (which != CONTROLS_STICK_LEFT && which != CONTROLS_STICK_RIGHT) {
        return;
    }

    st = &g_virtual_sticks[which == CONTROLS_STICK_RIGHT ? 1 : 0];
    mag2 = x * x + y * y;

    if (mag2 < 0.0004f || action == CONTROLS_ACTION_UP) {
        if (st->active) {
            if (GUNBROS_INPUT_TRACE && analog_log_count < 48) {
                sceClibPrintf("[INPUT-ANALOG] %s id=%d up at %.1f,%.1f\n",
                              st->name, st->id, st->cx, st->cy);
                analog_log_count++;
            }
            delivered = controls_handler_touch_routed(
                st->id, st->last_x, st->last_y, CONTROLS_ACTION_UP, 1);
            if (edge_diag_count < 48) {
                GUNBROS_PERF_LOG("[DIAG-STICK] name=%s edge=up id=%d pos=%.1f,%.1f delivered=%d route=direct\n",
                                 st->name, st->id, st->last_x, st->last_y,
                                 delivered);
                edge_diag_count++;
            }
            st->active = 0;
            st->last_x = st->cx;
            st->last_y = st->cy;
        }
        gunbros_publish_analog_stick_intent(which, 0.0f, 0.0f, 0);
        return;
    }

    tx = st->cx + x * st->radius;
    ty = st->cy + y * st->radius;
    if (tx < 0.0f) tx = 0.0f;
    if (tx > 959.0f) tx = 959.0f;
    if (ty < 0.0f) ty = 0.0f;
    if (ty > 543.0f) ty = 543.0f;

    out_action = st->active ? CONTROLS_ACTION_MOVE : CONTROLS_ACTION_DOWN;
    if (GUNBROS_INPUT_TRACE && analog_log_count < 48) {
        sceClibPrintf("[INPUT-ANALOG] %s id=%d vec=%.2f,%.2f -> touch %.1f,%.1f action=%s\n",
                      st->name, st->id, x, y, tx, ty, control_action_name(out_action));
        analog_log_count++;
    }

    if (!st->active) {
        delivered = controls_handler_touch_routed(
            st->id, st->cx, st->cy, CONTROLS_ACTION_DOWN, 1);
        if (edge_diag_count < 48) {
            GUNBROS_PERF_LOG("[DIAG-STICK] name=%s edge=down id=%d center=%.1f,%.1f target=%.1f,%.1f delivered=%d route=direct\n",
                             st->name, st->id, st->cx, st->cy, tx, ty,
                             delivered);
            edge_diag_count++;
        }
        if (!delivered) {
            st->active = 0;
            gunbros_publish_analog_stick_intent(which, 0.0f, 0.0f, 0);
            return;
        }
        st->active = 1;
        st->last_x = st->cx;
        st->last_y = st->cy;
        gunbros_publish_analog_stick_intent(which, x, y, 1);
        return;
    }
    delivered = controls_handler_touch_routed(st->id, tx, ty, out_action, 1);
    if (!delivered) {
        (void)controls_handler_touch_routed(st->id, st->last_x, st->last_y,
                                            CONTROLS_ACTION_UP, 1);
        st->active = 0;
        gunbros_publish_analog_stick_intent(which, 0.0f, 0.0f, 0);
        return;
    }
    st->last_x = tx;
    st->last_y = ty;
    st->active = 1;
    gunbros_publish_analog_stick_intent(which, x, y, 1);
}

void controls_handler_analog(ControlsStickId which, float x, float y, ControlsAction action) {
    analog_to_virtual_stick_touch(which, x, y, action);
}

static const char *gunbros_return_to_launcher(void) {
    gunbros_finish_requested = 1;
    return NULL;
}

int main(void) {
    GunBrosSpeedConfig speed_config;

    sceClibPrintf("=== Gun Bros Vita Loader ===\n");
    sceClibPrintf("[BUILD] %s\n", GUNBROS_LOADER_BUILD);

    sceClibPrintf("SO Loader: soloader_init_all()...\n");
    soloader_init_all();
    sceClibPrintf("SO Loader: done.\n");

    if (!jni) {
        sceClibPrintf("[FATAL] FalsoJNI jni == NULL\n");
        sceKernelExitDeleteThread(0);
        return 0;
    }

    gunbros_preload_offline_profile();

    speed_config = gunbros_load_speed_config();
    g_gunbros_gameplay_speed = speed_config.speed;
    g_gunbros_gameplay_max_delta_ms = speed_config.max_delta_ms;
    sceClibPrintf("[SPEED] gameplay_speed=%.3f gameplay_max_delta_ms=%u path=%sspeed.txt\n",
                  speed_config.speed, speed_config.max_delta_ms, DATA_PATH);

#ifndef GUNBROS_QUIET_LOGS
    gunbros_print_data_check();
#endif

    gunbros_java_init();
    run_jni_on_load();

    BEFORE("gl_init", gl_init);
    gl_init();
    glViewport(0, 0, 960, 544);
    AFTER("gl_init");

    sceClibPrintf("GL_VENDOR  : %s\n", (const char *)glGetString(GL_VENDOR));
    sceClibPrintf("GL_RENDERER: %s\n", (const char *)glGetString(GL_RENDERER));
    sceClibPrintf("GL_VERSION : %s\n", (const char *)glGetString(GL_VERSION));

    resolve_exports();
    (void)gunbros_audio_init();
    run_game_bootstrap();
    (void)gunbros_audio_start_pcm();
    controls_init();

    sceClibPrintf("Entering render loop...\n");

    uint32_t frame_count = 0;
    const uint64_t frame_clock_start_us = sceKernelGetProcessTimeWide();
    const jlong frame_clock_offset_ms =
        gunbros_tick_rate_ms > 0 ? (jlong)gunbros_tick_rate_ms : 16;

    unsigned int launcher_chord_held = 0;
    while (!gunbros_finish_requested) {
        uint64_t frame_begin_us;
        uint64_t frame_end_us;
        uint64_t frame_deadline_us;
        uint64_t native_begin_us;
        uint64_t native_end_us;
        jlong frame_time_ms;
        int tick_ms;
        int frame_budget_ms;
        int frame_log;

        frame_begin_us = sceKernelGetProcessTimeWide();
        frame_count++;
        if ((frame_count % 60u) == 0u) {
            gunbros_audio_service();
        }
        g_gunbros_render_frame = frame_count;
        tick_ms = gunbros_tick_rate_ms > 0 ? gunbros_tick_rate_ms : 16;
        frame_budget_ms = tick_ms < 16 ? 16 : tick_ms;
        frame_time_ms = frame_clock_offset_ms +
            (jlong)((frame_begin_us - frame_clock_start_us) / 1000u);
        frame_deadline_us = frame_begin_us + (uint64_t)frame_budget_ms * 1000u;
        frame_log = GUNBROS_FRAME_TRACE &&
                    ((frame_count <= 12) || ((frame_count % 120) == 0));

        if (frame_log) {
            sceClibPrintf("[FRAME] start frame=%u tick=%d budget=%d time=%lld handle=%d finish=%d\n",
                          frame_count,
                          tick_ms,
                          frame_budget_ms,
                          (long long)frame_time_ms,
                          g_game.native_handle,
                          gunbros_finish_requested);
        }

        SceCtrlData launcher_pad = {0};
        sceCtrlPeekBufferPositive(0, &launcher_pad, 1);
        unsigned int launcher_chord = (launcher_pad.buttons & (SCE_CTRL_START | SCE_CTRL_SELECT)) ==
                                      (SCE_CTRL_START | SCE_CTRL_SELECT);
        if (launcher_chord && !launcher_chord_held)
            gunbros_classic_dialog("Return to the Gun Bros launcher?\nYour progress will be saved.", gunbros_return_to_launcher);
        launcher_chord_held = launcher_chord;
        controls_poll();
        native_begin_us = sceKernelGetProcessTimeWide();

        if (g_game.on_draw_frame && g_game.native_handle) {
            if (frame_log) {
                sceClibPrintf("[FRAME] before onDrawFrame frame=%u\n", frame_count);
            }

            push_jni_local_frame(512);
            g_game.on_draw_frame(ENV, g_game.thiz, g_game.native_handle, frame_time_ms, JNI_TRUE);
            pop_jni_local_frame();

            if (frame_log) {
                sceClibPrintf("[FRAME] after onDrawFrame frame=%u\n", frame_count);
            }
        } else {
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        native_end_us = sceKernelGetProcessTimeWide();
        gl_swap();
        gunbros_classic_dialog_service();
        frame_end_us = sceKernelGetProcessTimeWide();
        gunbros_perf_record_frame(frame_begin_us, native_begin_us,
                                  native_end_us, frame_end_us);
        if (frame_end_us < frame_deadline_us) {
            uint64_t remaining_us = frame_deadline_us - frame_end_us;
            sceKernelDelayThread((unsigned int)remaining_us);
        }
    }

    shutdown_game();
    sceAppMgrLoadExec("app0:/eboot.bin", NULL, NULL);
    sceKernelExitDeleteThread(0);
    return 0;
}
