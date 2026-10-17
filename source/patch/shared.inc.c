static uintptr_t g_original_registry_flatten, g_original_registry_count;
static uintptr_t g_original_registry_validate, g_original_registry_validate_ref;
/* Stable continuations for hot registry lookups: no per-call code rewriting. */
static uintptr_t g_original_registry_get, g_original_registry_get_const;
static uintptr_t g_original_registry_load_req, g_original_registry_free_req;
/* Shared hook state, tracing, pointer validation, and runtime diagnostics. */

#define GUNBROS_OBJECT_TYPE_GUN 6u
#define GUNBROS_OBJECT_TYPE_ARMOR 2u
#define GUNBROS_OBJECT_TYPE_POWERUP 17u

extern volatile uint32_t g_gunbros_render_frame;
extern float g_gunbros_gameplay_speed;
extern unsigned int g_gunbros_gameplay_max_delta_ms;

/* Captured by the native CResTOCManager lifecycle hooks.  Saved
 * GameObjectRefs contain a pack hash in their first word; rebuilding that
 * word requires the same live TOC owner used by GameObjectRef::ReconcilePackIdx. */
static void *g_live_res_toc_manager;

static int menu_splash_busy_stub(void *self) {
    (void)self;
    patch_log_once("CMenuSplash::IsBusy", "false");
    return 0;
}

static so_hook h_menu_stack_set_menu;
static so_hook h_menu_stack_push_menu;
static so_hook h_menu_stack_pop_menu;
static so_hook h_menu_navigation_bar_hide_buttons;
static so_hook h_menu_system_get_font;
static so_hook h_refinement_get_interval_duration_ms;
static so_hook h_refinement_get_interval_efficiency;
static so_hook h_refinement_get_interval_purchase_cost;
static so_hook h_refinement_begin;
static so_hook h_refinement_collect_resources;
static so_hook h_refinery_meter_refresh;
static so_hook h_refinery_meter_draw;
static so_hook h_refinery_transfer_effect_draw;
static so_hook h_menu_game_resources_on_show;
static so_hook h_menu_game_resources_meters_enabled;
static so_hook h_menu_splash_is_loaded;
static so_hook h_gunbros_update;
static so_hook h_game_update;
static so_hook h_game_load;
static so_hook h_gunbros_reinit;
static so_hook h_gunbros_reinitialize_all;
static so_hook h_gunbros_bind;
static so_hook h_gunbros_load_menus;
static so_hook h_gunbros_load_mission;
static so_hook h_gunbros_show_main_menu;
static so_hook h_gunbros_set_menu;
static so_hook h_gunbros_flatten_object_index_const;
static so_hook h_gunbros_get_object_count_const;
static so_hook h_gunbros_validate_game_object_3_const;
static so_hook h_gunbros_validate_game_object_ref_const;
static so_hook h_gunbros_get_game_object_pack;
static so_hook h_gunbros_get_game_object_pack_const;
static so_hook h_gunbros_load_game_object_req;
static so_hook h_gunbros_free_game_object_req;
static so_hook h_resource_load_next;
static so_hook h_media_player_play_internal;
static so_hook h_options_read;
static so_hook h_bgm_play_track;
static so_hook h_menu_stack_load_menu;
static so_hook h_menu_splash_load;
static so_hook h_menu_greeting_load;
static so_hook h_menu_system_load;
static so_hook h_menu_system_set_menu;
static int classic_menu_block(int screen, unsigned short arg);
static so_hook h_menu_system_push_menu;
static so_hook h_menu_system_pop_menu;
static so_hook h_res_pack_toc_init;
static so_hook h_res_pack_toc_bind;
static so_hook h_res_toc_manager_init;
static so_hook h_res_toc_manager_bind;
static so_hook h_res_toc_manager_set_pack_hash;
static so_hook h_res_toc_manager_set_pack_str;
static so_hook h_res_toc_manager_set_pack_internal;
static so_hook h_image_pool_load_image;
static so_hook h_movie_load;
static so_hook h_movie_sprite_load;
static so_hook h_movie_sound_set_load;
static so_hook h_movie_tiled_sprite_load;
static so_hook h_graphics_instr_texture;
static so_hook h_player_progress_get_experience_for_level;
static so_hook h_player_progress_get_experience_delta;
static so_hook h_player_progress_get_percent_to_next_level;
static so_hook h_sprite_player_draw_rect;
static so_hook h_game_flow_configure_brother;
static so_hook h_game_flow_get_mission;
static so_hook h_game_get_player_data;
static so_hook h_friend_data_get_avatar_progress;
static so_hook h_friend_data_get_avatar_config;
static so_hook h_game_object_pack_init_game_object;
static so_hook h_game_object_pack_get_game_object;
static so_hook h_game_object_pack_get_game_object_const;
static so_hook h_store_aggregator_configure;
static so_hook h_store_aggregator_is_item_owned_or_equipped;
static so_hook h_store_aggregator_is_item_level_locked;
static so_hook h_store_aggregator_can_item_be_acquired;
static so_hook h_store_aggregator_get_item_status;
static so_hook h_store_aggregator_clear_cached_content;
static so_hook h_profile_manager_save;
static so_hook h_store_aggregator_init_filtered_list;
static so_hook h_store_aggregator_acquire_item;
static so_hook h_store_aggregator_equip_item;
static so_hook h_menu_mesh_player_bind_player;
static so_hook h_store_aggregator_get_filtered_item_count;
static so_hook h_store_aggregator_get_item_cost;
static so_hook h_store_aggregator_is_in_app_purchase;
static so_hook h_store_aggregator_create_item_cost_string;
static so_hook h_store_aggregator_create_sale_string;
static so_hook h_menu_store_handle_touch_input;
static so_hook h_menu_store_on_show;
static so_hook h_menu_upgrade_popup_show_for_guns;
static so_hook h_mesh_get_interpolation_values;
static so_hook h_mesh_get_nodes_count;
static so_hook h_mesh_get_node_at;
static so_hook h_mesh_build_tween_frame;
static so_hook h_mesh_get_vertices_at;
static so_hook h_mesh_animation_controller_render;
static so_hook h_level_on_start;
static so_hook h_brother_spawn_construct;
static so_hook h_brother_respawn_trace;
static so_hook h_brother_bind_trace;
static so_hook h_challenge_update_status_data;
static so_hook h_challenge_update_from_level_session;
static so_hook h_input_pad_bind;
static so_hook h_input_pad_update_input;
static so_hook h_gun_fire;
static so_hook h_weapon_mastery_add_xp;
static so_hook h_brother_move_trace;
static so_hook h_brother_fire_trace;
static so_hook h_brother_draw_trace;
static so_hook h_armor_template_validate;
static so_hook h_player_bind_trace;
static so_hook h_player_update_trace;

/* Fast original-entry trampolines for hooks reached every frame or once per
 * visible item.  Calling these avoids SO_CONTINUE's per-call code rewrite and
 * instruction-cache flush while retaining the existing guards/repairs. */
static uintptr_t g_original_gunbros_update;
static uintptr_t g_original_gunbros_show_main_menu;
static uintptr_t g_original_game_update;
static uintptr_t g_original_game_load;
static uintptr_t g_original_player_update_trace;
static uintptr_t g_original_input_pad_update_input;
static uintptr_t g_original_profile_manager_save;
static uintptr_t g_original_menu_store_handle_touch_input;
static uintptr_t g_original_menu_store_on_show;
static uintptr_t g_original_sprite_player_draw_rect;
static uintptr_t g_original_menu_system_get_font;
static uintptr_t g_original_player_progress_get_experience_for_level;
static uintptr_t g_original_player_progress_get_experience_delta;
static uintptr_t g_original_player_progress_get_percent_to_next_level;
static uintptr_t g_original_refinery_meter_refresh;
static uintptr_t g_original_refinery_meter_draw;
static uintptr_t g_original_refinery_transfer_effect_draw;
static uintptr_t g_original_resource_load_next;
static uintptr_t g_original_image_pool_load_image;
static uintptr_t g_original_game_object_pack_init_game_object;
static uintptr_t g_original_store_aggregator_is_item_owned_or_equipped;
static uintptr_t g_original_store_aggregator_is_item_level_locked;
static uintptr_t g_original_store_aggregator_can_item_be_acquired;
static uintptr_t g_original_store_aggregator_get_item_status;
static uintptr_t g_original_store_aggregator_clear_cached_content;
static uintptr_t g_original_store_aggregator_init_filtered_list;
static uintptr_t g_original_store_aggregator_get_filtered_item_count;
static uintptr_t g_original_store_aggregator_get_item_cost;
static uintptr_t g_original_store_aggregator_create_item_cost_string;
static uintptr_t g_original_store_aggregator_create_sale_string;

/* pack0_core_wvga contains the unarmored player atlases as ordinary image
 * resources.  Keep the ICRenderSurface objects returned by CImagePool so an
 * empty armor slot can restore its own base surface after a store preview has
 * released the previously bound armor template. */
typedef struct GunBrosCoreDefaultTextures {
    void *percy_torso;
    void *pants;
    void *cigar;
    void *francis_torso;
} GunBrosCoreDefaultTextures;

static GunBrosCoreDefaultTextures g_core_default_textures;

typedef struct GunBrosGameplayInputState {
    int move_active;
    int fire_active;
    float move_x;
    float move_y;
    float fire_x;
    float fire_y;
    int move_source;
    int fire_source;
    unsigned int seq;
} GunBrosGameplayInputState;

extern volatile GunBrosGameplayInputState g_gunbros_gameplay_input;
extern void gunbros_set_virtual_stick_layout(unsigned int which,
                                             float cx, float cy, float radius);

static void gameplay_control_bridge_update(int dt);
static int gameplay_armor_ref_resolves(unsigned int id,
                                       unsigned int subtype,
                                       const char *source);
static void gameplay_request_configured_content(const void *config,
                                                const char *source);
static void vita_asset_preload_note_loader(void *loader);
static void vita_asset_preload_enter_gameplay(void *loader,
                                               const char *source);
static void vita_asset_preload_restore_for_menus(const char *source);
static void vita_offline_profile_prepare_live_owner(const char *source);
static void vita_offline_profile_begin_bulk_transition(const char *source);
static void vita_offline_profile_end_bulk_transition(const char *source);
static int store_offline_purchases_save(int force);
static int trace_armor_template_validate(const void *self);
static void ensure_gti2_keepalive_offline_guards(const char *source);

enum {
    GUNBROS_STATE_LOADING_SPLASH = 2,
    GUNBROS_MAIN_MENU_SCREEN = 0x13,
    GUNBROS_MAIN_MENU_KICK_DELAY_UPDATES = 2,
};

static int g_main_menu_kick_pending;
static int g_main_menu_kick_done;
static int g_main_menu_kick_delay;
static int g_splash_loaded_seen;

/* Keep high-value diagnostics enabled after the category split.  The old
 * monolith used this gate to suppress frame-by-frame loader noise. */
static int trace_label_is_focused(const char *label) {
    return label &&
           (strstr(label, "CStoreAggregator") ||
            strstr(label, "CBrother") ||
            strstr(label, "CPlayer") ||
            strstr(label, "CResourceLoader") ||
            strstr(label, "CResPackTOC") ||
            strstr(label, "CResTOCManager") ||
            strstr(label, "CMenuStack") ||
            strstr(label, "CMenuSystem") ||
            strstr(label, "CMenuSplash") ||
            strstr(label, "CMenuGreeting") ||
            strstr(label, "CMenuGameResources") ||
            strstr(label, "CRefinementManager") ||
            strstr(label, "gameplay") ||
            strstr(label, "GunBros") ||
            strstr(label, "MenuUpgradePopup") ||
            strstr(label, "InstrTexure") ||
            strstr(label, "CMesh") ||
            strstr(label, "CImagePool"));
}

static int trace_allow_count(const char *label, int first, int every, int *out_count) {
#ifdef GUNBROS_QUIET_LOGS
    (void)label;
    (void)first;
    (void)every;
    if (out_count) {
        *out_count = 0;
    }
    return 0;
#else
    enum { max_slots = 96 };
    static struct {
        const char *label;
        int count;
    } slots[max_slots];
    int i;
    int slot = -1;

    if (!trace_label_is_focused(label)) {
        if (out_count) {
            *out_count = 0;
        }
        return 0;
    }

    for (i = 0; i < max_slots; i++) {
        if (slots[i].label == label) {
            slot = i;
            break;
        }

        if (!slots[i].label && slot < 0) {
            slot = i;
        }
    }

    if (slot >= 0 && !slots[slot].label) {
        slots[slot].label = label;
    }

    if (slot < 0) {
        if (out_count) {
            *out_count = 0;
        }
        return 1;
    }

    slots[slot].count++;

    if (out_count) {
        *out_count = slots[slot].count;
    }

    if (slots[slot].count <= first) {
        return 1;
    }

    return every > 0 && (slots[slot].count % every) == 0;
#endif
}

static int trace_allow_ex(const char *label, int first, int every) {
    return trace_allow_count(label, first, every, NULL);
}

static int trace_allow(const char *label) {
    return trace_allow_ex(label, 20, 120);
}

static const char *trace_cstr(const char *text) {
    uintptr_t value = (uintptr_t)text;
    int i;

    if (!text) {
        return "(null)";
    }

    if (value < 0x81000000u || value >= 0xA0000000u) {
        return "(badptr)";
    }

    for (i = 0; i < 96; ++i) {
        if (text[i] == '\0') {
            return text;
        }
    }

    return "(unterminated)";
}

static int trace_is_game_ptr(const void *ptr) {
    uintptr_t value = (uintptr_t)ptr;

    return value >= 0x81000000u && value < 0xA0000000u;
}

static int trace_is_game_range(const void *ptr, size_t size) {
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end = start + size;

    return size > 0 &&
           start >= 0x81000000u &&
           end >= start &&
           end < 0xA0000000u;
}

static int brother_has_bound_state(const void *self);

static void trace_dump_brother_state(const char *tag, const void *self) {
    const unsigned char *base = (const unsigned char *)self;
    const void *vptr = NULL;
    const void *map = NULL;
    const void *config = NULL;
    const void *gun_runtime = NULL;
    const void *move_primary = NULL;
    const void *move_secondary = NULL;
    const void *script_base_state = NULL;
    const void *script_normal_state = NULL;
    unsigned int active = 0;
    unsigned int visible = 0;
    unsigned int shooting = 0;
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    float max_health = 0.0f;
    float health = 0.0f;

    if (!trace_is_game_range(self, 0xbb0)) {
        sceClibPrintf("[DIAG-BROTHER] %s brother=%p INVALID_RANGE\n",
                      tag ? tag : "(null)", self);
        return;
    }

    vptr = *(const void * const *)(const void *)base;
    map = *(const void * const *)(const void *)(base + 0x6fc);
    config = *(const void * const *)(const void *)(base + 0x780);
    gun_runtime = *(const void * const *)(const void *)(base + 0x210);
    move_primary = *(const void * const *)(const void *)(base + 0x6f8);
    move_secondary = *(const void * const *)(const void *)(base + 0x6f4);
    script_base_state = *(const void * const *)(const void *)(base + 0x68 + 0x30);
    script_normal_state = *(const void * const *)(const void *)(base + 0x310 + 0x30);
    active = base[0x785];
    visible = base[0x786];
    shooting = base[0x788];
    pos_x = *(const float *)(const void *)(base + 0x708);
    pos_y = *(const float *)(const void *)(base + 0x70c);
    max_health = *(const float *)(const void *)(base + 0x728);
    health = *(const float *)(const void *)(base + 0x72c);

    sceClibPrintf("[DIAG-BROTHER] %s brother=%p vptr=%p map=%p cfg=%p gunCtx=%p moveA=%p moveB=%p scriptBase=%p scriptNormal=%p pos=(%.1f,%.1f) hp=%.1f/%.1f flags(active=%u visible=%u shooting=%u)\n",
                  tag ? tag : "(null)", self, vptr, map, config, gun_runtime,
                  move_primary, move_secondary, script_base_state,
                  script_normal_state, pos_x, pos_y, health, max_health,
                  active, visible, shooting);
}

static int trace_float_bad_or_dead(float value, float min_good, float max_good) {
    return !(value >= min_good && value <= max_good);
}

static void repair_brother_runtime_state(void *self, const char *tag) {
    unsigned char *base = (unsigned char *)self;
    float *max_health;
    float *health;
    int repaired = 0;

    if (!trace_is_game_range(self, 0xbb0u)) {
        return;
    }

    max_health = (float *)(void *)(base + 0x728);
    health = (float *)(void *)(base + 0x72c);

    /* Offline/profile bootstrap can leave health at 0, which makes the actor
     * look permanently dead and can suppress normal movement/damage handling. */
    if (trace_float_bad_or_dead(*max_health, 1.0f, 100000.0f)) {
        *max_health = 100.0f;
        repaired = 1;
    }
    if (trace_float_bad_or_dead(*health, 1.0f, *max_health)) {
        *health = *max_health;
        repaired = 1;
    }

    if (base[0x786] == 0) {
        base[0x786] = 1;
        repaired = 1;
    }

    if (brother_has_bound_state(self) && base[0x785] == 0) {
        base[0x785] = 1;
        repaired = 1;
    }

    if (repaired && trace_allow(tag ? tag : "CBrother/runtime_repair")) {
        sceClibPrintf("[FIX-BROTHER] runtime state repaired tag=%s brother=%p hp=%.1f/%.1f active=%u visible=%u\n",
                      tag ? tag : "(null)", self, *health, *max_health,
                      (unsigned int)base[0x785], (unsigned int)base[0x786]);
        trace_dump_brother_state("CBrother/runtime-after", self);
    }
}

/* Forward declaration: defined below (near patch_brother_spawn_bad_vtable),
 * but also needed here to construct CLevel's own "current" brother slot -
 * see the comment on trace_level_on_start below. */
static void construct_brother_if_uninitialized(void *self, const char *log_label);
static void repair_brother_vptr_preserve_state(void *self, const char *log_label, void *preferred_vptr);
