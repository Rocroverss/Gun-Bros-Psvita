static void *g_live_gunbros_self;
static int player_config_repair_armor_slots(void *config, const char *source);

enum {
    GUNBROS_GAME_OBJECT_TYPE_COUNT = 28,
    GUNBROS_GAME_OBJECT_INDEX_COUNT = 34,
    GUNBROS_GAME_OBJECT_TYPE_ARMOR = 2,
    GUNBROS_GAME_OBJECT_TYPE_GUN = 6,
};

static int gunbros_object_registry_is_valid(const void *self) {
    const unsigned char *base = (const unsigned char *)self;
    const void *packs;
    unsigned int count;

    if (!trace_is_game_range(self, 0x140)) {
        return 0;
    }

    packs = *(const void * const *)(const void *)(base + 0x138);
    count = *(const unsigned int *)(const void *)(base + 0x13c);

    return count > 0 &&
           count <= 64 &&
           trace_is_game_range(packs, (size_t)count * 0xb8u);
}

static int gunbros_game_object_pack_tables_ready(const void *pack,
                                                  const unsigned char *expected_counts,
                                                  unsigned int expected_type_count,
                                                  const unsigned int *expected_indices,
                                                  unsigned int expected_index_count,
                                                  unsigned int *gun_count,
                                                  unsigned int *armor_count) {
    const unsigned char *base = (const unsigned char *)pack;
    const unsigned char *objects;
    const unsigned char *load_flags;
    unsigned int object_type_count;
    unsigned int load_type_count;
    unsigned int type;

    if (!trace_is_game_range(pack, 0xb8u)) {
        return 0;
    }
    objects = *(const unsigned char * const *)(const void *)(base + 0x04u);
    object_type_count = *(const unsigned int *)(const void *)(base + 0x08u);
    load_flags = *(const unsigned char * const *)(const void *)(base + 0x0cu);
    load_type_count = *(const unsigned int *)(const void *)(base + 0x10u);
    if (object_type_count != GUNBROS_GAME_OBJECT_TYPE_COUNT ||
        load_type_count != GUNBROS_GAME_OBJECT_TYPE_COUNT ||
        expected_type_count != GUNBROS_GAME_OBJECT_TYPE_COUNT ||
        !expected_counts ||
        memcmp(base + 0x14u, expected_counts,
               GUNBROS_GAME_OBJECT_TYPE_COUNT) != 0 ||
        expected_index_count != GUNBROS_GAME_OBJECT_INDEX_COUNT ||
        !expected_indices ||
        memcmp(base + 0x30u, expected_indices,
               GUNBROS_GAME_OBJECT_INDEX_COUNT * sizeof(unsigned int)) != 0 ||
        !trace_is_game_range(objects, GUNBROS_GAME_OBJECT_TYPE_COUNT * 8u) ||
        !trace_is_game_range(load_flags, GUNBROS_GAME_OBJECT_TYPE_COUNT * 8u)) {
        return 0;
    }

    for (type = 0; type < GUNBROS_GAME_OBJECT_TYPE_COUNT; ++type) {
        const unsigned char *object_desc = objects + type * 8u;
        const unsigned char *flag_desc = load_flags + type * 8u;
        const void *object_table = *(const void * const *)(const void *)object_desc;
        const void *flag_table = *(const void * const *)(const void *)flag_desc;
        unsigned int count = base[0x14u + type];
        unsigned int object_count = *(const unsigned int *)(const void *)(object_desc + 4u);
        unsigned int flag_count = *(const unsigned int *)(const void *)(flag_desc + 4u);
        unsigned int first_resource =
            *(const unsigned int *)(const void *)(base + 0x30u + type * 4u);

        if (object_count != count || flag_count != count ||
            (count > 0u &&
             (!trace_is_game_range(object_table, (size_t)count * sizeof(void *)) ||
              !trace_is_game_range(flag_table, count) ||
              first_resource == UINT32_MAX))) {
            return 0;
        }
    }

    if (gun_count) {
        *gun_count = base[0x14u + GUNBROS_GAME_OBJECT_TYPE_GUN];
    }
    if (armor_count) {
        *armor_count = base[0x14u + GUNBROS_GAME_OBJECT_TYPE_ARMOR];
    }
    return 1;
}

#ifndef GUNBROS_QUIET_LOGS
static void report_gunbros_registry_tables(const void *self, const char *source) {
    static const void *last_self;
    static unsigned int last_pack_count = UINT32_MAX;
    static unsigned int last_ready_count = UINT32_MAX;
    static unsigned int last_gun_count = UINT32_MAX;
    static unsigned int last_armor_count = UINT32_MAX;
    static int last_first_pending = -2;
    const unsigned char *base = (const unsigned char *)self;
    const unsigned char *packs;
    unsigned int pack_count;
    unsigned int expected_pack_count;
    unsigned int expected_table_count = 0;
    unsigned int ready_count = 0;
    unsigned int total_guns = 0;
    unsigned int total_armor = 0;
    unsigned int expected_guns = 0;
    unsigned int expected_armor = 0;
    int first_pending = -1;
    unsigned int i;
    int complete;

    if (!gunbros_object_registry_is_valid(self)) {
        return;
    }
    packs = *(const unsigned char * const *)(const void *)(base + 0x138u);
    pack_count = *(const unsigned int *)(const void *)(base + 0x13cu);
    expected_pack_count = gunbros_get_pack_toc_expectation_count();

    for (i = 0; i < pack_count; ++i) {
        const unsigned char *expected_counts = NULL;
        const unsigned int *expected_indices = NULL;
        unsigned int expected_type_count = 0;
        unsigned int expected_index_count = 0;
        unsigned int guns = 0;
        unsigned int armor = 0;
        const unsigned char *pack = packs + i * 0xb8u;
        unsigned int pack_index = *(const unsigned short *)(const void *)pack;
        int has_expected_tables =
            gunbros_get_pack_game_object_tables((unsigned short)i,
                                                 &expected_counts,
                                                 &expected_type_count,
                                                 &expected_indices,
                                                 &expected_index_count);

        if (trace_is_game_range(pack, 0xb8u)) {
            total_guns += pack[0x14u + GUNBROS_GAME_OBJECT_TYPE_GUN];
            total_armor += pack[0x14u + GUNBROS_GAME_OBJECT_TYPE_ARMOR];
        }
        if (has_expected_tables &&
            expected_type_count == GUNBROS_GAME_OBJECT_TYPE_COUNT &&
            expected_index_count == GUNBROS_GAME_OBJECT_INDEX_COUNT) {
            ++expected_table_count;
            expected_guns += expected_counts[GUNBROS_GAME_OBJECT_TYPE_GUN];
            expected_armor += expected_counts[GUNBROS_GAME_OBJECT_TYPE_ARMOR];
        }

        if (pack_index == i &&
            gunbros_game_object_pack_tables_ready(pack,
                                                  expected_counts,
                                                  expected_type_count,
                                                  expected_indices,
                                                  expected_index_count,
                                                  &guns, &armor)) {
            ++ready_count;
        } else if (first_pending < 0) {
            first_pending = (int)i;
        }
    }

    if (last_self == self && last_pack_count == pack_count &&
        last_ready_count == ready_count && last_gun_count == total_guns &&
        last_armor_count == total_armor && last_first_pending == first_pending) {
        return;
    }
    last_self = self;
    last_pack_count = pack_count;
    last_ready_count = ready_count;
    last_gun_count = total_guns;
    last_armor_count = total_armor;
    last_first_pending = first_pending;
    complete = expected_pack_count > 0u &&
               pack_count == expected_pack_count &&
               expected_table_count == pack_count &&
               ready_count == pack_count &&
               total_guns == expected_guns &&
               total_armor == expected_armor;

    sceClibPrintf("[VERIFY-GOBJ][%s] source=%s self=%p packs=%u/%u expected=%u tables=%u guns=%u/%u armor=%u/%u first_pending=%d\n",
                  complete ? "OK" : "WAIT",
                  source ? source : "?", self, ready_count, pack_count,
                  expected_pack_count, expected_table_count,
                  total_guns, expected_guns, total_armor, expected_armor,
                  first_pending);
}

#endif

static void remember_live_gunbros_self(void *self, const char *source) {
    if (gunbros_object_registry_is_valid(self)) {
        if (g_live_gunbros_self != self && trace_allow("CGunBros/live-registry")) {
            sceClibPrintf("[FIX-GUNBROS] live registry from %s: %p (old=%p)\n",
                          source ? source : "?", self, g_live_gunbros_self);
        }
        g_live_gunbros_self = self;
#ifndef GUNBROS_QUIET_LOGS
        report_gunbros_registry_tables(self, source);
#endif
    }
}

static void *gunbros_registry_self_or_live(void *self,
                                           const char *label,
                                           unsigned int type,
                                           unsigned int id) {
    if (gunbros_object_registry_is_valid(self)) {
        return self;
    }

    if (gunbros_object_registry_is_valid(g_live_gunbros_self)) {
        if (trace_allow(label)) {
            sceClibPrintf("[FIX-GUNBROS] %s repaired self=%p -> live=%p type=%u id=%u\n",
                          label ? label : "registry", self, g_live_gunbros_self,
                          type, id);
        }
        return g_live_gunbros_self;
    }

    /* The latest dump returned from this helper directly into the stale
     * gti2SendKeepAlive +0x78 continuation. Reassert that continuation guard
     * immediately before returning, even if a queued callback restored the
     * original transport instructions after startup. */
    ensure_gti2_keepalive_offline_guards("registry-safe-miss");

    if (trace_allow(label)) {
        const void *packs = NULL;
        unsigned int count = 0;
        if (trace_is_game_range(self, 0x140u)) {
            packs = *(const void * const *)(const void *)
                ((const unsigned char *)self + 0x138u);
            count = *(const unsigned int *)(const void *)
                ((const unsigned char *)self + 0x13cu);
        }
        sceClibPrintf("[FIX-GUNBROS] %s no live registry self=%p packs=%p count=%u type=%u id=%u -> safe miss\n",
                      label ? label : "registry", self, packs, count, type, id);
    }
    return NULL;
}

typedef int (*gunbros_unflatten_object_index_fn_t)(void *self,
                                                    unsigned int type,
                                                    unsigned int flat_index,
                                                    uint16_t *out_index,
                                                    unsigned char *out_pack);

static int trace_gunbros_flatten_object_index_const(void *self,
                                                     unsigned int type,
                                                     unsigned int index,
                                                     unsigned int pack,
                                                     uint16_t *out_flat) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::FlattenObjectIndex/null-self",
                                                    type,
                                                    index);
    if (!safe_self) {
        if (trace_is_game_range(out_flat, sizeof(*out_flat))) {
            *out_flat = 0;
        }
        return 0;
    }

    GUNBROS_PERF_BEGIN(lookup_begin);
    int result;
    if (g_original_registry_flatten)
        result = ((int (*)(void *, unsigned, unsigned, unsigned, uint16_t *))g_original_registry_flatten)(safe_self, type, index, pack, out_flat);
    else
        result = SO_CONTINUE(int, h_gunbros_flatten_object_index_const, safe_self, type, index, pack, out_flat);
    GUNBROS_PERF_END(GUNBROS_STAGE_REGISTRY_LOOKUP, lookup_begin, 0);
    return result;
}

static unsigned int trace_gunbros_get_object_count_const(void *self, unsigned int type) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::GetObjectCount/null-self",
                                                    type,
                                                    0);
    if (!safe_self) {
        return 0;
    }

    GUNBROS_PERF_BEGIN(lookup_begin);
    unsigned result;
    if (g_original_registry_count)
        result = ((unsigned (*)(void *, unsigned))g_original_registry_count)(safe_self, type);
    else
        result = SO_CONTINUE(unsigned, h_gunbros_get_object_count_const, safe_self, type);
    GUNBROS_PERF_END(GUNBROS_STAGE_REGISTRY_LOOKUP, lookup_begin, 0);
    return result;
}

static int trace_gunbros_validate_game_object_3_const(void *self,
                                                       unsigned int type,
                                                       unsigned int index,
                                                       unsigned int pack) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::ValidateGameObject/null-self",
                                                    type,
                                                    index);
    if (!safe_self) {
        return 0;
    }

    GUNBROS_PERF_BEGIN(lookup_begin);
    int result;
    if (g_original_registry_validate)
        result = ((int (*)(void *, unsigned, unsigned, unsigned))g_original_registry_validate)(safe_self, type, index, pack);
    else
        result = SO_CONTINUE(int, h_gunbros_validate_game_object_3_const, safe_self, type, index, pack);
    GUNBROS_PERF_END(GUNBROS_STAGE_REGISTRY_LOOKUP, lookup_begin, 0);
    return result;
}

static int trace_gunbros_validate_game_object_ref_const(void *self,
                                                         unsigned int type,
                                                         const void *ref) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::ValidateGameObjectRef/null-self",
                                                    type,
                                                    0);
    if (!safe_self || !trace_is_game_range(ref, 8)) {
        if (trace_allow("CGunBros::ValidateGameObjectRef/bad-ref")) {
            sceClibPrintf("[FIX-GUNBROS] ValidateGameObject(ref) safe miss self=%p ref=%p type=%u\n",
                          self, ref, type);
        }
        return 0;
    }

    GUNBROS_PERF_BEGIN(lookup_begin);
    int result;
    if (g_original_registry_validate_ref)
        result = ((int (*)(void *, unsigned, const void *))g_original_registry_validate_ref)(safe_self, type, ref);
    else
        result = SO_CONTINUE(int, h_gunbros_validate_game_object_ref_const, safe_self, type, ref);
    GUNBROS_PERF_END(GUNBROS_STAGE_REGISTRY_LOOKUP, lookup_begin, 0);
    return result;
}

static void *trace_gunbros_get_game_object_pack(void *self,
                                                 unsigned int type,
                                                 unsigned int index,
                                                 unsigned int pack) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::GetGameObject(pack)/null-self",
                                                    type,
                                                    index);
    if (!safe_self) {
        return NULL;
    }

    GUNBROS_PERF_BEGIN(lookup_begin);
    void *result;
    if (g_original_registry_get)
        result = ((void *(*)(void *, unsigned, unsigned, unsigned))g_original_registry_get)(safe_self, type, index, pack);
    else
        result = SO_CONTINUE(void *, h_gunbros_get_game_object_pack, safe_self, type, index, pack);
    GUNBROS_PERF_END(GUNBROS_STAGE_REGISTRY_LOOKUP, lookup_begin, 0);
    return result;
}

static void *trace_gunbros_get_game_object_pack_const(void *self,
                                                       unsigned int type,
                                                       unsigned int index,
                                                       unsigned int pack) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::GetGameObject(pack const)/null-self",
                                                    type,
                                                    index);
    if (!safe_self) {
        return NULL;
    }

    GUNBROS_PERF_BEGIN(lookup_begin);
    void *result;
    if (g_original_registry_get_const)
        result = ((void *(*)(void *, unsigned, unsigned, unsigned))g_original_registry_get_const)(safe_self, type, index, pack);
    else
        result = SO_CONTINUE(void *, h_gunbros_get_game_object_pack_const, safe_self, type, index, pack);
    GUNBROS_PERF_END(GUNBROS_STAGE_REGISTRY_LOOKUP, lookup_begin, 0);
    return result;
}

static void trace_gunbros_load_game_object_req(void *self,
                                                unsigned int type,
                                                unsigned int flat_index,
                                                unsigned int flag) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::LoadGameObjectReq/null-self",
                                                    type,
                                                    flat_index);
    if (!safe_self) {
        return;
    }

    GUNBROS_PERF_BEGIN(ref_begin);
    if (g_original_registry_load_req)
        ((void (*)(void *, unsigned, unsigned, unsigned))g_original_registry_load_req)(safe_self, type, flat_index, flag);
    else
        (void)SO_CONTINUE(int, h_gunbros_load_game_object_req, safe_self, type, flat_index, flag);
    GUNBROS_PERF_END(GUNBROS_STAGE_RESOURCE_REF, ref_begin, 0);
}

static void trace_gunbros_free_game_object_req(void *self,
                                                unsigned int type,
                                                unsigned int flat_index,
                                                unsigned int flag) {
    void *safe_self = gunbros_registry_self_or_live(self,
                                                    "CGunBros::FreeGameObjectReq/null-self",
                                                    type,
                                                    flat_index);
    if (!safe_self) {
        return;
    }

    GUNBROS_PERF_BEGIN(ref_begin);
    if (g_original_registry_free_req)
        ((void (*)(void *, unsigned, unsigned, unsigned))g_original_registry_free_req)(safe_self, type, flat_index, flag);
    else
        (void)SO_CONTINUE(int, h_gunbros_free_game_object_req, safe_self, type, flat_index, flag);
    GUNBROS_PERF_END(GUNBROS_STAGE_RESOURCE_REF, ref_begin, 0);
}


static int challenge_manager_has_status_data(const void *self) {
    const unsigned char *base = (const unsigned char *)self;
    const void *status;

    if (!trace_is_game_range(self, 0x860)) {
        return 0;
    }

    status = *(const void * const *)(const void *)(base + 0x85c);
    return trace_is_game_range(status, 0x338);
}

static void trace_challenge_update_status_data(void *self, unsigned int player_index) {
    if (!challenge_manager_has_status_data(self)) {
        if (trace_allow("CChallengeManager::UpdateChallengeStatusData/missing_status")) {
            void *status = NULL;
            if (trace_is_game_range(self, 0x860)) {
                status = *(void **)(void *)((unsigned char *)self + 0x85c);
            }
            sceClibPrintf("[FIX-CHALLENGE] UpdateChallengeStatusData skipped self=%p player=%u status=%p\n",
                          self, player_index, status);
        }
        return;
    }

    (void)SO_CONTINUE(int, h_challenge_update_status_data, self, player_index);
}


static int trace_weapon_mastery_add_xp(void *self, void *gun, unsigned int pack, unsigned int variant, unsigned int hand, unsigned int xp, unsigned int reason) {
    const unsigned char *gun_base = (const unsigned char *)gun;
    const void *gun_xp_thresholds = NULL;
    unsigned int mastery_class = 0xffffffffu;

    if (!trace_is_game_range(self, 0x610)) {
        if (trace_allow("CWeaponMastery::AddXP/bad_self")) {
            sceClibPrintf("[FIX-MASTERY] AddXP skipped bad self=%p gun=%p pack=%u variant=%u xp=%u reason=%u\n",
                          self, gun, pack, variant, xp, reason);
        }
        return 0;
    }

    if (!trace_is_game_range(gun, 0xe0)) {
        if (trace_allow("CWeaponMastery::AddXP/bad_gun")) {
            sceClibPrintf("[FIX-MASTERY] AddXP skipped bad gun self=%p gun=%p pack=%u variant=%u xp=%u reason=%u\n",
                          self, gun, pack, variant, xp, reason);
        }
        return 0;
    }

    gun_xp_thresholds = *(const void * const *)(const void *)(gun_base + 0xa8);
    mastery_class = *(const unsigned int *)(const void *)(gun_base + 0xac);

    /* End-of-level/surrender awards call CWeaponMastery::AddXP. The offline
     * fallback weapon can be enough for shooting/rendering but still miss the
     * XP-threshold table at CGun+0xa8. Original AddXP indexes that table at
     * +0/+8 and crashes before its own null-entry fallback runs. Weapon XP is
     * optional for playability, so skip only this award when the table is bad. */
    if (!trace_is_game_range(gun_xp_thresholds, 12)) {
        if (trace_allow("CWeaponMastery::AddXP/no_xp_thresholds")) {
            sceClibPrintf("[FIX-MASTERY] AddXP skipped missing gun XP table self=%p gun=%p table=%p mastery_class=%u pack=%u variant=%u xp=%u reason=%u\n",
                          self, gun, gun_xp_thresholds, mastery_class, pack, variant, xp, reason);
        }
        return 0;
    }

    vita_weapon_mastery_prepare(self);
    int result = SO_CONTINUE(int, h_weapon_mastery_add_xp, self, gun, pack, variant, hand, xp, reason);
    (void)vita_weapon_mastery_save(self);
    return result;
}

static void trace_challenge_update_from_level_session(void *self, const void *level, const void *ref, unsigned int player_index) {
    if (!challenge_manager_has_status_data(self)) {
        if (trace_allow("CChallengeManager::UpdateFromLevelSession/missing_status")) {
            void *status = NULL;
            if (trace_is_game_range(self, 0x860)) {
                status = *(void **)(void *)((unsigned char *)self + 0x85c);
            }
            sceClibPrintf("[FIX-CHALLENGE] UpdateFromLevelSession skipped self=%p level=%p ref=%p player=%u status=%p\n",
                          self, level, ref, player_index, status);
        }
        return;
    }

    (void)SO_CONTINUE(int, h_challenge_update_from_level_session, self, level, ref, player_index);
}

static int trace_input_pad_bind(void *self, unsigned int mission, void *provider) {
    int ret = SO_CONTINUE(int, h_input_pad_bind, self, mission, provider);

    /* CInputPad owns two embedded ControlStick instances. Configure() stores
     * their authoritative WVGA centers at +0x220/+0x224 and radius at +0x234.
     * Feeding a DOWN outside the corresponding floating-spawn rectangle makes
     * the native pad ignore the stick completely, which was why the guessed
     * left-stick coordinates produced animation input but no movement. */
    if (trace_is_game_range(self, 0x1fe8u)) {
        const unsigned char *base = (const unsigned char *)self;
        const unsigned char *move = base + 0x1b60u;
        const unsigned char *fire = base + 0x1db0u;
        float move_x = *(const float *)(const void *)(move + 0x220u);
        float move_y = *(const float *)(const void *)(move + 0x224u);
        float move_r = *(const float *)(const void *)(move + 0x234u);
        float fire_x = *(const float *)(const void *)(fire + 0x220u);
        float fire_y = *(const float *)(const void *)(fire + 0x224u);
        float fire_r = *(const float *)(const void *)(fire + 0x234u);

        if (isfinite(move_x) && isfinite(move_y) && isfinite(move_r)) {
            gunbros_set_virtual_stick_layout(0u, move_x, move_y, move_r);
        }
        if (isfinite(fire_x) && isfinite(fire_y) && isfinite(fire_r)) {
            gunbros_set_virtual_stick_layout(1u, fire_x, fire_y, fire_r);
        }
        if (trace_allow_ex("CInputPad::Bind/layout", 12, 180)) {
            sceClibPrintf("[FIX-INPUT] Bind self=%p mission=%u provider=%p move=(%.1f,%.1f r=%.1f) fire=(%.1f,%.1f r=%.1f)\n",
                          self, mission, provider,
                          move_x, move_y, move_r, fire_x, fire_y, fire_r);
        }
    }

    return ret;
}

static void input_pad_release_stale_synthetic_stick(void *self,
                                                     unsigned int which,
                                                     int analog_active) {
    typedef void (*input_touch_release_fn_t)(int, int, int);
    typedef void (*control_stick_reset_fn_t)(void *);
    static input_touch_release_fn_t input_release;
    static control_stick_reset_fn_t reset_state;
    static control_stick_reset_fn_t reset;
    unsigned char *stick;
    int expected_id;
    int touch_id;
    int x;
    int y;

    if (analog_active || which >= 2u ||
        !trace_is_game_range(self, 0x2000u)) {
        return;
    }

    stick = (unsigned char *)self +
        (which == 0u ? 0x1b60u : 0x1db0u);
    expected_id = which == 0u ? 0x7f10 : 0x7f11;
    touch_id = *(const int *)(const void *)(stick + 0x21cu);
    if (touch_id != expected_id || stick[0x24cu] == 0u) {
        return;
    }

    if (!input_release) {
        input_release = (input_touch_release_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZN6CInput18HandleTouchReleaseEiii");
        reset_state = (control_stick_reset_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZN12ControlStick10ResetStateEv");
        reset = (control_stick_reset_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZN12ControlStick5ResetEv");
    }
    if (!input_release || !reset_state || !reset) {
        return;
    }

    x = (int)*(const float *)(const void *)(stick + 0x220u);
    y = (int)*(const float *)(const void *)(stick + 0x224u);
    input_release(x, y, expected_id);
    reset_state(stick);
    reset(stick);
}

#include "reimpl/native_touch.h"

static void trace_input_touch_down(void *self, int x, int y, int id) {
    if (trace_is_game_range(self, 0x3d4u))
        gunbros_native_touch(self, x, y, id, 1);
}

static void trace_input_touch_move(void *self, int x, int y, int id) {
    if (trace_is_game_range(self, 0x3d4u))
        gunbros_native_touch(self, x, y, id, 2);
}

static void trace_input_touch_release(void *self, int x, int y, int id) {
    if (trace_is_game_range(self, 0x3d4u))
        gunbros_native_touch(self, x, y, id, 3);
}

static int trace_input_pad_update_input(void *self, int dt) {
    typedef int (*input_pad_update_fn_t)(void *, int);
    GunBrosGameplayInputState input;
    int ret;

    if (trace_allow_ex("CInputPad::UpdateInput/diag", 16, 240)) {
        const unsigned char *base = (const unsigned char *)self;
        void *slot0 = NULL;
        void *slot1 = NULL;
        void *slot2 = NULL;
        void *slot3 = NULL;
        unsigned int consumed = 0xff;
        if (trace_is_game_range(self, 0x2590)) {
            slot0 = *(void **)(void *)(base + 0x2000);
            slot1 = *(void **)(void *)(base + 0x2004);
            slot2 = *(void **)(void *)(base + 0x2008);
            slot3 = *(void **)(void *)(base + 0x200c);
            consumed = base[0x258c];
        }
        sceClibPrintf("[DIAG-INPUTPAD] UpdateInput enter self=%p dt=%d slots={%p,%p,%p,%p} consumed=%u\n",
                      self, dt, slot0, slot1, slot2, slot3, consumed);
    }

    if (g_original_input_pad_update_input) {
        ret = ((input_pad_update_fn_t)g_original_input_pad_update_input)(self,
                                                                         dt);
    } else {
        ret = SO_CONTINUE(int, h_input_pad_update_input, self, dt);
    }

    /* Verify releases only after native UpdateInput has consumed this frame.
     * Running the repair both before and after UpdateInput made transient
     * input gaps more disruptive. The exact reserved IDs still prevent this
     * guard from touching physical touchscreen contacts. */
    input = g_gunbros_gameplay_input;
    input_pad_release_stale_synthetic_stick(self, 0u, input.move_active);
    input_pad_release_stale_synthetic_stick(self, 1u, input.fire_active);

    if (trace_allow_ex("CInputPad::UpdateInput/diag-leave", 16, 240)) {
        unsigned int consumed = 0xff;
        if (trace_is_game_range(self, 0x2590)) {
            consumed = ((const unsigned char *)self)[0x258c];
        }
        sceClibPrintf("[DIAG-INPUTPAD] UpdateInput leave self=%p ret=%d consumed=%u\n",
                      self, ret, consumed);
    }
    return ret;
}

static int trace_level_on_start(void *self) {
    int log = trace_allow("CLevel::OnStart");
    int ret;
    unsigned char *base = (unsigned char *)self;
    void *current = trace_is_game_range(self, 0x46ab0u + sizeof(void *)) ?
                    *(void **)(void *)(base + 0x46ab0) : NULL;

    /* CLevel's own "current" brother slot (this+0x46ab0) is never routed
     * through CBrother::Spawn, so trace_brother_spawn_construct never gets
     * a chance to fix it up - it reaches OnStart (and everything OnStart
     * calls: DrawEnemyHealthBars, DrawBrotherHealthBar, CRenderQueue::Add/
     * Draw, the render sort comparator, ...) with whatever stale vtable
     * bytes were already sitting in that memory (observed as 0x4cd0), and
     * every virtual call through it crashes. Construct it the same way we
     * construct Spawn's brother, before any of OnStart's body - including
     * the real OnStart call below - can dispatch a virtual method on it. */
    construct_brother_if_uninitialized(current, "CLevel::OnStart/pre-state-fix");

    if (log) {
        const void *vptr = trace_is_game_range(current, sizeof(void *)) ?
                           *(const void * const *)current : NULL;

        sceClibPrintf("[TRACE-LEVEL] enter CLevel::OnStart(this=%p, current=%p, vptr=%p)\n",
                      self, current, vptr);
    }

    ret = SO_CONTINUE(int, h_level_on_start, self);
    repair_brother_vptr_preserve_state(current, "CLevel::OnStart/current-vptr-repair", NULL);
    repair_brother_runtime_state(current, "CLevel::OnStart/current-runtime");

    if (log) {
        sceClibPrintf("[TRACE-LEVEL] leave CLevel::OnStart(this=%p) -> %d\n",
                      self, ret);
        trace_dump_brother_state("CLevel::OnStart/current-after", current);
    }

    return ret;
}

enum {
    /* BigViewer: pack0 type-16/subtype-0 resource 0x125 contains exactly
     * 201 XP entries and 201 level entries. CPlayerProgress::Init copies
     * those native runtime tables into 32-bit and 16-bit arrays here. */
    GUNBROS_PLAYER_PROGRESS_TABLE_COUNT = 201u
};

static int player_progress_has_exp_table(const void *self) {
    const unsigned char *base = (const unsigned char *)self;
    const void *xp_table;
    const void *level_table;
    unsigned int xp_count;
    unsigned int level_count;

    if (!trace_is_game_range(self, 0x60u)) {
        return 0;
    }

    xp_table = *(const void * const *)(const void *)(base + 0x08);
    xp_count = *(const unsigned int *)(const void *)(base + 0x0c);
    level_table = *(const void * const *)(const void *)(base + 0x10);
    level_count = *(const unsigned int *)(const void *)(base + 0x14);

    return xp_count == GUNBROS_PLAYER_PROGRESS_TABLE_COUNT &&
           level_count == GUNBROS_PLAYER_PROGRESS_TABLE_COUNT &&
           trace_is_game_range(xp_table, (size_t)xp_count * sizeof(uint32_t)) &&
           trace_is_game_range(level_table, (size_t)level_count * sizeof(uint16_t));
}

typedef void (*player_progress_init_fn_t)(void *self, void *gunbros);

static int player_progress_ensure_native_tables(void *self, const char *source) {
    unsigned char *base = (unsigned char *)self;
    void *xp_table;
    void *level_table;
    player_progress_init_fn_t init_fn;

    if (!trace_is_game_range(self, 0x60u)) {
        return 0;
    }
    if (player_progress_has_exp_table(self)) {
        return 1;
    }

    xp_table = *(void **)(void *)(base + 0x08u);
    level_table = *(void **)(void *)(base + 0x10u);
    if ((xp_table && !trace_is_game_ptr(xp_table)) ||
        (level_table && !trace_is_game_ptr(level_table))) {
        if (trace_allow_ex("CPlayerProgress::Init/bad-existing-table", 8, 180)) {
            sceClibPrintf("[FIX-PROGRESS] native table load rejected source=%s progress=%p xp=%p levels=%p\n",
                          source ? source : "?", self, xp_table, level_table);
        }
        return 0;
    }
    if (!gunbros_object_registry_is_valid(g_live_gunbros_self)) {
        return 0;
    }

    init_fn = (player_progress_init_fn_t)(uintptr_t)
        so_symbol(&so_mod, "_ZN15CPlayerProgress4InitEP8CGunBros");
    if (!init_fn) {
        return 0;
    }

    /* This is the game's real loader. It initializes type 0x10/subtype 0,
     * whose pack0 keyset entry resolves logical resource 0x125, then copies
     * all 201 XP deltas and all 201 level values into this progress object. */
    init_fn(self, g_live_gunbros_self);
    if (player_progress_has_exp_table(self)) {
        sceClibPrintf("[FIX-PROGRESS] loaded native XP tables source=%s progress=%p xp=%p count=%u levels=%p level_count=%u level=%u\n",
                      source ? source : "?", self,
                      *(void **)(void *)(base + 0x08u),
                      *(unsigned int *)(void *)(base + 0x0cu),
                      *(void **)(void *)(base + 0x10u),
                      *(unsigned int *)(void *)(base + 0x14u),
                      (unsigned int)*(unsigned short *)(void *)(base + 0x50u));
        return 1;
    }

    return 0;
}

static uint64_t trace_player_progress_get_experience_for_level(void *self) {
    typedef uint64_t (*get_experience_for_level_fn_t)(void *);

    GUNBROS_PERF_COUNT(xp_query_calls);
    if (!player_progress_has_exp_table(self) &&
        !player_progress_ensure_native_tables(self, "GetExperienceForLevel")) {
        if (trace_allow("CPlayerProgress::GetExperienceForLevel/null_table")) {
            sceClibPrintf("[PATCH-PROGRESS] GetExperienceForLevel(this=%p) -> 0; XP table unavailable\n", self);
        }
        return 0;
    }

    if (g_original_player_progress_get_experience_for_level) {
        return ((get_experience_for_level_fn_t)
                    g_original_player_progress_get_experience_for_level)(self);
    }
    return SO_CONTINUE(uint64_t, h_player_progress_get_experience_for_level,
                       self);
}

static uint32_t trace_player_progress_get_experience_delta(void *self) {
    typedef uint32_t (*get_experience_delta_fn_t)(void *);

    GUNBROS_PERF_COUNT(xp_query_calls);
    if (!player_progress_has_exp_table(self) &&
        !player_progress_ensure_native_tables(self, "GetExperienceDelta")) {
        if (trace_allow("CPlayerProgress::GetExperienceDelta/null_table")) {
            sceClibPrintf("[PATCH-PROGRESS] GetExperienceDelta(this=%p) -> 0; XP table unavailable\n", self);
        }
        return 0;
    }

    if (g_original_player_progress_get_experience_delta) {
        return ((get_experience_delta_fn_t)
                    g_original_player_progress_get_experience_delta)(self);
    }
    return SO_CONTINUE(uint32_t, h_player_progress_get_experience_delta, self);
}

static uint32_t trace_player_progress_get_percent_to_next_level(void *self) {
    typedef uint32_t (*get_percent_to_next_level_fn_t)(void *);

    GUNBROS_PERF_COUNT(xp_query_calls);
    if (!player_progress_has_exp_table(self) &&
        !player_progress_ensure_native_tables(self, "GetPercentToNextLevel")) {
        if (trace_allow("CPlayerProgress::GetPercentToNextLevel/null_table")) {
            sceClibPrintf("[PATCH-PROGRESS] GetPercentToNextLevel(this=%p) -> 0.0; XP table unavailable\n", self);
        }
        return 0;
    }

    if (g_original_player_progress_get_percent_to_next_level) {
        return ((get_percent_to_next_level_fn_t)
                    g_original_player_progress_get_percent_to_next_level)(self);
    }
    return SO_CONTINUE(uint32_t,
                       h_player_progress_get_percent_to_next_level, self);
}

static int trace_is_player_config_for_brother(const void *config) {
    return trace_is_game_range(config, 0x84u);
}

static int trace_is_player_progress_for_brother(const void *progress) {
    return trace_is_game_range(progress, 0x60u);
}

typedef void (*player_config_set_gun_ref_fn_t)(void *self, unsigned char slot, const void *ref);
typedef void (*player_config_set_armor_ref_fn_t)(void *self, const void *ref);

static unsigned char g_vita_default_player_config[0x84] __attribute__((aligned(4)));
static unsigned char g_vita_native_default_player_config[0x84] __attribute__((aligned(4)));
static unsigned char g_vita_default_player_progress[0x60] __attribute__((aligned(4)));
static int g_vita_default_profile_ready;
static int g_vita_default_config_native_ready;
static int g_vita_native_default_loadout_ready;
static void *g_vita_active_player_config;
static void *g_vita_active_player_progress;
static void *g_vita_native_player_config;
static void *g_vita_native_player_progress;
static int g_vita_native_profile_reconciled;
static void *g_vita_hydrated_player_config;
static void *g_vita_hydrated_player_progress;
static void *g_vita_post_loaded_player_config;
static void *g_vita_post_loaded_player_progress;
static unsigned char g_vita_loaded_config_payload[0x78] __attribute__((aligned(4)));
static unsigned char g_vita_loaded_progress_payload[0x38] __attribute__((aligned(4)));
static int g_vita_loaded_profile_valid;

enum {
    VITA_OFFLINE_OWNED_MAX = 256,
    VITA_PROFILE_MAGIC = 0x50564247u, /* "GBVP" */
    VITA_PROFILE_VERSION = 1,
    VITA_CONFIG_PAYLOAD_SIZE = 0x78,
    VITA_PROGRESS_PAYLOAD_SIZE = 0x38,
    VITA_CONFIG_REF_COUNT = 8,
};

typedef struct VitaOwnedGameObjectRef {
    uint16_t id;
    uint8_t subtype;
    uint8_t type;
} VitaOwnedGameObjectRef;

typedef struct VitaOfflineProfileDisk {
    uint32_t magic;
    uint32_t version;
    uint32_t config_size;
    uint32_t progress_size;
    uint32_t owned_count;
    uint32_t checksum;
    unsigned char config[VITA_CONFIG_PAYLOAD_SIZE];
    unsigned char progress[VITA_PROGRESS_PAYLOAD_SIZE];
    VitaOwnedGameObjectRef owned[VITA_OFFLINE_OWNED_MAX];
} VitaOfflineProfileDisk;

enum {
    VITA_LOADOUT_MAGIC = 0x4c564247u, /* "GBVL" */
    VITA_LOADOUT_VERSION = 2,
    VITA_LOADOUT_OLDEST_VERSION = 1,
};

/* Equipment gets a tiny journal in addition to the complete profile file.
 * Store selection can be the last action before the user closes the app, and
 * the full profile save is deliberately coalesced to keep flash I/O out of
 * menu input.  This journal contains only process-independent identities, so
 * it is cheap to commit immediately and safe to overlay during the next
 * complete-profile load. */
typedef struct VitaOfflineLoadoutDisk {
    uint32_t magic;
    uint32_t version;
    uint32_t checksum;
    uint16_t gun_id[2];
    uint8_t gun_subtype[2];
    uint8_t active_gun;
    uint8_t reserved0;
    uint16_t mastery_id[2];
    uint8_t mastery_subtype[2];
    uint8_t reserved1[2];
} VitaOfflineLoadoutDisk;

enum {
    /* The native collection implementations use fixed 64-entry arrays.  The
     * disassembly gives the exact state ranges: each starts at object+8 and
     * ends with its item count.  The vtable and save-owner pointer are kept
     * out of the Vita sidecar and remain owned by the live native object. */
    VITA_MISSION_SCORE_STATE_SIZE = 0x304,
    VITA_MISSION_OBJECTIVE_STATE_SIZE = 0x184,
    VITA_MISSION_WAVE_STATE_SIZE = 0x8284,
    VITA_MISSION_SCORE_COUNT_OFFSET = 0x308,
    VITA_MISSION_OBJECTIVE_COUNT_OFFSET = 0x188,
    VITA_MISSION_WAVE_COUNT_OFFSET = 0x8288,
    VITA_MISSIONS_MAGIC = 0x4d564247u, /* "GBVM" */
    VITA_MISSIONS_VERSION = 1,
};

typedef struct VitaOfflineMissionsDisk {
    uint32_t magic;
    uint32_t version;
    uint32_t score_size;
    uint32_t objective_size;
    uint32_t wave_size;
    uint32_t checksum;
    unsigned char score[VITA_MISSION_SCORE_STATE_SIZE];
    unsigned char objective[VITA_MISSION_OBJECTIVE_STATE_SIZE];
    unsigned char wave[VITA_MISSION_WAVE_STATE_SIZE];
} VitaOfflineMissionsDisk;

static VitaOwnedGameObjectRef g_vita_offline_owned[VITA_OFFLINE_OWNED_MAX];
static uint32_t g_vita_offline_owned_count;
static uint32_t g_vita_offline_profile_last_checksum;
static uint32_t g_vita_offline_loadout_last_checksum;
static int g_vita_offline_loadout_stage_attempted;
static int g_vita_offline_loadout_staged_valid;
static VitaOfflineLoadoutDisk g_vita_offline_loadout_staged;
static int g_vita_offline_profile_load_attempted;
static int g_vita_offline_profile_load_applied;
static int g_vita_offline_profile_staged_valid;
static VitaOfflineProfileDisk g_vita_offline_profile_staged;
static unsigned int g_vita_offline_profile_save_elapsed_ms;
static unsigned int g_vita_offline_profile_deferred_elapsed_ms;
static int g_vita_offline_profile_save_pending;
static unsigned int g_vita_offline_profile_generation = 1u;
static int g_vita_offline_profile_startup_owner_ready;
static unsigned int g_vita_profile_bulk_transition_depth;
static unsigned int g_vita_profile_bulk_save_calls;
static uint32_t g_vita_profile_bulk_save_types;
static const char *g_vita_profile_bulk_transition_source;
static VitaOfflineMissionsDisk g_vita_loaded_missions;
static void *g_vita_mission_high_score;
static void *g_vita_mission_objectives;
static void *g_vita_mission_waves;
static uint32_t g_vita_offline_missions_last_checksum;
static int g_vita_offline_missions_load_attempted;
static int g_vita_loaded_missions_valid;
static int g_vita_loaded_missions_hydrated;

static void vita_default_profile_try_native_load(const char *source);
static int vita_offline_profile_register_native_owner(void *config,
                                                      void *progress,
                                                      const char *source);
static int vita_offline_profile_discover_native_owner(const char *source);

static void vita_offline_profile_bump_generation(void) {
    g_vita_offline_profile_generation++;
    if (g_vita_offline_profile_generation == 0u) {
        g_vita_offline_profile_generation = 1u;
    }
}

static unsigned int vita_offline_profile_generation(void) {
    return g_vita_offline_profile_generation;
}

static int vita_offline_owned_has(unsigned int type,
                                  unsigned int id,
                                  unsigned int subtype) {
    uint32_t i;

    for (i = 0; i < g_vita_offline_owned_count; ++i) {
        const VitaOwnedGameObjectRef *entry = &g_vita_offline_owned[i];
        if (entry->type == (uint8_t)type &&
            entry->id == (uint16_t)id &&
            entry->subtype == (uint8_t)subtype) {
            return 1;
        }
    }
    return 0;
}

static int vita_offline_owned_add(unsigned int type,
                                  unsigned int id,
                                  unsigned int subtype) {
    VitaOwnedGameObjectRef *entry;

    if (type > 0xffu || id > 0xffffu || subtype >= 0xffu ||
        vita_offline_owned_has(type, id, subtype)) {
        return 0;
    }
    if (g_vita_offline_owned_count >= VITA_OFFLINE_OWNED_MAX) {
        if (trace_allow_ex("vita-profile/owned-full", 4, 180)) {
            sceClibPrintf("[SAVE-PROFILE] owned ledger full; rejected ref=%u:%u:%u\n",
                          type, id, subtype);
        }
        return 0;
    }

    entry = &g_vita_offline_owned[g_vita_offline_owned_count++];
    entry->type = (uint8_t)type;
    entry->id = (uint16_t)id;
    entry->subtype = (uint8_t)subtype;
    return 1;
}

enum {
    /* These are the exact id/subtype pairs written by the original
     * CPlayerConfiguration::Reset.  Once the object registry is live we copy
     * the complete native GameObjectRefs (including their pack hashes) from
     * g_vita_native_default_player_config instead of synthesizing them. */
    GUNBROS_DEFAULT_WHIPPERSNAPPERS_ID = 0,
    GUNBROS_DEFAULT_WHIPPERSNAPPERS_SUBTYPE = 0,
    GUNBROS_DEFAULT_SECOND_GUN_ID = 0,
    GUNBROS_DEFAULT_SECOND_GUN_SUBTYPE = 4,
    GUNBROS_DEFAULT_PANTS_ID = 0,
    GUNBROS_DEFAULT_PANTS_SUBTYPE = 2,
    GUNBROS_DEFAULT_VEST_ID = 0,
    GUNBROS_DEFAULT_VEST_SUBTYPE = 1,
    GUNBROS_DEFAULT_NO_HELMET_ID = 0,
    GUNBROS_DEFAULT_NO_HELMET_SUBTYPE = 0,
};

static void player_config_set_ref(unsigned char *config, unsigned int off,
                                  unsigned short id, unsigned char subtype) {
    config[off + 0] = (unsigned char)(id & 0xffu);
    config[off + 1] = (unsigned char)((id >> 8) & 0xffu);
    config[off + 2] = subtype;
}

static int player_config_repair_loadout_defaults(void *config, const char *source) {
    unsigned char *cfg = (unsigned char *)config;
    static const unsigned int required_ref_offsets[] = {
        0x0cu, 0x14u, 0x2cu, 0x34u, 0x3cu
    };
    static const unsigned short fallback_ids[] = {
        GUNBROS_DEFAULT_WHIPPERSNAPPERS_ID,
        GUNBROS_DEFAULT_SECOND_GUN_ID,
        GUNBROS_DEFAULT_PANTS_ID,
        GUNBROS_DEFAULT_VEST_ID,
        GUNBROS_DEFAULT_NO_HELMET_ID
    };
    static const unsigned char fallback_subtypes[] = {
        GUNBROS_DEFAULT_WHIPPERSNAPPERS_SUBTYPE,
        GUNBROS_DEFAULT_SECOND_GUN_SUBTYPE,
        GUNBROS_DEFAULT_PANTS_SUBTYPE,
        GUNBROS_DEFAULT_VEST_SUBTYPE,
        GUNBROS_DEFAULT_NO_HELMET_SUBTYPE
    };
    unsigned int i;
    int changed = 0;

    if (!trace_is_player_config_for_brother(config)) {
        return 0;
    }

    /* Preserve every valid saved choice. Fill only absent required slots:
     * both starter guns, combat pants, combat vest, and the native
     * no-helmet object. The fourth armor/trinket slot is intentionally
     * optional and is never replaced here. */
    for (i = 0; i < sizeof(required_ref_offsets) / sizeof(required_ref_offsets[0]); ++i) {
        unsigned int ref_off = required_ref_offsets[i];
        if (cfg[ref_off + 6u] != 0xffu) {
            continue;
        }
        if (g_vita_native_default_loadout_ready) {
            sceClibMemcpy(cfg + ref_off,
                          g_vita_native_default_player_config + ref_off,
                          8u);
        } else {
            memset(cfg + ref_off, 0, 8u);
            player_config_set_ref(cfg, ref_off + 4u,
                                  fallback_ids[i], fallback_subtypes[i]);
        }
        changed = 1;
    }

    /* Gun mastery refs are paired with the two gun slots. A legacy save made
     * by the one-gun patch cleared slot 1's mastery along with the gun. */
    if (cfg[0x22u] == 0xffu) {
        if (g_vita_native_default_loadout_ready) {
            sceClibMemcpy(cfg + 0x1cu,
                          g_vita_native_default_player_config + 0x1cu, 8u);
        } else {
            memset(cfg + 0x1cu, 0, 8u);
            player_config_set_ref(cfg, 0x20u, 0u, 0u);
        }
        changed = 1;
    }
    if (cfg[0x2au] == 0xffu) {
        if (g_vita_native_default_loadout_ready) {
            sceClibMemcpy(cfg + 0x24u,
                          g_vita_native_default_player_config + 0x24u, 8u);
        } else {
            memset(cfg + 0x24u, 0, 8u);
            player_config_set_ref(cfg, 0x28u, 0u, 0x2du);
        }
        changed = 1;
    }

    if (cfg[0x4cu] >= 2u ||
        cfg[0x10u + (unsigned int)cfg[0x4cu] * 8u + 2u] == 0xffu) {
        cfg[0x4cu] = 0u;
        changed = 1;
    }

    if (changed && trace_allow_ex("CPlayerConfiguration/loadout-defaults", 24, 180)) {
        sceClibPrintf("[FIX-CONFIG] restored missing native loadout defaults source=%s config=%p active=%u\n",
                      source ? source : "?", config, (unsigned int)cfg[0x4cu]);
    }
    return changed;
}

static void player_config_set_store_ref(unsigned char *config,
                                        unsigned int id_off,
                                        const unsigned char *ref,
                                        unsigned short id,
                                        unsigned char subtype) {
    if (id_off >= 4u && trace_is_game_range(ref, 8u)) {
        sceClibMemcpy(config + id_off - 4u, ref, 8u);
    }
    player_config_set_ref(config, id_off, id, subtype);
}

static player_config_set_gun_ref_fn_t player_config_set_gun_ref_fn(void) {
    static player_config_set_gun_ref_fn_t fn;
    static int looked_up;

    if (!looked_up) {
        fn = (player_config_set_gun_ref_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZN20CPlayerConfiguration6SetGunEhRKN11IGameObject13GameObjectRefE");
        looked_up = 1;
        if (!fn) {
            sceClibPrintf("[FIX-STORE] CPlayerConfiguration::SetGun symbol missing\n");
        }
    }
    return fn;
}

static player_config_set_armor_ref_fn_t player_config_set_armor_ref_fn(void) {
    static player_config_set_armor_ref_fn_t fn;
    static int looked_up;

    if (!looked_up) {
        fn = (player_config_set_armor_ref_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZN20CPlayerConfiguration8SetArmorERKN11IGameObject13GameObjectRefE");
        looked_up = 1;
        if (!fn) {
            sceClibPrintf("[FIX-STORE] CPlayerConfiguration::SetArmor symbol missing\n");
        }
    }
    return fn;
}

static void trace_dump_player_config_summary(const char *label, const void *config) {
    const unsigned char *c = (const unsigned char *)config;

    if (!trace_is_player_config_for_brother(config)) {
        sceClibPrintf("[DIAG-CONFIG] %s config=%p INVALID\n",
                      label ? label : "(null)", config);
        return;
    }

    sceClibPrintf("[DIAG-CONFIG] %s config=%p guns=[%u:%u,%u:%u] active=%u armor=[%u:%u,%u:%u,%u:%u,%u:%u]\n",
                  label ? label : "(null)", config,
                  (unsigned int)(c[0x10] | (c[0x11] << 8)), (unsigned int)c[0x12],
                  (unsigned int)(c[0x18] | (c[0x19] << 8)), (unsigned int)c[0x1a],
                  (unsigned int)c[0x4c],
                  (unsigned int)(c[0x30] | (c[0x31] << 8)), (unsigned int)c[0x32],
                  (unsigned int)(c[0x38] | (c[0x39] << 8)), (unsigned int)c[0x3a],
                  (unsigned int)(c[0x40] | (c[0x41] << 8)), (unsigned int)c[0x42],
                  (unsigned int)(c[0x48] | (c[0x49] << 8)), (unsigned int)c[0x4a]);
}

static void vita_offline_owned_capture_equipped(const void *config) {
    static const unsigned int gun_offsets[2] = { 0x10u, 0x18u };
    static const unsigned int armor_offsets[4] = { 0x30u, 0x38u, 0x40u, 0x48u };
    const unsigned char *cfg = (const unsigned char *)config;
    unsigned int i;

    if (!trace_is_player_config_for_brother(config)) {
        return;
    }

    for (i = 0; i < 2u; ++i) {
        unsigned int off = gun_offsets[i];
        if (cfg[off + 2u] != 0xffu) {
            (void)vita_offline_owned_add(GUNBROS_OBJECT_TYPE_GUN,
                                         (unsigned int)(cfg[off] | (cfg[off + 1u] << 8)),
                                         cfg[off + 2u]);
        }
    }
    for (i = 0; i < 4u; ++i) {
        unsigned int off = armor_offsets[i];
        if (cfg[off + 2u] != 0xffu) {
            (void)vita_offline_owned_add(GUNBROS_OBJECT_TYPE_ARMOR,
                                         (unsigned int)(cfg[off] | (cfg[off + 1u] << 8)),
                                         cfg[off + 2u]);
        }
    }
}

static uint32_t vita_profile_checksum(const void *data, size_t size) {
    const unsigned char *bytes = (const unsigned char *)data;
    uint32_t hash = 2166136261u;
    size_t i;

    for (i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash ? hash : 1u;
}

static void vita_offline_loadout_fill(VitaOfflineLoadoutDisk *disk,
                                      const void *config) {
    const unsigned char *cfg = (const unsigned char *)config;

    memset(disk, 0, sizeof(*disk));
    disk->magic = VITA_LOADOUT_MAGIC;
    disk->version = VITA_LOADOUT_VERSION;
    disk->gun_id[0] = *(const uint16_t *)(const void *)(cfg + 0x10u);
    disk->gun_id[1] = *(const uint16_t *)(const void *)(cfg + 0x18u);
    disk->gun_subtype[0] = cfg[0x12u];
    disk->gun_subtype[1] = cfg[0x1au];
    disk->active_gun = cfg[0x4cu] & 1u;
    disk->mastery_id[0] = *(const uint16_t *)(const void *)(cfg + 0x20u);
    disk->mastery_id[1] = *(const uint16_t *)(const void *)(cfg + 0x28u);
    disk->mastery_subtype[0] = cfg[0x22u];
    disk->mastery_subtype[1] = cfg[0x2au];
    disk->checksum = 0u;
    disk->checksum = vita_profile_checksum(disk, sizeof(*disk));
}

static int vita_offline_loadout_save(const void *config,
                                     const char *source) {
    static const char final_path[] = DATA_PATH "vita_loadout_v1.dat";
    static const char temp_path[] = DATA_PATH "vita_loadout_v1.tmp";
    VitaOfflineLoadoutDisk disk;
    uint64_t begin_us;
    uint64_t end_us;
    FILE *fp;
    size_t written;

    if (!trace_is_player_config_for_brother(config)) {
        return 0;
    }
    vita_offline_loadout_fill(&disk, config);
    if (disk.checksum == g_vita_offline_loadout_last_checksum) {
        return 1;
    }

    begin_us = sceKernelGetProcessTimeWide();
    fp = fopen(temp_path, "wb");
    if (!fp) {
        GUNBROS_PERF_LOG("[PROFILE-LOADOUT] save-error source=%s stage=open path=%s\n",
                         source ? source : "?", temp_path);
        return 0;
    }
    written = fwrite(&disk, 1, sizeof(disk), fp);
    (void)fflush(fp);
    if (fclose(fp) != 0 || written != sizeof(disk)) {
        (void)remove(temp_path);
        GUNBROS_PERF_LOG("[PROFILE-LOADOUT] save-error source=%s stage=write bytes=%u/%u\n",
                         source ? source : "?", (unsigned int)written,
                         (unsigned int)sizeof(disk));
        return 0;
    }
    if (rename(temp_path, final_path) != 0) {
        (void)remove(final_path);
        if (rename(temp_path, final_path) != 0) {
            (void)remove(temp_path);
            GUNBROS_PERF_LOG("[PROFILE-LOADOUT] save-error source=%s stage=rename path=%s\n",
                             source ? source : "?", final_path);
            return 0;
        }
    }

    end_us = sceKernelGetProcessTimeWide();
    g_vita_offline_loadout_last_checksum = disk.checksum;
    g_vita_offline_loadout_staged = disk;
    g_vita_offline_loadout_stage_attempted = 1;
    g_vita_offline_loadout_staged_valid = 1;
    GUNBROS_PERF_LOG("[PROFILE-LOADOUT] saved source=%s guns=%u:%u,%u:%u active=%u checksum=%08x elapsed_us=%llu\n",
                     source ? source : "?",
                     (unsigned int)disk.gun_id[0],
                     (unsigned int)disk.gun_subtype[0],
                     (unsigned int)disk.gun_id[1],
                     (unsigned int)disk.gun_subtype[1],
                     (unsigned int)disk.active_gun, disk.checksum,
                     (unsigned long long)(end_us >= begin_us ?
                         end_us - begin_us : 0u));
    return 1;
}

static int vita_offline_loadout_stage(void) {
    static const char final_path[] = DATA_PATH "vita_loadout_v1.dat";
    uint32_t stored;
    uint32_t expected;
    FILE *fp;
    size_t read_size;

    if (g_vita_offline_loadout_stage_attempted) {
        return g_vita_offline_loadout_staged_valid;
    }
    g_vita_offline_loadout_stage_attempted = 1;
    sceClibMemset(&g_vita_offline_loadout_staged, 0,
                  sizeof(g_vita_offline_loadout_staged));
    fp = fopen(final_path, "rb");
    if (!fp) {
        GUNBROS_PERF_LOG("[PROFILE-LOADOUT] missing path=%s\n", final_path);
        return 0;
    }
    read_size = fread(&g_vita_offline_loadout_staged, 1,
                      sizeof(g_vita_offline_loadout_staged), fp);
    (void)fclose(fp);
    stored = g_vita_offline_loadout_staged.checksum;
    g_vita_offline_loadout_staged.checksum = 0u;
    expected = vita_profile_checksum(&g_vita_offline_loadout_staged,
                                     sizeof(g_vita_offline_loadout_staged));
    if (read_size != sizeof(g_vita_offline_loadout_staged) ||
        g_vita_offline_loadout_staged.magic != VITA_LOADOUT_MAGIC ||
        g_vita_offline_loadout_staged.version <
            VITA_LOADOUT_OLDEST_VERSION ||
        g_vita_offline_loadout_staged.version > VITA_LOADOUT_VERSION ||
        stored != expected ||
        g_vita_offline_loadout_staged.active_gun >= 2u ||
        g_vita_offline_loadout_staged.gun_subtype[0] == 0xffu ||
        g_vita_offline_loadout_staged.gun_subtype[1] == 0xffu) {
        GUNBROS_PERF_LOG("[PROFILE-LOADOUT] rejected bytes=%u magic=%08x version=%u active=%u stored=%08x expected=%08x\n",
                         (unsigned int)read_size,
                         g_vita_offline_loadout_staged.magic,
                         g_vita_offline_loadout_staged.version,
                         (unsigned int)g_vita_offline_loadout_staged.active_gun,
                         stored, expected);
        return 0;
    }
    if (g_vita_offline_loadout_staged.version == 1u) {
        unsigned int active = g_vita_offline_loadout_staged.active_gun;
        unsigned int other = active ^ 1u;
        int active_is_starter =
            g_vita_offline_loadout_staged.gun_id[active] == 0u &&
            (g_vita_offline_loadout_staged.gun_subtype[active] ==
                 GUNBROS_DEFAULT_WHIPPERSNAPPERS_SUBTYPE ||
             g_vita_offline_loadout_staged.gun_subtype[active] ==
                 GUNBROS_DEFAULT_SECOND_GUN_SUBTYPE);
        int other_is_starter =
            g_vita_offline_loadout_staged.gun_id[other] == 0u &&
            (g_vita_offline_loadout_staged.gun_subtype[other] ==
                 GUNBROS_DEFAULT_WHIPPERSNAPPERS_SUBTYPE ||
             g_vita_offline_loadout_staged.gun_subtype[other] ==
                 GUNBROS_DEFAULT_SECOND_GUN_SUBTYPE);

        /* v1 could save a temporary starter fallback when the selected gun
         * had not loaded yet. Recover the usual affected case once; v2 never
         * changes the persisted slot during asynchronous rehydration. */
        if (active_is_starter && !other_is_starter) {
            g_vita_offline_loadout_staged.active_gun = (uint8_t)other;
            GUNBROS_PERF_LOG("[PROFILE-LOADOUT] migrated-v1 active=%u->%u starter-fallback=1\n",
                             active, other);
        }
    }
    g_vita_offline_loadout_staged.checksum = stored;
    g_vita_offline_loadout_last_checksum = stored;
    g_vita_offline_loadout_staged_valid = 1;
    GUNBROS_PERF_LOG("[PERF-PROFILE] event=loadout-staged bytes=%u checksum=%08x\n",
                     (unsigned int)read_size, stored);
    return 1;
}

static int vita_offline_loadout_overlay(unsigned char *config_payload) {
    const VitaOfflineLoadoutDisk *disk = &g_vita_offline_loadout_staged;

    if (!config_payload || !vita_offline_loadout_stage()) {
        return 0;
    }

    /* config_payload starts at CPlayerConfiguration+0x0c.  Preserve no
     * launch-specific hash words; the existing config-ref rebuild restores
     * those from the live TOC before CPlayerConfiguration::Init. */
    *(uint32_t *)(void *)(config_payload + 0x00u) = 0u;
    *(uint16_t *)(void *)(config_payload + 0x04u) = disk->gun_id[0];
    config_payload[0x06u] = disk->gun_subtype[0];
    *(uint32_t *)(void *)(config_payload + 0x08u) = 0u;
    *(uint16_t *)(void *)(config_payload + 0x0cu) = disk->gun_id[1];
    config_payload[0x0eu] = disk->gun_subtype[1];
    *(uint32_t *)(void *)(config_payload + 0x10u) = 0u;
    *(uint16_t *)(void *)(config_payload + 0x14u) = disk->mastery_id[0];
    config_payload[0x16u] = disk->mastery_subtype[0];
    *(uint32_t *)(void *)(config_payload + 0x18u) = 0u;
    *(uint16_t *)(void *)(config_payload + 0x1cu) = disk->mastery_id[1];
    config_payload[0x1eu] = disk->mastery_subtype[1];
    config_payload[0x40u] = disk->active_gun;
    GUNBROS_PERF_LOG("[PROFILE-LOADOUT] loaded guns=%u:%u,%u:%u active=%u checksum=%08x\n",
                     (unsigned int)disk->gun_id[0],
                     (unsigned int)disk->gun_subtype[0],
                     (unsigned int)disk->gun_id[1],
                     (unsigned int)disk->gun_subtype[1],
                     (unsigned int)disk->active_gun,
                     g_vita_offline_loadout_last_checksum);
    return 1;
}

/* CPlayerConfiguration stores eight GameObjectRefs contiguously at +0x0c.
 * The first word is the process-independent pack-name hash, but it must still
 * be reconstructed through the current CResTOCManager so native equality and
 * ReconcilePackIdx see the exact representation they create.  The following
 * pack-index/object-index bytes are the stable lookup identity persisted by
 * the sidecar. */
static const unsigned int g_vita_profile_ref_types[VITA_CONFIG_REF_COUNT] = {
    GUNBROS_OBJECT_TYPE_GUN, GUNBROS_OBJECT_TYPE_GUN,
    3u, 3u,
    GUNBROS_OBJECT_TYPE_ARMOR, GUNBROS_OBJECT_TYPE_ARMOR,
    GUNBROS_OBJECT_TYPE_ARMOR, GUNBROS_OBJECT_TYPE_ARMOR
};

static void vita_profile_sanitize_config_payload(unsigned char *payload) {
    unsigned int i;

    if (!payload) {
        return;
    }
    for (i = 0; i < VITA_CONFIG_REF_COUNT; ++i) {
        memset(payload + i * 8u, 0, sizeof(void *));
    }
}

static void vita_profile_capture_config_payload(unsigned char *payload,
                                                const void *config) {
    if (!payload || !trace_is_player_config_for_brother(config)) {
        return;
    }
    sceClibMemcpy(payload, (const unsigned char *)config + 0x0cu,
                  VITA_CONFIG_PAYLOAD_SIZE);
    vita_profile_sanitize_config_payload(payload);
}

static void vita_profile_apply_config_payload(void *config,
                                              const unsigned char *payload) {
    unsigned char *cfg = (unsigned char *)config;
    uint32_t native_empty_hash[VITA_CONFIG_REF_COUNT];
    unsigned int i;

    if (!trace_is_player_config_for_brother(config) || !payload) {
        return;
    }

    for (i = 0; i < VITA_CONFIG_REF_COUNT; ++i) {
        native_empty_hash[i] = *(const uint32_t *)(const void *)
            (cfg + 0x0cu + i * 8u);
    }
    sceClibMemcpy(cfg + 0x0cu, payload, VITA_CONFIG_PAYLOAD_SIZE);

    for (i = 0; i < VITA_CONFIG_REF_COUNT; ++i) {
        unsigned int off = 0x0cu + i * 8u;
        unsigned int subtype = cfg[off + 6u];

        /* Native Reset supplies the core pack hash for empty refs. Preserve
         * that scalar hash (it is not a pointer); non-empty refs are rebuilt
         * from their saved pack index through the live TOC manager below. */
        *(uint32_t *)(void *)(cfg + off) =
            subtype == 0xffu ? native_empty_hash[i] : 0u;
    }
}

static void *vita_profile_resolve_object_ref(unsigned int type,
                                             unsigned int id,
                                             unsigned int subtype) {
    void *self = g_live_gunbros_self;
    void *object;
    uint16_t flat = 0;

    if (subtype == 0xffu ||
        !gunbros_object_registry_is_valid(self) ||
        !h_gunbros_get_game_object_pack_const.addr) {
        return NULL;
    }

    object = trace_gunbros_get_game_object_pack_const(self, type, id, subtype);
    if (trace_is_game_ptr(object)) {
        return object;
    }

    if (h_gunbros_flatten_object_index_const.addr &&
        h_gunbros_load_game_object_req.addr &&
        trace_gunbros_flatten_object_index_const(self, type, id, subtype,
                                                 &flat)) {
        trace_gunbros_load_game_object_req(self, type, flat, 1u);
        object = trace_gunbros_get_game_object_pack_const(self, type, id,
                                                          subtype);
        if (trace_is_game_ptr(object)) {
            return object;
        }
    }
    return NULL;
}

static int vita_profile_restore_ref_pack_hash(unsigned char *ref) {
    typedef uint32_t (*get_pack_hash_fn_t)(const void *self,
                                           unsigned short pack_index);
    static get_pack_hash_fn_t get_pack_hash;
    unsigned int pack_index;
    unsigned int pack_count;
    uint32_t pack_hash;

    if (!ref || !trace_is_game_range(g_live_res_toc_manager, 0x228u)) {
        return 0;
    }

    pack_index = *(const uint16_t *)(const void *)(ref + 4u);
    pack_count = *(const uint32_t *)(const void *)
        ((const unsigned char *)g_live_res_toc_manager + 0x224u);
    if (pack_count == 0u || pack_count > 256u || pack_index >= pack_count) {
        return 0;
    }

    if (!get_pack_hash) {
        get_pack_hash = (get_pack_hash_fn_t)(uintptr_t)so_symbol(
            &so_mod,
            "_ZN14CResTOCManager20GetPackHashFromIndexEt");
    }
    if (!get_pack_hash) {
        return 0;
    }

    pack_hash = get_pack_hash(g_live_res_toc_manager,
                              (unsigned short)pack_index);
    if (pack_hash == 0u) {
        return 0;
    }

    *(uint32_t *)(void *)ref = pack_hash;
    return 1;
}

static int vita_profile_rebuild_config_refs(void *config,
                                            const char *source) {
    unsigned char *cfg = (unsigned char *)config;
    unsigned int i;
    int ready = 1;

    if (!trace_is_player_config_for_brother(config)) {
        return 0;
    }

    for (i = 0; i < VITA_CONFIG_REF_COUNT; ++i) {
        unsigned int off = 0x0cu + i * 8u;
        unsigned int id = (unsigned int)(cfg[off + 4u] |
                                         (cfg[off + 5u] << 8));
        unsigned int subtype = cfg[off + 6u];
        void *object;

        if (subtype == 0xffu) {
            /* Empty refs are never resolved as game objects. Their hash is
             * irrelevant to GetGameObject, but restoring it when the saved
             * pack index is valid keeps native equality/reconcile behavior. */
            if (*(uint32_t *)(void *)(cfg + off) == 0u) {
                (void)vita_profile_restore_ref_pack_hash(cfg + off);
            }
            continue;
        }

        object = vita_profile_resolve_object_ref(
            g_vita_profile_ref_types[i], id, subtype);
        /* GameObjectRef+0 is the original pack-name hash, not the resolved
         * CArmor/CGun Template pointer.  IsArmorEquipped, IsGunEquipped and
         * CPlayerConfiguration::Init all consume that hash.  Storing the
         * template here made restored helmets numerically correct but
         * unequal to store refs and could make ReconcilePackIdx rewrite the
         * saved pack index from an unrelated pointer value. */
        if (!vita_profile_restore_ref_pack_hash(cfg + off) || !object) {
            ready = 0;
            if (trace_allow_ex("vita-profile/unresolved-ref", 24, 180)) {
                sceClibPrintf("[SAVE-PROFILE] deferred ref rebuild source=%s config=%p slot=%u type=%u ref=%u:%u\n",
                              source ? source : "?", config, i,
                              g_vita_profile_ref_types[i], id, subtype);
            }
        }
    }
    return ready;
}

static const void *vita_offline_profile_config_source(void) {
    if (trace_is_player_config_for_brother(g_vita_active_player_config)) {
        return g_vita_active_player_config;
    }
    return g_vita_default_player_config;
}

static const void *vita_offline_profile_progress_source(void) {
    if (trace_is_player_progress_for_brother(g_vita_active_player_progress)) {
        return g_vita_active_player_progress;
    }
    return g_vita_default_player_progress;
}

static int vita_offline_profile_get_native_pair(void **out_config,
                                                void **out_progress) {
    int valid = trace_is_player_config_for_brother(g_vita_native_player_config) &&
                trace_is_player_progress_for_brother(g_vita_native_player_progress);

    if (out_config) {
        *out_config = valid ? g_vita_native_player_config : NULL;
    }
    if (out_progress) {
        *out_progress = valid ? g_vita_native_player_progress : NULL;
    }
    return valid;
}

static void vita_offline_profile_get_best_local_pair(void **out_config,
                                                     void **out_progress) {
    void *config = NULL;
    void *progress = NULL;

    if (!g_vita_offline_profile_startup_owner_ready) {
        vita_default_profile_try_native_load("best-local-pair");
        (void)vita_offline_profile_discover_native_owner("best-local-pair");
    }
    if (!vita_offline_profile_get_native_pair(&config, &progress)) {
        config = (void *)vita_offline_profile_config_source();
        progress = (void *)vita_offline_profile_progress_source();
    }
    if (out_config) {
        *out_config = config;
    }
    if (out_progress) {
        *out_progress = progress;
    }
}

static void vita_offline_profile_post_load(void *config, void *progress,
                                           const char *source) {
    typedef void (*config_init_fn_t)(void *self);
    typedef void (*progress_update_fn_t)(const void *self);
    config_init_fn_t config_init;
    progress_update_fn_t progress_update;

    config_init = (config_init_fn_t)(uintptr_t)
        so_symbol(&so_mod, "_ZN20CPlayerConfiguration4InitEv");
    progress_update = (progress_update_fn_t)(uintptr_t)
        so_symbol(&so_mod, "_ZNK15CPlayerProgress12ProgressData20UpdateContentTrackerEv");
    if (trace_is_player_config_for_brother(config)) {
        /* GameObjectRef::Init dereferences the live CGunBros object registry.
         * The sidecar must be readable before that registry exists so menus
         * can display the saved wallet/level, but its native post-read step
         * must wait until the registry is verified. */
        if (config_init &&
            gunbros_object_registry_is_valid(g_live_gunbros_self) &&
            vita_profile_rebuild_config_refs(config, source)) {
            config_init(config);
            g_vita_post_loaded_player_config = config;
        }
        /* Move legacy armor refs into their native typed slots before filling
         * an absent pants/vest/helmet slot with its starter default. */
        (void)player_config_repair_armor_slots(config, source);
        (void)player_config_repair_loadout_defaults(config, source);
        vita_offline_owned_capture_equipped(config);
    }
    if (trace_is_player_progress_for_brother(progress)) {
        if (progress_update &&
            gunbros_object_registry_is_valid(g_live_gunbros_self)) {
            progress_update((unsigned char *)progress + 0x28u);
            g_vita_post_loaded_player_progress = progress;
        }
        (void)player_progress_ensure_native_tables(progress, source);
    }
}

static int vita_offline_loadout_restore_config(void *config,
                                               const char *source) {
    unsigned char payload[VITA_CONFIG_PAYLOAD_SIZE];

    if (!trace_is_player_config_for_brother(config) ||
        !vita_offline_loadout_stage()) {
        return 0;
    }

    vita_profile_capture_config_payload(payload, config);
    if (!vita_offline_loadout_overlay(payload)) {
        return 0;
    }

    vita_profile_apply_config_payload(config, payload);
    vita_offline_profile_post_load(config, NULL,
                                   source ? source : "loadout-restore");

    /* Keep future owner hydration from reapplying a starter-gun generation
     * after the level-local configuration has been repaired. Only the two gun
     * refs, their mastery refs, and the active slot belong to this journal. */
    if (g_vita_loaded_profile_valid) {
        sceClibMemcpy(g_vita_loaded_config_payload + 0x00u,
                      payload + 0x00u, 0x20u);
        g_vita_loaded_config_payload[0x40u] = payload[0x40u];
    }
    g_vita_hydrated_player_config = config;
    vita_offline_profile_bump_generation();
    return 1;
}

static int vita_offline_profile_config_is_authoritative(const void *config) {
    return trace_is_player_config_for_brother(config) &&
           (config == g_vita_active_player_config ||
            config == g_vita_native_player_config ||
            config == g_vita_default_player_config);
}

/* CPlayerProgress and CPlayerConfiguration returned by the native profile
 * manager are not necessarily the synthetic objects used during offline
 * bootstrap.  Record the verified live pair and hydrate each new live object
 * exactly once from the Vita save.  Subsequent snapshots then come from that
 * same live pair, so level, XP, currencies, and equipped gear cannot silently
 * be replaced by unchanged synthetic defaults. */
static void vita_offline_profile_adopt_active(void *config, void *progress,
                                              const char *source) {
    void *old_active_config = g_vita_active_player_config;
    void *old_active_progress = g_vita_active_player_progress;
    int applied_config = 0;
    int applied_progress = 0;
    int promoted_outgoing = 0;
    int finish_config_post_load = 0;
    int finish_progress_post_load = 0;

    if (!g_vita_offline_profile_startup_owner_ready) {
        vita_default_profile_try_native_load(source ? source : "adopt-active");
    }

    /* A menu/gameplay transition can expose a different native copy of the
     * local profile before the one-second disk tick.  Promote the outgoing
     * live pair to the in-memory canonical payload first, so hydrating the
     * incoming pair cannot roll back currency or XP earned moments earlier. */
    if (trace_is_player_config_for_brother(g_vita_active_player_config) &&
        trace_is_player_progress_for_brother(g_vita_active_player_progress) &&
        g_vita_active_player_config != g_vita_default_player_config &&
        g_vita_active_player_progress != g_vita_default_player_progress &&
        (g_vita_active_player_config != config ||
         g_vita_active_player_progress != progress)) {
        vita_profile_capture_config_payload(g_vita_loaded_config_payload,
                                            g_vita_active_player_config);
        sceClibMemcpy(g_vita_loaded_progress_payload,
                      (const unsigned char *)g_vita_active_player_progress + 0x28u,
                      VITA_PROGRESS_PAYLOAD_SIZE);
        g_vita_loaded_profile_valid = 1;
        promoted_outgoing = 1;
    }

    if (trace_is_player_config_for_brother(config)) {
        g_vita_active_player_config = config;
        if (g_vita_loaded_profile_valid &&
            (g_vita_hydrated_player_config != config || promoted_outgoing)) {
            vita_profile_apply_config_payload(config,
                                              g_vita_loaded_config_payload);
            g_vita_hydrated_player_config = config;
            applied_config = 1;
        }
    }
    if (trace_is_player_progress_for_brother(progress)) {
        g_vita_active_player_progress = progress;
        if (g_vita_loaded_profile_valid &&
            (g_vita_hydrated_player_progress != progress || promoted_outgoing)) {
            sceClibMemcpy((unsigned char *)progress + 0x28u,
                          g_vita_loaded_progress_payload,
                          VITA_PROGRESS_PAYLOAD_SIZE);
            g_vita_hydrated_player_progress = progress;
            applied_progress = 1;
        }
    }

    /* A profile may first be hydrated while menus are coming up, before the
     * game-object registry is safe enough for CPlayerConfiguration::Init.
     * Finish that exact native LoadFromDisk step on the first later use of
     * the same live object; do not make the user wait until CPlayer::Bind. */
    finish_config_post_load =
        g_vita_loaded_profile_valid &&
        trace_is_player_config_for_brother(config) &&
        g_vita_hydrated_player_config == config &&
        g_vita_post_loaded_player_config != config &&
        gunbros_object_registry_is_valid(g_live_gunbros_self);
    finish_progress_post_load =
        g_vita_loaded_profile_valid &&
        trace_is_player_progress_for_brother(progress) &&
        g_vita_hydrated_player_progress == progress &&
        g_vita_post_loaded_player_progress != progress &&
        gunbros_object_registry_is_valid(g_live_gunbros_self);

    if (applied_config || applied_progress || finish_config_post_load ||
        finish_progress_post_load) {
        vita_offline_profile_post_load((applied_config || finish_config_post_load) ?
                                           config : NULL,
                                       (applied_progress || finish_progress_post_load) ?
                                           progress : NULL,
                                       source ? source : "adopt-active");
        sceClibPrintf("[SAVE-PROFILE] hydrated live objects source=%s config=%p applied=%d progress=%p applied=%d level=%u common=%u:%u rare=%u\n",
                      source ? source : "?", config, applied_config,
                      progress, applied_progress,
                      trace_is_player_progress_for_brother(progress) ?
                          (unsigned int)*(uint16_t *)(void *)
                              ((unsigned char *)progress + 0x50u) : 0u,
                      trace_is_player_progress_for_brother(progress) ?
                          (unsigned int)*(uint32_t *)(void *)
                              ((unsigned char *)progress + 0x3cu) : 0u,
                      trace_is_player_progress_for_brother(progress) ?
                          (unsigned int)*(uint32_t *)(void *)
                              ((unsigned char *)progress + 0x38u) : 0u,
                      trace_is_player_progress_for_brother(progress) ?
                          (unsigned int)*(uint32_t *)(void *)
                              ((unsigned char *)progress + 0x40u) : 0u);
        if (trace_is_player_config_for_brother(config)) {
            const unsigned char *cfg = (const unsigned char *)config;
            GUNBROS_PERF_LOG("[PROFILE-RESTORE] stage=hydrate source=%s config=%p applied=%d finished=%d guns=%u:%u,%u:%u active=%u\n",
                             source ? source : "?", config, applied_config,
                             finish_config_post_load,
                             (unsigned int)*(const uint16_t *)(const void *)
                                 (cfg + 0x10u),
                             (unsigned int)cfg[0x12u],
                             (unsigned int)*(const uint16_t *)(const void *)
                                 (cfg + 0x18u),
                             (unsigned int)cfg[0x1au],
                             (unsigned int)cfg[0x4cu]);
        }
    }

    if (old_active_config != g_vita_active_player_config ||
        old_active_progress != g_vita_active_player_progress ||
        applied_config || applied_progress || promoted_outgoing) {
        vita_offline_profile_bump_generation();
    }
}

static int vita_offline_profile_native_progress_is_newer(const void *progress) {
    const unsigned char *native = (const unsigned char *)progress;
    const unsigned char *saved = g_vita_loaded_progress_payload;
    uint16_t native_level;
    uint16_t saved_level;
    uint64_t native_xp;
    uint64_t saved_xp;

    if (!trace_is_player_progress_for_brother(progress)) {
        return 0;
    }
    if (!g_vita_loaded_profile_valid) {
        return 1;
    }

    native_level = *(const uint16_t *)(const void *)(native + 0x50u);
    saved_level = *(const uint16_t *)(const void *)(saved + 0x28u);
    native_xp = *(const uint64_t *)(const void *)(native + 0x48u);
    saved_xp = *(const uint64_t *)(const void *)(saved + 0x20u);

    if (native_level != saved_level) {
        return native_level > saved_level;
    }
    if (native_xp != saved_xp) {
        return native_xp > saved_xp;
    }

    /* Equal progression means this is a normal restart: the sidecar wins so
     * purchases and equipped refs made at that level are restored. */
    return 0;
}

/* The native profile owner has a layout proved by CGame::GetPlayerData:
 * config=ProfileData+0x150, progress=ProfileData+0x2e0 (delta 0x190).
 * Menus, store, and gameplay copies must all converge on this pair. */
static int vita_offline_profile_register_native_owner(void *config,
                                                      void *progress,
                                                      const char *source) {
    unsigned char merged_config[VITA_CONFIG_PAYLOAD_SIZE];
    int native_wins;
    int same_owner;
    int loadout_merged = 0;

    if (!trace_is_player_config_for_brother(config) ||
        !trace_is_player_progress_for_brother(progress) ||
        (uintptr_t)progress != (uintptr_t)config + 0x190u) {
        return 0;
    }

    if (!g_vita_offline_profile_startup_owner_ready) {
        vita_default_profile_try_native_load(source ? source : "native-owner");
    }
    same_owner = config == g_vita_native_player_config &&
                 progress == g_vita_native_player_progress;
    g_vita_native_player_config = config;
    g_vita_native_player_progress = progress;

    if (g_vita_native_profile_reconciled) {
        vita_offline_profile_adopt_active(config, progress,
                                          source ? source : "native-owner");
        if (!same_owner && trace_allow_ex("vita-profile/native-owner-change", 8, 180)) {
            sceClibPrintf("[SAVE-PROFILE] native owner changed source=%s config=%p progress=%p\n",
                          source ? source : "?", config, progress);
        }
        return 1;
    }

    native_wins = vita_offline_profile_native_progress_is_newer(progress);
    g_vita_native_profile_reconciled = 1;
    if (native_wins) {
        g_vita_active_player_config = config;
        g_vita_active_player_progress = progress;
        g_vita_hydrated_player_config = config;
        g_vita_hydrated_player_progress = progress;
        vita_offline_profile_post_load(config, progress,
                                       source ? source : "native-owner");
        vita_profile_capture_config_payload(g_vita_loaded_config_payload,
                                            config);
        sceClibMemcpy(g_vita_loaded_progress_payload,
                      (const unsigned char *)progress + 0x28u,
                      VITA_PROGRESS_PAYLOAD_SIZE);
        g_vita_loaded_profile_valid = 1;
        vita_offline_profile_bump_generation();
    } else {
        vita_offline_profile_adopt_active(config, progress,
                                          source ? source : "native-owner");
    }

    /* Level/XP decides which complete profile is newer, but the small
     * loadout journal is written synchronously by every Equip action. It is
     * therefore authoritative for equipped guns even when native progress
     * wins reconciliation. Without this final merge a newer XP snapshot can
     * silently put the starter pistol back into the live configuration. */
    vita_profile_capture_config_payload(merged_config, config);
    if (vita_offline_loadout_overlay(merged_config)) {
        vita_profile_apply_config_payload(config, merged_config);
        g_vita_active_player_config = config;
        g_vita_hydrated_player_config = config;
        g_vita_post_loaded_player_config = NULL;
        vita_offline_profile_post_load(config, NULL,
                                       "native-owner/loadout-journal");
        vita_profile_capture_config_payload(g_vita_loaded_config_payload,
                                            config);
        g_vita_loaded_profile_valid = 1;
        loadout_merged = 1;
        vita_offline_profile_bump_generation();
    }

    sceClibPrintf("[SAVE-PROFILE] reconciled native owner source=%s winner=%s loadout_journal=%d config=%p progress=%p level=%u xp=%u:%u common=%u:%u rare=%u\n",
                  source ? source : "?", native_wins ? "native" : "sidecar",
                  loadout_merged, config, progress,
                  (unsigned int)*(const uint16_t *)(const void *)
                      ((const unsigned char *)progress + 0x50u),
                  (unsigned int)*(const uint32_t *)(const void *)
                      ((const unsigned char *)progress + 0x4cu),
                  (unsigned int)*(const uint32_t *)(const void *)
                      ((const unsigned char *)progress + 0x48u),
                  (unsigned int)*(const uint32_t *)(const void *)
                      ((const unsigned char *)progress + 0x3cu),
                  (unsigned int)*(const uint32_t *)(const void *)
                      ((const unsigned char *)progress + 0x38u),
                  (unsigned int)*(const uint32_t *)(const void *)
                      ((const unsigned char *)progress + 0x40u));
    return 1;
}

static int vita_offline_profile_discover_native_owner(const char *source) {
    unsigned char *profile_base = (unsigned char *)g_live_gunbros_self;

    if (vita_offline_profile_get_native_pair(NULL, NULL)) {
        return 1;
    }
    if (!gunbros_object_registry_is_valid(profile_base) ||
        !trace_is_game_range(profile_base, 0x340u)) {
        return 0;
    }

    /* The live trace and both native accessors agree on this exact layout:
     * CGunBros/ProfileData base +0x150/+0x2e0.  This closes the small startup
     * window where StoreAggregator can configure before the HUD OnShow hook. */
    return vita_offline_profile_register_native_owner(
        profile_base + 0x150u, profile_base + 0x2e0u,
        source ? source : "discover-native-owner");
}

static uint32_t vita_offline_profile_build_snapshot(VitaOfflineProfileDisk *disk) {
    uint32_t count = g_vita_offline_owned_count;
    const unsigned char *config =
        (const unsigned char *)vita_offline_profile_config_source();
    const unsigned char *progress =
        (const unsigned char *)vita_offline_profile_progress_source();

    memset(disk, 0, sizeof(*disk));
    disk->magic = VITA_PROFILE_MAGIC;
    disk->version = VITA_PROFILE_VERSION;
    disk->config_size = VITA_CONFIG_PAYLOAD_SIZE;
    disk->progress_size = VITA_PROGRESS_PAYLOAD_SIZE;
    if (count > VITA_OFFLINE_OWNED_MAX) {
        count = VITA_OFFLINE_OWNED_MAX;
    }
    disk->owned_count = count;
    vita_profile_capture_config_payload(disk->config, config);
    sceClibMemcpy(disk->progress, progress + 0x28u,
                  VITA_PROGRESS_PAYLOAD_SIZE);
    if (count > 0u) {
        sceClibMemcpy(disk->owned, g_vita_offline_owned,
                      count * sizeof(VitaOwnedGameObjectRef));
    }
    disk->checksum = 0u;
    disk->checksum = vita_profile_checksum(disk, sizeof(*disk));
    return disk->checksum;
}

static int vita_offline_profile_sync_resident_from_live(void) {
    unsigned char config_payload[VITA_CONFIG_PAYLOAD_SIZE];
    unsigned char progress_payload[VITA_PROGRESS_PAYLOAD_SIZE];
    const unsigned char *config =
        (const unsigned char *)vita_offline_profile_config_source();
    const unsigned char *progress =
        (const unsigned char *)vita_offline_profile_progress_source();
    int changed;

    if (!trace_is_player_config_for_brother(config) ||
        !trace_is_player_progress_for_brother(progress)) {
        return 0;
    }

    vita_profile_capture_config_payload(config_payload, config);
    sceClibMemcpy(progress_payload, progress + 0x28u,
                  VITA_PROGRESS_PAYLOAD_SIZE);
    changed = !g_vita_loaded_profile_valid ||
              memcmp(config_payload, g_vita_loaded_config_payload,
                     VITA_CONFIG_PAYLOAD_SIZE) != 0 ||
              memcmp(progress_payload, g_vita_loaded_progress_payload,
                     VITA_PROGRESS_PAYLOAD_SIZE) != 0;
    if (changed) {
        sceClibMemcpy(g_vita_loaded_config_payload, config_payload,
                      VITA_CONFIG_PAYLOAD_SIZE);
        sceClibMemcpy(g_vita_loaded_progress_payload, progress_payload,
                      VITA_PROGRESS_PAYLOAD_SIZE);
        g_vita_loaded_profile_valid = 1;
    }
    vita_offline_owned_capture_equipped(config);
    return changed;
}

static int vita_offline_profile_save(int force) {
    static const char final_path[] = DATA_PATH "vita_profile_v1.dat";
    static const char temp_path[] = DATA_PATH "vita_profile_v1.tmp";
    VitaOfflineProfileDisk disk;
    uint32_t checksum;
    const unsigned char *config;
    const unsigned char *progress;
    FILE *fp;
    size_t written;

    if (!g_vita_default_profile_ready || !g_vita_default_config_native_ready) {
        return 0;
    }

    config = (const unsigned char *)vita_offline_profile_config_source();
    progress = (const unsigned char *)vita_offline_profile_progress_source();
    vita_offline_owned_capture_equipped(config);
    checksum = vita_offline_profile_build_snapshot(&disk);
    if (!force && checksum == g_vita_offline_profile_last_checksum) {
        return 1;
    }

    fp = fopen(temp_path, "wb");
    if (!fp) {
        sceClibPrintf("[SAVE-PROFILE] open failed path=%s\n", temp_path);
        return 0;
    }
    written = fwrite(&disk, 1, sizeof(disk), fp);
    (void)fflush(fp);
    if (fclose(fp) != 0 || written != sizeof(disk)) {
        (void)remove(temp_path);
        sceClibPrintf("[SAVE-PROFILE] write failed path=%s bytes=%u/%u\n",
                      temp_path, (unsigned int)written, (unsigned int)sizeof(disk));
        return 0;
    }

    if (rename(temp_path, final_path) != 0) {
        /* Some Vita libc/filesystem combinations do not replace an existing
         * destination. The completed temporary file remains the source of
         * truth while retrying with an explicit destination removal. */
        (void)remove(final_path);
        if (rename(temp_path, final_path) != 0) {
            (void)remove(temp_path);
            sceClibPrintf("[SAVE-PROFILE] rename failed temp=%s final=%s\n",
                          temp_path, final_path);
            return 0;
        }
    }

    g_vita_offline_profile_last_checksum = checksum;
    g_vita_offline_profile_staged = disk;
    g_vita_offline_profile_load_attempted = 1;
    g_vita_offline_profile_staged_valid = 1;
    sceClibMemcpy(g_vita_loaded_config_payload, disk.config,
                  VITA_CONFIG_PAYLOAD_SIZE);
    sceClibMemcpy(g_vita_loaded_progress_payload, disk.progress,
                  VITA_PROGRESS_PAYLOAD_SIZE);
    g_vita_loaded_profile_valid = 1;
    /* Do not rewrite the equipment journal from a periodic profile snapshot.
     * Level setup can temporarily place the starter gun in the live config;
     * only an explicit Equip action or gameplay gun swap is authoritative for
     * the journal. The journal is merged back into every full profile load. */
    sceClibPrintf("[SAVE-PROFILE] saved path=%s checksum=%08x owned=%u config=%p progress=%p level=%u common=%u:%u rare=%u\n",
                  final_path, checksum, g_vita_offline_owned_count,
                  config, progress,
                  (unsigned int)*(const uint16_t *)(const void *)(progress + 0x50u),
                  (unsigned int)*(const uint32_t *)(const void *)(progress + 0x3cu),
                  (unsigned int)*(const uint32_t *)(const void *)(progress + 0x38u),
                  (unsigned int)*(const uint32_t *)(const void *)(progress + 0x40u));
    return 1;
}

static void vita_offline_profile_request_save(void) {
    /* Coalesce the large profile/owned-ledger write shortly after the UI
     * transition. Gun identity uses the separate tiny immediate journal, so
     * restart safety does not require writing the full payload in this input
     * callback. */
    /* Update the process-resident canonical payload immediately. Disk I/O is
     * still debounced, but any native owner exposed before that write sees
     * the newest wallet/equipment/XP generation from memory. */
    (void)vita_offline_profile_sync_resident_from_live();
    g_vita_offline_profile_save_pending = 1;
    g_vita_offline_profile_deferred_elapsed_ms = 0u;
}

static void vita_offline_profile_begin_bulk_transition(const char *source) {
    if (g_vita_profile_bulk_transition_depth++ != 0u) {
        return;
    }
    g_vita_profile_bulk_save_calls = 0u;
    g_vita_profile_bulk_save_types = 0u;
    g_vita_profile_bulk_transition_source = source;
    (void)vita_offline_profile_sync_resident_from_live();
}

static void vita_offline_profile_end_bulk_transition(const char *source) {
    if (g_vita_profile_bulk_transition_depth == 0u) {
        return;
    }
    if (--g_vita_profile_bulk_transition_depth != 0u) {
        return;
    }

    /* LoadMenus/LoadMission save the same resident owners 11/9 times in one
     * blocking call. State-changing paths still use native Save normally,
     * and CGunBros::SaveAll remains untouched at shutdown. Coalesce only this
     * redundant transition burst into the checksum-protected Vita sidecars. */
    vita_offline_profile_request_save();
    GUNBROS_PERF_LOG("[PERF-PROFILE] event=bulk-save-coalesced source=%s calls=%u types=%08x resident_generation=%u\n",
                     source ? source :
                         (g_vita_profile_bulk_transition_source ?
                              g_vita_profile_bulk_transition_source : "?"),
                     g_vita_profile_bulk_save_calls,
                     g_vita_profile_bulk_save_types,
                     g_vita_offline_profile_generation);
    g_vita_profile_bulk_transition_source = NULL;
}

static int trace_profile_manager_save(void *self, unsigned int type,
                                      void *loader, unsigned char remote) {
    typedef int (*profile_manager_save_fn_t)(void *, unsigned int, void *,
                                              unsigned char);
    int result;
    void *owner = NULL;

    /* SaveAll enumerates the 18 native owners at +0x7c. The Vita profile
     * sidecar contains only config/progress and owned equipment; it cannot
     * replace tutorial, statistics, friend-power, or other manager saves. */
    if (type >= 1000u && type < 1018u &&
        trace_is_game_range(self, 0xc4u)) {
        owner = *(void **)(void *)((unsigned char *)self + 0x7cu +
                                   (type - 1000u) * 4u);
    }

    if (g_vita_profile_bulk_transition_depth != 0u &&
        owner &&
        (owner == vita_offline_profile_config_source() ||
         owner == vita_offline_profile_progress_source())) {
        g_vita_profile_bulk_save_calls++;
        g_vita_profile_bulk_save_types |= 1u << (type - 1000u);
        (void)self;
        (void)loader;
        (void)remote;
        /* The supplied ARM body returns zero on every completed/no-op path. */
        return 0;
    }

    if (g_original_profile_manager_save) {
        result = ((profile_manager_save_fn_t)g_original_profile_manager_save)(
            self, type, loader, remote);
    } else {
        result = SO_CONTINUE(int, h_profile_manager_save, self, type, loader,
                             remote);
    }
    /* A real state-changing native save remains authoritative. Mirror its
     * current live profile generation into the resident payload immediately
     * and debounce the compatibility sidecar write. */
    vita_offline_profile_request_save();
    return result;
}

/* Read and validate the process-independent sidecar without touching native
 * player objects.  This phase is safe immediately after so_initialize(); in
 * particular, it must not run CPlayerProgress's constructor because that
 * constructor asks the not-yet-created CGunBros singleton whether the profile
 * belongs to the local player. */
static void vita_offline_profile_stage(void) {
    static const char final_path[] = DATA_PATH "vita_profile_v1.dat";
    uint32_t expected;
    uint32_t stored;
    FILE *fp;
    size_t read_size;

    if (g_vita_offline_profile_load_attempted) {
        return;
    }
    g_vita_offline_profile_load_attempted = 1;
    (void)vita_offline_loadout_stage();
    sceClibMemset(&g_vita_offline_profile_staged, 0,
                  sizeof(g_vita_offline_profile_staged));

    fp = fopen(final_path, "rb");
    if (!fp) {
        sceClibPrintf("[SAVE-PROFILE] no prior offline profile at %s; using native defaults\n",
                      final_path);
        return;
    }
    read_size = fread(&g_vita_offline_profile_staged, 1,
                      sizeof(g_vita_offline_profile_staged), fp);
    (void)fclose(fp);
    if (read_size != sizeof(g_vita_offline_profile_staged) ||
        g_vita_offline_profile_staged.magic != VITA_PROFILE_MAGIC ||
        g_vita_offline_profile_staged.version != VITA_PROFILE_VERSION ||
        g_vita_offline_profile_staged.config_size != VITA_CONFIG_PAYLOAD_SIZE ||
        g_vita_offline_profile_staged.progress_size != VITA_PROGRESS_PAYLOAD_SIZE ||
        g_vita_offline_profile_staged.owned_count > VITA_OFFLINE_OWNED_MAX) {
        sceClibPrintf("[SAVE-PROFILE] rejected invalid profile path=%s bytes=%u magic=%08x version=%u owned=%u\n",
                      final_path, (unsigned int)read_size,
                      g_vita_offline_profile_staged.magic,
                      g_vita_offline_profile_staged.version,
                      g_vita_offline_profile_staged.owned_count);
        return;
    }

    stored = g_vita_offline_profile_staged.checksum;
    g_vita_offline_profile_staged.checksum = 0u;
    expected = vita_profile_checksum(&g_vita_offline_profile_staged,
                                     sizeof(g_vita_offline_profile_staged));
    if (stored != expected) {
        sceClibPrintf("[SAVE-PROFILE] rejected checksum path=%s stored=%08x expected=%08x\n",
                      final_path, stored, expected);
        return;
    }

    /* The tiny equipment journal is newer when the app was closed before the
     * coalesced full-profile write. Overlay only its stable gun identities;
     * level, currency, armor, and ownership still come from the validated
     * complete profile above. */
    (void)vita_offline_loadout_overlay(g_vita_offline_profile_staged.config);
    g_vita_offline_profile_staged_valid = 1;
    GUNBROS_PERF_LOG("[PERF-PROFILE] event=sidecar-staged bytes=%u owned=%u\n",
                     (unsigned int)read_size,
                     g_vita_offline_profile_staged.owned_count);
}

/* Apply the already staged bytes only after the native profile constructor is
 * known to be safe.  No save file is opened on this path, so a first shop or
 * mission bind only performs bounded memory copies. */
static void vita_offline_profile_load(void) {
    static const char final_path[] = DATA_PATH "vita_profile_v1.dat";
    VitaOfflineProfileDisk *disk = &g_vita_offline_profile_staged;

    vita_offline_profile_stage();
    if (g_vita_offline_profile_load_applied ||
        !g_vita_default_profile_ready) {
        return;
    }
    g_vita_offline_profile_load_applied = 1;
    if (!g_vita_offline_profile_staged_valid) {
        vita_offline_owned_capture_equipped(g_vita_default_player_config);
        return;
    }

    vita_profile_apply_config_payload(g_vita_default_player_config,
                                      disk->config);
    sceClibMemcpy(g_vita_default_player_progress + 0x28u, disk->progress,
                  VITA_PROGRESS_PAYLOAD_SIZE);
    sceClibMemcpy(g_vita_loaded_config_payload, disk->config,
                  VITA_CONFIG_PAYLOAD_SIZE);
    vita_profile_sanitize_config_payload(g_vita_loaded_config_payload);
    sceClibMemcpy(g_vita_loaded_progress_payload, disk->progress,
                  VITA_PROGRESS_PAYLOAD_SIZE);
    g_vita_loaded_profile_valid = 1;
    g_vita_hydrated_player_config = g_vita_default_player_config;
    g_vita_hydrated_player_progress = g_vita_default_player_progress;
    g_vita_offline_owned_count = disk->owned_count;
    if (disk->owned_count > 0u) {
        sceClibMemcpy(g_vita_offline_owned, disk->owned,
                      disk->owned_count * sizeof(VitaOwnedGameObjectRef));
    }

    /* Match the exact post-read steps in the native SaveToDisk/LoadFromDisk
     * implementations: config Init plus progress content-tracker refresh. */
    vita_offline_profile_post_load(g_vita_default_player_config,
                                   g_vita_default_player_progress,
                                   "offline-profile-load");
    /* Store/gameplay may have exposed the live profile pair before the object
     * registry became ready enough to read the sidecar.  Hydrate that already
     * adopted pair now, before the first periodic snapshot can replace the
     * loaded file with its pre-load contents. */
    vita_offline_profile_adopt_active(g_vita_active_player_config,
                                      g_vita_active_player_progress,
                                      "offline-profile-load/live");
    g_vita_offline_profile_last_checksum =
        vita_offline_profile_build_snapshot(disk);
    sceClibPrintf("[SAVE-PROFILE] loaded path=%s checksum=%08x owned=%u level=%u\n",
                  final_path, g_vita_offline_profile_last_checksum,
                  g_vita_offline_owned_count,
                  (unsigned int)*(uint16_t *)(void *)(g_vita_default_player_progress + 0x50u));
    GUNBROS_PERF_LOG("[PROFILE-RESTORE] stage=profile-loaded guns=%u:%u,%u:%u active=%u owned=%u level=%u\n",
                     (unsigned int)*(uint16_t *)(const void *)
                         (g_vita_default_player_config + 0x10u),
                     (unsigned int)g_vita_default_player_config[0x12u],
                     (unsigned int)*(uint16_t *)(const void *)
                         (g_vita_default_player_config + 0x18u),
                     (unsigned int)g_vita_default_player_config[0x1au],
                     (unsigned int)g_vita_default_player_config[0x4cu],
                     g_vita_offline_owned_count,
                     (unsigned int)*(uint16_t *)(void *)
                         (g_vita_default_player_progress + 0x50u));
}

static int vita_offline_mission_object_valid(const void *object,
                                             const void *expected_vptr,
                                             unsigned int count_offset) {
    return expected_vptr &&
           trace_is_game_range(object, (size_t)count_offset + sizeof(uint32_t)) &&
           *(const void * const *)object == expected_vptr &&
           *(const uint32_t *)(const void *)
               ((const unsigned char *)object + count_offset) <= 64u;
}

static int vita_offline_missions_refresh_objects(const char *source) {
    static const void *score_vptr;
    static const void *objective_vptr;
    static const void *wave_vptr;
    const unsigned char *gunbros = (const unsigned char *)g_live_gunbros_self;
    void *score = NULL;
    void *objectives = NULL;
    void *waves = NULL;
    int changed;

    if (!score_vptr) {
        uintptr_t vtable = so_symbol(&so_mod, "_ZTV17CMissionHighScore");
        score_vptr = vtable ? (const void *)(vtable + 8u) : NULL;
    }
    if (!objective_vptr) {
        uintptr_t vtable = so_symbol(&so_mod, "_ZTV23CMissionObjectiveStatus");
        objective_vptr = vtable ? (const void *)(vtable + 8u) : NULL;
    }
    if (!wave_vptr) {
        uintptr_t vtable = so_symbol(&so_mod, "_ZTV18CMissionWaveStatus");
        wave_vptr = vtable ? (const void *)(vtable + 8u) : NULL;
    }

    if (gunbros_object_registry_is_valid(g_live_gunbros_self)) {
        /* CGunBros::Init allocates 0x8290/0x190/0x310-byte objects at these
         * exact fields, assigns the three native vtables, then registers them
         * as profile save owners. */
        waves = *(void * const *)(const void *)(gunbros + 0x24u);
        objectives = *(void * const *)(const void *)(gunbros + 0x28u);
        score = *(void * const *)(const void *)(gunbros + 0x2cu);
    }
    if (!vita_offline_mission_object_valid(
            score, score_vptr, VITA_MISSION_SCORE_COUNT_OFFSET)) {
        score = NULL;
    }
    if (!vita_offline_mission_object_valid(
            objectives, objective_vptr, VITA_MISSION_OBJECTIVE_COUNT_OFFSET)) {
        objectives = NULL;
    }
    if (!vita_offline_mission_object_valid(
            waves, wave_vptr, VITA_MISSION_WAVE_COUNT_OFFSET)) {
        waves = NULL;
    }

    changed = score != g_vita_mission_high_score ||
              objectives != g_vita_mission_objectives ||
              waves != g_vita_mission_waves;

    if (changed) {
        g_vita_mission_high_score = score;
        g_vita_mission_objectives = objectives;
        g_vita_mission_waves = waves;
        g_vita_loaded_missions_hydrated = 0;
        if (score || objectives || waves) {
            sceClibPrintf("[SAVE-MISSIONS] owners source=%s score=%p objectives=%p waves=%p\n",
                          source ? source : "?", score, objectives, waves);
        }
    }

    return score && objectives && waves;
}

static int vita_offline_missions_disk_counts_valid(
    const VitaOfflineMissionsDisk *disk) {
    return *(const uint32_t *)(const void *)(disk->score + 0x300u) <= 64u &&
           *(const uint32_t *)(const void *)(disk->objective + 0x180u) <= 64u &&
           *(const uint32_t *)(const void *)(disk->wave + 0x8280u) <= 64u;
}

static void vita_offline_missions_load(void) {
    static const char final_path[] = DATA_PATH "vita_missions_v1.dat";
    uint32_t stored;
    uint32_t expected;
    FILE *fp;
    size_t read_size;

    if (g_vita_offline_missions_load_attempted) {
        return;
    }
    g_vita_offline_missions_load_attempted = 1;

    fp = fopen(final_path, "rb");
    if (!fp) {
        sceClibPrintf("[SAVE-MISSIONS] no prior mission sidecar at %s; using native defaults\n",
                      final_path);
        return;
    }
    read_size = fread(&g_vita_loaded_missions, 1,
                      sizeof(g_vita_loaded_missions), fp);
    (void)fclose(fp);
    if (read_size != sizeof(g_vita_loaded_missions) ||
        g_vita_loaded_missions.magic != VITA_MISSIONS_MAGIC ||
        g_vita_loaded_missions.version != VITA_MISSIONS_VERSION ||
        g_vita_loaded_missions.score_size != VITA_MISSION_SCORE_STATE_SIZE ||
        g_vita_loaded_missions.objective_size != VITA_MISSION_OBJECTIVE_STATE_SIZE ||
        g_vita_loaded_missions.wave_size != VITA_MISSION_WAVE_STATE_SIZE ||
        !vita_offline_missions_disk_counts_valid(&g_vita_loaded_missions)) {
        sceClibPrintf("[SAVE-MISSIONS] rejected invalid sidecar path=%s bytes=%u magic=%08x version=%u sizes=%u/%u/%u\n",
                      final_path, (unsigned int)read_size,
                      g_vita_loaded_missions.magic,
                      g_vita_loaded_missions.version,
                      g_vita_loaded_missions.score_size,
                      g_vita_loaded_missions.objective_size,
                      g_vita_loaded_missions.wave_size);
        return;
    }

    stored = g_vita_loaded_missions.checksum;
    g_vita_loaded_missions.checksum = 0u;
    expected = vita_profile_checksum(&g_vita_loaded_missions,
                                     sizeof(g_vita_loaded_missions));
    g_vita_loaded_missions.checksum = stored;
    if (stored != expected) {
        sceClibPrintf("[SAVE-MISSIONS] rejected checksum path=%s stored=%08x expected=%08x\n",
                      final_path, stored, expected);
        return;
    }

    g_vita_loaded_missions_valid = 1;
    g_vita_offline_missions_last_checksum = stored;
    sceClibPrintf("[SAVE-MISSIONS] loaded path=%s checksum=%08x counts(score=%u objectives=%u waves=%u)\n",
                  final_path, stored,
                  *(const uint32_t *)(const void *)(g_vita_loaded_missions.score + 0x300u),
                  *(const uint32_t *)(const void *)(g_vita_loaded_missions.objective + 0x180u),
                  *(const uint32_t *)(const void *)(g_vita_loaded_missions.wave + 0x8280u));
}

static int vita_offline_missions_try_hydrate(const char *source) {
    if (!vita_offline_missions_refresh_objects(source)) {
        return 0;
    }
    if (!g_vita_loaded_missions_valid || g_vita_loaded_missions_hydrated) {
        return 1;
    }

    sceClibMemcpy((unsigned char *)g_vita_mission_high_score + 8u,
                  g_vita_loaded_missions.score,
                  VITA_MISSION_SCORE_STATE_SIZE);
    sceClibMemcpy((unsigned char *)g_vita_mission_objectives + 8u,
                  g_vita_loaded_missions.objective,
                  VITA_MISSION_OBJECTIVE_STATE_SIZE);
    sceClibMemcpy((unsigned char *)g_vita_mission_waves + 8u,
                  g_vita_loaded_missions.wave,
                  VITA_MISSION_WAVE_STATE_SIZE);
    g_vita_loaded_missions_hydrated = 1;
    sceClibPrintf("[SAVE-MISSIONS] hydrated native owners source=%s counts(score=%u objectives=%u waves=%u)\n",
                  source ? source : "?",
                  *(uint32_t *)(void *)((unsigned char *)g_vita_mission_high_score +
                                       VITA_MISSION_SCORE_COUNT_OFFSET),
                  *(uint32_t *)(void *)((unsigned char *)g_vita_mission_objectives +
                                       VITA_MISSION_OBJECTIVE_COUNT_OFFSET),
                  *(uint32_t *)(void *)((unsigned char *)g_vita_mission_waves +
                                       VITA_MISSION_WAVE_COUNT_OFFSET));
    return 1;
}

static uint32_t vita_offline_missions_build_snapshot(
    VitaOfflineMissionsDisk *disk) {
    memset(disk, 0, sizeof(*disk));
    disk->magic = VITA_MISSIONS_MAGIC;
    disk->version = VITA_MISSIONS_VERSION;
    disk->score_size = VITA_MISSION_SCORE_STATE_SIZE;
    disk->objective_size = VITA_MISSION_OBJECTIVE_STATE_SIZE;
    disk->wave_size = VITA_MISSION_WAVE_STATE_SIZE;
    sceClibMemcpy(disk->score,
                  (const unsigned char *)g_vita_mission_high_score + 8u,
                  VITA_MISSION_SCORE_STATE_SIZE);
    sceClibMemcpy(disk->objective,
                  (const unsigned char *)g_vita_mission_objectives + 8u,
                  VITA_MISSION_OBJECTIVE_STATE_SIZE);
    sceClibMemcpy(disk->wave,
                  (const unsigned char *)g_vita_mission_waves + 8u,
                  VITA_MISSION_WAVE_STATE_SIZE);
    disk->checksum = 0u;
    disk->checksum = vita_profile_checksum(disk, sizeof(*disk));
    return disk->checksum;
}

static int vita_offline_missions_save(int force) {
    static const char final_path[] = DATA_PATH "vita_missions_v1.dat";
    static const char temp_path[] = DATA_PATH "vita_missions_v1.tmp";
    static VitaOfflineMissionsDisk disk;
    uint32_t checksum;
    FILE *fp;
    size_t written;

    vita_offline_missions_load();
    if (!vita_offline_missions_try_hydrate("mission-save")) {
        return 0;
    }

    checksum = vita_offline_missions_build_snapshot(&disk);
    if (!force && checksum == g_vita_offline_missions_last_checksum) {
        return 1;
    }

    fp = fopen(temp_path, "wb");
    if (!fp) {
        sceClibPrintf("[SAVE-MISSIONS] open failed path=%s\n", temp_path);
        return 0;
    }
    written = fwrite(&disk, 1, sizeof(disk), fp);
    (void)fflush(fp);
    if (fclose(fp) != 0 || written != sizeof(disk)) {
        (void)remove(temp_path);
        sceClibPrintf("[SAVE-MISSIONS] write failed path=%s bytes=%u/%u\n",
                      temp_path, (unsigned int)written,
                      (unsigned int)sizeof(disk));
        return 0;
    }
    if (rename(temp_path, final_path) != 0) {
        (void)remove(final_path);
        if (rename(temp_path, final_path) != 0) {
            (void)remove(temp_path);
            sceClibPrintf("[SAVE-MISSIONS] rename failed temp=%s final=%s\n",
                          temp_path, final_path);
            return 0;
        }
    }

    g_vita_offline_missions_last_checksum = checksum;
    sceClibMemcpy(&g_vita_loaded_missions, &disk, sizeof(disk));
    g_vita_loaded_missions_valid = 1;
    g_vita_loaded_missions_hydrated = 1;
    sceClibPrintf("[SAVE-MISSIONS] saved path=%s checksum=%08x counts(score=%u objectives=%u waves=%u)\n",
                  final_path, checksum,
                  *(const uint32_t *)(const void *)(disk.score + 0x300u),
                  *(const uint32_t *)(const void *)(disk.objective + 0x180u),
                  *(const uint32_t *)(const void *)(disk.wave + 0x8280u));
    return 1;
}

static void vita_offline_profile_tick(int dt) {
    unsigned int elapsed = (dt > 0 && dt < 1000) ? (unsigned int)dt : 16u;
    int saved = 0;
    int purchases_saved = 1;

    if (g_vita_offline_profile_save_pending &&
        g_vita_offline_profile_deferred_elapsed_ms < 350u) {
        g_vita_offline_profile_deferred_elapsed_ms += elapsed;
    }

    if (g_vita_offline_profile_save_pending &&
        g_vita_offline_profile_deferred_elapsed_ms >= 350u) {
        if (!g_vita_offline_profile_startup_owner_ready) {
            vita_default_profile_try_native_load("offline-profile-deferred");
        }
        saved = vita_offline_profile_save(0);
        purchases_saved = store_offline_purchases_save(0);
        saved = saved && purchases_saved;
        if (saved) {
            g_vita_offline_profile_save_pending = 0;
            g_vita_offline_profile_deferred_elapsed_ms = 0u;
        }
    }

    if (g_vita_offline_profile_save_elapsed_ms < 1000u) {
        g_vita_offline_profile_save_elapsed_ms += elapsed;
    }
    if (g_vita_offline_profile_save_elapsed_ms >= 1000u) {
        g_vita_offline_profile_save_elapsed_ms = 0u;
        if (!g_vita_offline_profile_startup_owner_ready) {
            vita_default_profile_try_native_load("offline-profile-tick");
        }
        saved = vita_offline_profile_save(0);
        purchases_saved = store_offline_purchases_save(0);
        saved = saved && purchases_saved;
        if (saved) {
            g_vita_offline_profile_save_pending = 0;
            g_vita_offline_profile_deferred_elapsed_ms = 0u;
        }
        (void)vita_offline_missions_save(0);
        (void)vita_weapon_mastery_save(vita_mastery_local_owner());
    }
}

void gunbros_flush_offline_profile(void) {
    int profile_saved;
    int purchases_saved;

    vita_default_profile_try_native_load("offline-profile-flush");
    profile_saved = vita_offline_profile_save(1);
    purchases_saved = store_offline_purchases_save(1);
    if (profile_saved && purchases_saved) {
        g_vita_offline_profile_save_pending = 0;
        g_vita_offline_profile_deferred_elapsed_ms = 0u;
    }
    (void)vita_offline_missions_save(1);
    (void)vita_weapon_mastery_save(vita_mastery_local_owner());
}

static void vita_default_profile_init(void) {
    typedef void (*profile_ctor_fn_t)(void *self);
    profile_ctor_fn_t config_ctor;
    profile_ctor_fn_t progress_ctor;

    if (g_vita_default_profile_ready) {
        return;
    }
    /* CPlayerProgress::CPlayerProgress -> ResetData -> SetCommonCurrency ->
     * ProgressData::IsLocalPlayer dereferences the native CGunBros singleton.
     * Refuse construction until one of the hooked CGunBros lifecycle methods
     * has published a real owner, even if a future caller regresses startup
     * ordering and reaches this helper before bootstrap. */
    if (!g_live_gunbros_self) {
        return;
    }

    memset(g_vita_default_player_config, 0, sizeof(g_vita_default_player_config));
    memset(g_vita_default_player_progress, 0, sizeof(g_vita_default_player_progress));

    config_ctor = (profile_ctor_fn_t)(uintptr_t)
        so_symbol(&so_mod, "_ZN20CPlayerConfigurationC1Ev");
    progress_ctor = (profile_ctor_fn_t)(uintptr_t)
        so_symbol(&so_mod, "_ZN15CPlayerProgressC1Ev");
    if (config_ctor) {
        config_ctor(g_vita_default_player_config);
    }
    if (progress_ctor) {
        progress_ctor(g_vita_default_player_progress);
    }

    /* Match the original starter loadout even before the live registry can
     * run Reset: two free guns, combat pants, combat vest, and no helmet.
     * Reset later replaces these identities with complete native refs. */
    player_config_set_ref(g_vita_default_player_config, 0x10,
                           GUNBROS_DEFAULT_WHIPPERSNAPPERS_ID,
                           GUNBROS_DEFAULT_WHIPPERSNAPPERS_SUBTYPE);
    player_config_set_ref(g_vita_default_player_config, 0x18,
                           GUNBROS_DEFAULT_SECOND_GUN_ID,
                           GUNBROS_DEFAULT_SECOND_GUN_SUBTYPE);
    player_config_set_ref(g_vita_default_player_config, 0x20, 0, 0);
    player_config_set_ref(g_vita_default_player_config, 0x28, 0, 0x2d);
    player_config_set_ref(g_vita_default_player_config, 0x30,
                           GUNBROS_DEFAULT_PANTS_ID,
                           GUNBROS_DEFAULT_PANTS_SUBTYPE);
    player_config_set_ref(g_vita_default_player_config, 0x38,
                           GUNBROS_DEFAULT_VEST_ID,
                           GUNBROS_DEFAULT_VEST_SUBTYPE);
    player_config_set_ref(g_vita_default_player_config, 0x40,
                           GUNBROS_DEFAULT_NO_HELMET_ID,
                           GUNBROS_DEFAULT_NO_HELMET_SUBTYPE);
    player_config_set_ref(g_vita_default_player_config, 0x48, 0, 0xff);
    g_vita_default_player_config[0x4c] = 0;      /* active gun: primary */
    g_vita_default_player_config[0x4d] = 0;

    if (!progress_ctor) {
        *(uint16_t *)(void *)(g_vita_default_player_progress + 0x50) = 1;
    }

    g_vita_active_player_config = g_vita_default_player_config;
    g_vita_active_player_progress = g_vita_default_player_progress;
    g_vita_default_profile_ready = 1;
    sceClibPrintf("[FIX-PROFILE] constructed offline config=%p progress=%p native_ctors=(%d,%d)\n",
                  g_vita_default_player_config, g_vita_default_player_progress,
                  config_ctor != NULL, progress_ctor != NULL);
    trace_dump_player_config_summary("synthetic-default", g_vita_default_player_config);

    /* Apply the payload staged before bootstrap as soon as a local profile
     * can be constructed safely. The old ordering waited for the game-object
     * registry, which let the first menu bind to constructor defaults (level 1
     * and zero currency). Native GameObjectRef initialization remains deferred
     * by vita_offline_profile_post_load until the registry is ready. */
    vita_offline_profile_load();
}

void gunbros_preload_offline_profile(void) {
    uint64_t begin_us = sceKernelGetProcessTimeWide();
    uint64_t end_us;

    /* Called from main after so_initialize(), before the Java/native game
     * bootstrap. Only stage plain disk bytes here: CPlayerProgress's native
     * constructor dereferences the CGunBros singleton, which does not exist at
     * this point. The normal first live-owner path constructs and hydrates the
     * native objects later without reopening any sidecar. */
    vita_offline_profile_stage();
    vita_offline_missions_load();
    end_us = sceKernelGetProcessTimeWide();
    GUNBROS_PERF_LOG("[PERF-PROFILE] event=startup-staged profile=%s loadout=%s missions=%s elapsed_us=%llu\n",
                     g_vita_offline_profile_staged_valid ? "loaded" : "defaults",
                     g_vita_offline_loadout_staged_valid ? "loaded" : "defaults",
                     g_vita_loaded_missions_valid ? "loaded" : "defaults",
                     (unsigned long long)(end_us >= begin_us ?
                         end_us - begin_us : 0u));
}

static void vita_default_profile_try_native_load(const char *source) {
    typedef void (*player_config_reset_fn_t)(void *self);

    vita_default_profile_init();
    if (!gunbros_object_registry_is_valid(g_live_gunbros_self)) {
        return;
    }

    if (!g_vita_default_config_native_ready) {
        player_config_reset_fn_t reset_fn = (player_config_reset_fn_t)(uintptr_t)
            so_symbol(&so_mod, "_ZN20CPlayerConfiguration5ResetEv");
        if (reset_fn) {
            /* Capture the original starter refs before restoring a save. This
             * is the authoritative fallback for legacy sidecars that lost a
             * gun or one of the three required body slots. */
            reset_fn(g_vita_default_player_config);
            sceClibMemcpy(g_vita_native_default_player_config,
                          g_vita_default_player_config,
                          sizeof(g_vita_native_default_player_config));
            g_vita_native_default_loadout_ready = 1;
            g_vita_default_config_native_ready = 1;
            vita_offline_profile_load();
            if (!g_vita_loaded_profile_valid) {
                /* A tiny loadout journal can exist even if the first full
                 * profile commit was interrupted. Seed the rest of the
                 * payload from native defaults and recover just the two guns
                 * and selected slot instead of discarding the journal. */
                vita_profile_capture_config_payload(
                    g_vita_loaded_config_payload,
                    g_vita_default_player_config);
                if (vita_offline_loadout_overlay(
                        g_vita_loaded_config_payload)) {
                    sceClibMemcpy(g_vita_loaded_progress_payload,
                                  g_vita_default_player_progress + 0x28u,
                                  VITA_PROGRESS_PAYLOAD_SIZE);
                    g_vita_loaded_profile_valid = 1;
                    GUNBROS_PERF_LOG("[PROFILE-RESTORE] stage=journal-only native-profile-missing\n");
                }
            }
            if (g_vita_loaded_profile_valid) {
                /* Reset constructs valid native starter GameObjectRefs, but it
                 * must not replace the profile payload already read for the
                 * menus. Restore both saved payloads, then run the original
                 * config Init/content-tracker post-read operations. */
                vita_profile_apply_config_payload(
                    g_vita_default_player_config,
                    g_vita_loaded_config_payload);
                sceClibMemcpy(g_vita_default_player_progress + 0x28u,
                              g_vita_loaded_progress_payload,
                              VITA_PROGRESS_PAYLOAD_SIZE);
                g_vita_hydrated_player_config = g_vita_default_player_config;
                g_vita_hydrated_player_progress = g_vita_default_player_progress;
                g_vita_post_loaded_player_config = NULL;
                g_vita_post_loaded_player_progress = NULL;
                vita_offline_profile_post_load(g_vita_default_player_config,
                                               g_vita_default_player_progress,
                                               "offline-native-reset/restore");
            }
            vita_offline_missions_load();
            player_config_repair_loadout_defaults(g_vita_default_player_config,
                                                   "offline-native-reset");
            vita_offline_owned_capture_equipped(g_vita_default_player_config);
            sceClibPrintf("[FIX-PROFILE] loaded native default configuration source=%s config=%p\n",
                          source ? source : "?", g_vita_default_player_config);
        }
    }

    (void)player_progress_ensure_native_tables(g_vita_default_player_progress,
                                                source);
}

static void vita_offline_profile_prepare_live_owner(const char *source) {
    void *config = NULL;
    void *progress = NULL;

    if (g_vita_offline_profile_startup_owner_ready) {
        return;
    }

    vita_default_profile_try_native_load(
        source ? source : "prepare-live-owner");
    if (!vita_offline_profile_discover_native_owner(
            source ? source : "prepare-live-owner") ||
        !vita_offline_profile_get_native_pair(&config, &progress)) {
        return;
    }

    vita_offline_profile_adopt_active(config, progress,
                                      source ? source : "prepare-live-owner");
    vita_offline_missions_load();
    if (!vita_offline_missions_try_hydrate(
            source ? source : "prepare-live-owner")) {
        /* ProfileData can become valid a few updates before CGunBros has
         * installed all three mission-owner pointers. Keep retrying this
         * cheap in-memory handoff until both profile and level state are
         * resident; no file is reopened because both load guards are set. */
        return;
    }
    g_vita_offline_profile_startup_owner_ready = 1;
    GUNBROS_PERF_LOG("[PERF-PROFILE] event=live-owner-ready source=%s config=%p progress=%p generation=%u\n",
                     source ? source : "?", config, progress,
                     g_vita_offline_profile_generation);
}

static const void *vita_default_config(void) {
    if (!g_vita_offline_profile_startup_owner_ready) {
        vita_default_profile_try_native_load("vita_default_config");
    }
    return g_vita_default_player_config;
}

static const void *vita_default_progress(void) {
    if (!g_vita_offline_profile_startup_owner_ready) {
        vita_default_profile_try_native_load("vita_default_progress");
    }
    return g_vita_default_player_progress;
}

static void continue_game_get_player_data(const void *self,
                                          void **out_config,
                                          void **out_progress) {
    typedef void (*game_get_player_data_fn_t)(const void *, void **, void **);
    game_get_player_data_fn_t original;

    kuKernelCpuUnrestrictedMemcpy((void *)h_game_get_player_data.addr,
                                  h_game_get_player_data.orig_instr,
                                  sizeof(h_game_get_player_data.orig_instr));
    kuKernelFlushCaches((void *)h_game_get_player_data.addr,
                        sizeof(h_game_get_player_data.orig_instr));

    original = (game_get_player_data_fn_t)(uintptr_t)
        (h_game_get_player_data.thumb_addr ?
             h_game_get_player_data.thumb_addr :
             h_game_get_player_data.addr);
    original(self, out_config, out_progress);

    kuKernelCpuUnrestrictedMemcpy((void *)h_game_get_player_data.addr,
                                  h_game_get_player_data.patch_instr,
                                  sizeof(h_game_get_player_data.patch_instr));
    kuKernelFlushCaches((void *)h_game_get_player_data.addr,
                        sizeof(h_game_get_player_data.patch_instr));
}

static void trace_game_get_player_data(const void *self,
                                       void **out_config,
                                       void **out_progress) {
    void *config = NULL;
    void *progress = NULL;

    /* SO_CONTINUE always declares a result temporary and therefore cannot
     * represent this native void ABI. Use the same temporary-unhook pattern
     * as the other verified void methods in this patch set. */
    continue_game_get_player_data(self, out_config, out_progress);
    if (trace_is_game_range(out_config, sizeof(void *))) {
        config = *out_config;
    }
    if (trace_is_game_range(out_progress, sizeof(void *))) {
        progress = *out_progress;
    }

    /* This leaf accessor is the first authoritative source used by the main
     * menu/HUD as well as gameplay. Hydrating its returned pair makes saved
     * wallet and level visible on the first screen, without replacing the
     * native CGame-owned objects with synthetic storage. */
    if (trace_is_player_config_for_brother(config) &&
        trace_is_player_progress_for_brother(progress)) {
        if (!vita_offline_profile_register_native_owner(
                config, progress, "CGame::GetPlayerData")) {
            vita_offline_profile_adopt_active(config, progress,
                                              "CGame::GetPlayerData");
        }
    }
}

static void *trace_friend_data_get_avatar_progress(void *self, int index) {
    void *progress = SO_CONTINUE(void *, h_friend_data_get_avatar_progress, self, index);

    if (!trace_is_player_progress_for_brother(progress)) {
        const void *fallback = vita_default_progress();
        if (trace_allow("CFriendDataManager::GetFriendAvatarProgress/bad_ptr")) {
            sceClibPrintf("[FIX-PROFILE] GetFriendAvatarProgress(this=%p, index=%d) returned bad progress=%p; using synthetic=%p\n",
                          self, index, progress, fallback);
        }
        return (void *)fallback;
    }

    (void)player_progress_ensure_native_tables(progress,
                                                "GetFriendAvatarProgress");
    return progress;
}

static void *trace_friend_data_get_avatar_config(void *self, int index) {
    void *config = SO_CONTINUE(void *, h_friend_data_get_avatar_config, self, index);

    if (!trace_is_player_config_for_brother(config)) {
        const void *fallback = vita_default_config();
        if (trace_allow("CFriendDataManager::GetFriendAvatarConfig/bad_ptr")) {
            sceClibPrintf("[FIX-PROFILE] GetFriendAvatarConfig(this=%p, index=%d) returned bad config=%p; using synthetic=%p\n",
                          self, index, config, fallback);
        }
        return (void *)fallback;
    }

    return config;
}

static void trace_game_flow_configure_brother(void *self, const void *config, const void *progress) {
    const void *safe_config = config;
    const void *safe_progress = progress;
    void *local_config = NULL;
    void *local_progress = NULL;

    if (!trace_is_game_range(self, 0x260)) {
        if (trace_allow("CGameFlow::ConfigureBrother/bad_self")) {
            sceClibPrintf("[PATCH-FLOW] ConfigureBrother(this=%p, config=%p, progress=%p) skipped bad gameflow\n",
                          self, config, progress);
        }
        return;
    }

    vita_offline_profile_get_best_local_pair(&local_config, &local_progress);
    if (!trace_is_player_config_for_brother(config)) {
        safe_config = local_config;
    }
    (void)player_config_repair_armor_slots((void *)safe_config,
                                           "CGameFlow::ConfigureBrother");
    (void)player_config_repair_loadout_defaults((void *)safe_config,
                                                "CGameFlow::ConfigureBrother");

    if (!trace_is_player_progress_for_brother(progress)) {
        safe_progress = local_progress;
    }

    /* ConfigureBrother copies the native payload into CGameFlow before its
     * RequirementList is built. Hydrate here so saved armor/gun refs enter
     * GetRequirements and are loaded before CBrother::Bind consumes them. */
    vita_offline_profile_adopt_active((void *)safe_config,
                                      (void *)safe_progress,
                                      "CGameFlow::ConfigureBrother");
    (void)vita_offline_loadout_restore_config(
        (void *)safe_config, "CGameFlow::ConfigureBrother/journal");
    (void)player_progress_ensure_native_tables((void *)safe_progress,
                                                "CGameFlow::ConfigureBrother");
    /* Queue any restored armor whose template exists but whose mesh was
     * released by the store preview.  This runs before native ConfigureBrother
     * copies the config and builds its RequirementList, not at the final
     * CPlayer::Bind where an asynchronous request would be too late. */
    gameplay_request_configured_content(safe_config,
                                        "CGameFlow::ConfigureBrother/pre");

    if (safe_config != config || safe_progress != progress) {
        if (trace_allow("CGameFlow::ConfigureBrother/bad_brother_data")) {
            sceClibPrintf("[FIX-PROFILE] ConfigureBrother(this=%p) repaired config=%p->%p progress=%p->%p\n",
                          self, config, safe_config, progress, safe_progress);
        }
    }

    (void)SO_CONTINUE(int, h_game_flow_configure_brother, self, safe_config, safe_progress);
}


static unsigned char g_vita_dummy_mission[0x80] __attribute__((aligned(4)));
static int g_vita_dummy_mission_ready;

static void *vita_dummy_mission(void) {
    if (!g_vita_dummy_mission_ready) {
        memset(g_vita_dummy_mission, 0, sizeof(g_vita_dummy_mission));
        /* CLevel::OnEnemyKilled only needs mission+0x3c for the crash path.
         * Keep it zero so mission-specific kill awards are disabled, but the
         * kill handler can still run score/XP/object cleanup paths. */
        *(uint32_t *)(void *)(g_vita_dummy_mission + 0x3c) = 0;
        g_vita_dummy_mission_ready = 1;
        sceClibPrintf("[FIX-MISSION] synthetic dummy mission=%p field_3c=0\n", g_vita_dummy_mission);
    }
    return g_vita_dummy_mission;
}

static void *trace_game_flow_get_mission(void *self) {
    void *mission;

    if (!trace_is_game_ptr(self)) {
        if (trace_allow("CGameFlow::GetMission/bad_self")) {
            sceClibPrintf("[FIX-MISSION] GetMission(this=%p) bad self -> dummy\n", self);
        }
        return vita_dummy_mission();
    }

    mission = SO_CONTINUE(void *, h_game_flow_get_mission, self);
    if (!trace_is_game_range(mission, 0x40)) {
        if (trace_allow("CGameFlow::GetMission/null")) {
            sceClibPrintf("[FIX-MISSION] GetMission(this=%p) returned %p -> dummy\n", self, mission);
        }
        return vita_dummy_mission();
    }

    return mission;
}

typedef struct GameObjectPackSlot {
    void *items;
    uint32_t count;
} GameObjectPackSlot;

enum {
    GOBJ_PACK_TYPE_COUNT = 28,
    GOBJ_PACK_MAX_TYPES = 64,
    GOBJ_PACK_MAX_LAZY_COUNT = 512,
};

static GameObjectPackSlot *gobj_alloc_slot_vector(uint32_t count) {
    uint32_t *raw;

    if (count == 0 || count > GOBJ_PACK_MAX_TYPES) {
        return NULL;
    }

    raw = (uint32_t *)calloc(1, 8u + count * sizeof(GameObjectPackSlot));
    if (!raw) {
        return NULL;
    }

    raw[0] = sizeof(GameObjectPackSlot);
    raw[1] = count;
    return (GameObjectPackSlot *)(void *)(raw + 2);
}

static void gobj_free_slot_vector(GameObjectPackSlot *slots, uint32_t count) {
    unsigned char *raw;

    if (!slots || count == 0 || count > GOBJ_PACK_MAX_TYPES) {
        return;
    }

    raw = (unsigned char *)(void *)slots - 8;
    if (trace_is_game_range(raw, 8u + count * sizeof(GameObjectPackSlot))) {
        free(raw);
    }
}

static GameObjectPackSlot *gobj_ensure_slot_vector(void *self,
                                                   size_t slot_offset,
                                                   size_t count_offset,
                                                   unsigned int type,
                                                   const char *label) {
    unsigned char *base = (unsigned char *)self;
    GameObjectPackSlot *slots = *(GameObjectPackSlot **)(void *)(base + slot_offset);
    uint32_t count = *(uint32_t *)(void *)(base + count_offset);
    uint32_t new_count = GOBJ_PACK_TYPE_COUNT;
    GameObjectPackSlot *new_slots;

    if (type < count &&
        count <= GOBJ_PACK_MAX_TYPES &&
        trace_is_game_range(slots, count * sizeof(GameObjectPackSlot))) {
        return slots;
    }

    if (type >= new_count) {
        new_count = type + 1;
    }

    if (new_count > GOBJ_PACK_MAX_TYPES) {
        return NULL;
    }

    new_slots = gobj_alloc_slot_vector(new_count);
    if (!new_slots) {
        return NULL;
    }

    if (count > 0 &&
        count <= GOBJ_PACK_MAX_TYPES &&
        trace_is_game_range(slots, count * sizeof(GameObjectPackSlot))) {
        sceClibMemcpy(new_slots, slots, count * sizeof(GameObjectPackSlot));
        gobj_free_slot_vector(slots, count);
    }

    *(GameObjectPackSlot **)(void *)(base + slot_offset) = new_slots;
    *(uint32_t *)(void *)(base + count_offset) = new_count;

    if (trace_allow(label)) {
        sceClibPrintf("[PATCH-GOBJ] repaired %s slot vector for pack=%p type=%u count=%u\n",
                      label, self, type, (unsigned int)new_count);
    }

    return new_slots;
}

static void *gobj_resize_items(void *items, uint32_t old_count, uint32_t new_count, size_t item_size) {
    void *new_items;
    size_t old_size = 0;

    if (new_count == 0 || new_count > GOBJ_PACK_MAX_LAZY_COUNT || item_size == 0) {
        return NULL;
    }

    new_items = calloc(new_count, item_size);
    if (!new_items) {
        return NULL;
    }

    if (items && old_count > 0 && old_count <= GOBJ_PACK_MAX_LAZY_COUNT) {
        old_size = old_count * item_size;
    }

    if (old_size > 0 && trace_is_game_range(items, old_size)) {
        sceClibMemcpy(new_items, items, old_size);
        free(items);
    }

    return new_items;
}

static int gobj_ensure_type_storage(void *self, unsigned int type, unsigned int index) {
    GameObjectPackSlot *object_slots;
    GameObjectPackSlot *state_slots;
    GameObjectPackSlot *object_slot;
    GameObjectPackSlot *state_slot;
    uint32_t needed;
    uint32_t object_count;
    uint32_t state_count;
    unsigned char *base = (unsigned char *)self;

    if (!trace_is_game_range(self, 0x30) ||
        type >= GOBJ_PACK_MAX_TYPES ||
        index == 0xff ||
        index >= GOBJ_PACK_MAX_LAZY_COUNT) {
        return 0;
    }

    needed = index + 1;
    object_slots = gobj_ensure_slot_vector(self, 0x04, 0x08, type, "object");
    state_slots = gobj_ensure_slot_vector(self, 0x0c, 0x10, type, "state");
    if (!object_slots || !state_slots) {
        return 0;
    }

    object_slot = &object_slots[type];
    state_slot = &state_slots[type];
    object_count = (object_slot->count >= needed &&
                    object_slot->count <= GOBJ_PACK_MAX_LAZY_COUNT) ? object_slot->count : needed;
    state_count = (state_slot->count >= needed &&
                   state_slot->count <= GOBJ_PACK_MAX_LAZY_COUNT) ? state_slot->count : needed;

    if (object_slot->count > GOBJ_PACK_MAX_LAZY_COUNT ||
        !trace_is_game_range(object_slot->items, object_slot->count * sizeof(void *)) ||
        object_slot->count < needed) {
        void *old_items = object_slot->items;
        void *items = gobj_resize_items(object_slot->items,
                                        object_slot->count,
                                        object_count,
                                        sizeof(void *));
        if (!items) {
            return 0;
        }

        object_slot->items = items;
        object_slot->count = object_count;
        if (trace_allow("CGameObjectPack/object_slot_repair")) {
            sceClibPrintf("[PATCH-GOBJ] repaired object slots pack=%p type=%u old_items=%p new_items=%p count=%u needed=%u\n",
                          self, type, old_items, object_slot->items,
                          (unsigned int)object_count, (unsigned int)needed);
        }
    }

    if (state_slot->count > GOBJ_PACK_MAX_LAZY_COUNT ||
        !trace_is_game_range(state_slot->items, state_slot->count) ||
        state_slot->count < needed) {
        void *items = gobj_resize_items(state_slot->items,
                                        state_slot->count,
                                        state_count,
                                        1);
        if (!items) {
            return 0;
        }

        state_slot->items = items;
        state_slot->count = state_count;
        if (trace_allow("CGameObjectPack/state_slot_repair")) {
            sceClibPrintf("[PATCH-GOBJ] repaired state slots pack=%p type=%u count=%u needed=%u\n",
                          self, type, (unsigned int)state_count, (unsigned int)needed);
        }
    }

    if (type < GOBJ_PACK_TYPE_COUNT && base[0x14 + type] < needed) {
        base[0x14 + type] = (unsigned char)needed;
    }

    return 1;
}

static void *trace_game_object_pack_get_game_object(void *self, unsigned int type, unsigned int index) {
    unsigned char *base = (unsigned char *)self;
    GameObjectPackSlot *slots;
    GameObjectPackSlot *slot;
    uint32_t type_count;

    if (index == 0xff || !trace_is_game_range(self, 0x0c)) {
        return NULL;
    }

    slots = *(GameObjectPackSlot **)(void *)(base + 0x04);
    type_count = *(uint32_t *)(void *)(base + 0x08);
    if (type >= type_count ||
        type_count > GOBJ_PACK_MAX_TYPES ||
        !trace_is_game_range(slots, type_count * sizeof(GameObjectPackSlot))) {
        return NULL;
    }

    slot = &slots[type];
    if (index >= slot->count ||
        slot->count > GOBJ_PACK_MAX_LAZY_COUNT ||
        !trace_is_game_range(slot->items, slot->count * sizeof(void *))) {
        return NULL;
    }

    return ((void **)slot->items)[index];
}

static void trace_game_object_pack_init_game_object(void *self, unsigned int type, unsigned int index) {
    typedef void (*game_object_init_fn_t)(void *, unsigned int, unsigned int);

    GUNBROS_PERF_COUNT(game_object_init_calls);
    if (!gobj_ensure_type_storage(self, type, index)) {
        if (trace_allow("CGameObjectPack::InitGameObject/bad_storage")) {
            sceClibPrintf("[PATCH-GOBJ] InitGameObject(pack=%p, type=%u, index=%u) skipped bad storage\n",
                          self, type, index);
        }
        return;
    }

    GUNBROS_PERF_BEGIN(object_begin);
    if (g_original_game_object_pack_init_game_object) {
        ((game_object_init_fn_t)g_original_game_object_pack_init_game_object)(
            self, type, index);
    } else {
        (void)SO_CONTINUE(int, h_game_object_pack_init_game_object,
                          self, type, index);
    }
    GUNBROS_PERF_END(GUNBROS_STAGE_OBJECT, object_begin, 0);
}

static void *gameplay_find_brother_target(void);
static void gameplay_repair_player_model(void *brother, const char *source);
static void gameplay_force_player_gun(unsigned int subtype, unsigned int id, const char *source);
static int gameplay_gun_ref_resolves(unsigned int id, unsigned int subtype, const char *source);
static int gameplay_armor_ref_resolves(unsigned int id, unsigned int subtype, const char *source);
static int gameplay_install_store_gun_ref(const unsigned char *ref,
                                           unsigned int id,
                                           unsigned int subtype,
                                           unsigned int equip_now,
                                           const char *source);
static int gameplay_apply_configured_player_gun(void *config, const char *source);
