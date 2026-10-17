static void vita_asset_preload_tick(void);

static int trace_menu_stack_set_menu(void *self, const void *config, unsigned short menu_id, unsigned char flags, int arg) {
    int log = trace_allow("CMenuStack::SetMenu");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuStack::SetMenu(this=%p, config=%p, menu=%u, flags=%u, arg=%d)\n",
                      self, config, (unsigned int)menu_id, (unsigned int)flags, arg);
    }

    ret = SO_CONTINUE(int, h_menu_stack_set_menu, self, config, menu_id, flags, arg);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuStack::SetMenu(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_menu_stack_push_menu(void *self, const void *config, unsigned short menu_id, unsigned char flags, int arg) {
    int log = trace_allow("CMenuStack::PushMenu");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuStack::PushMenu(this=%p, config=%p, menu=%u, flags=%u, arg=%d)\n",
                      self, config, (unsigned int)menu_id, (unsigned int)flags, arg);
    }

    ret = SO_CONTINUE(int, h_menu_stack_push_menu, self, config, menu_id, flags, arg);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuStack::PushMenu(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_menu_stack_pop_menu(void *self, const void *config, unsigned char flags) {
    int log = trace_allow("CMenuStack::PopMenu");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuStack::PopMenu(this=%p, config=%p, flags=%u)\n",
                      self, config, (unsigned int)flags);
    }

    ret = SO_CONTINUE(int, h_menu_stack_pop_menu, self, config, flags);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuStack::PopMenu(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_menu_navigation_bar_hide_buttons(void *self, unsigned char hide) {
    const unsigned char *base = (const unsigned char *)self;
    void *movie = self ? *(void **)(void *)(base + 0x0c) : NULL;
    int log = trace_allow("CMenuNavigationBar::HideButtons");
    int ret;

    if (!movie) {
        sceClibPrintf("[PATCH-MENU] CMenuNavigationBar::HideButtons(this=%p, hide=%u) skipped null movie\n",
                      self, (unsigned int)hide);
        return 0;
    }

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuNavigationBar::HideButtons(this=%p, movie=%p, hide=%u)\n",
                      self, movie, (unsigned int)hide);
    }

    ret = SO_CONTINUE(int, h_menu_navigation_bar_hide_buttons, self, hide);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuNavigationBar::HideButtons(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static void *trace_menu_system_get_font(void *self,
                                        int font,
                                        unsigned char allow_load) {
    typedef void *(*menu_get_font_fn_t)(void *, int, unsigned char);
    void *result;
    static unsigned int success_logs;
    static unsigned int failure_logs;
    if (g_original_menu_system_get_font) {
        result = ((menu_get_font_fn_t)g_original_menu_system_get_font)(
            self, font, (unsigned char)1);
    } else {
        result = SO_CONTINUE(void *, h_menu_system_get_font,
                             self, font, (unsigned char)1);
    }

    if (result) {
        if (success_logs < 12u) {
            sceClibPrintf("[FIX-FONT] CMenuSystem::GetFont(font=%d allow=%u->1) -> %p\n",
                          font, (unsigned int)allow_load, result);
            success_logs++;
        }
    } else if (failure_logs < 24u) {
        sceClibPrintf("[FIX-FONT] CMenuSystem::GetFont(font=%d allow=%u->1) still NULL self=%p\n",
                      font, (unsigned int)allow_load, self);
        failure_logs++;
    }

    return result;
}

enum {
    VITA_REFINERY_TIERS_PER_PHASE = 6,
    VITA_REFINERY_PREMIUM_FIRST = 0,
    VITA_REFINERY_STANDARD_FIRST = 6,
    VITA_REFINERY_INTERVAL_COUNT = 12,
    VITA_REFINERY_SLOT_OFFSET = 0xd8,
    VITA_REFINERY_SLOT_SIZE = 0x20,
    VITA_REFINERY_GATE_OFFSET = 0xc8,
    VITA_REFINERY_MANAGER_SIZE = 0x268,
    VITA_REFINERY_MAX_DURATION_MS = 604800000,
    VITA_REFINERY_ACTIVE_REFRESH_US = 1000000,
    VITA_REFINERY_IDLE_REFRESH_US = 30000000,
    VITA_REFINERY_REFRESH_SPACING_US = 100000,
    VITA_REFINERY_MAGIC = 0x52464247u, /* "GBFR" */
    VITA_REFINERY_VERSION = 4,
    VITA_REFINERY_OLDEST_VERSION = 1,
};

static const uint32_t g_vita_refinery_duration_ms[VITA_REFINERY_INTERVAL_COUNT] = {
    /* Premium phase, BIG resource minutes: 0, 5, 15, 120, 480, 1440. */
    0u,
    300000u,
    900000u,
    7200000u,
    28800000u,
    86400000u,

    /* Standard phase, BIG resource minutes: 0, 10, 30, 1440, 2880, 4320. */
    0u,
    600000u,
    1800000u,
    86400000u,
    172800000u,
    259200000u,
};

static const float g_vita_refinery_efficiency[VITA_REFINERY_INTERVAL_COUNT] = {
    /* BIG resource 43: 100%, 120%, 150%, 200%, 300%, 500%.
     * Both phases yield common coins (native CollectResources, 0x2fae90). */
    1.00f,
    1.20f,
    1.50f,
    2.00f,
    3.00f,
    5.00f,

    /* Standard: 100%, 115%, 130%, 150%, 200%, 400%. */
    1.00f,
    1.15f,
    1.30f,
    1.50f,
    2.00f,
    4.00f,
};

typedef struct VitaRefineryDisk {
    uint32_t magic;
    uint32_t version;
    uint32_t slot_count;
    uint32_t saved_at_seconds;
    uint32_t checksum;
    uint32_t deadlines[VITA_REFINERY_INTERVAL_COUNT];
    unsigned char slots[VITA_REFINERY_INTERVAL_COUNT]
                       [VITA_REFINERY_SLOT_SIZE];
} VitaRefineryDisk;

static void *g_vita_refinery_manager;
static uint32_t g_vita_refinery_deadlines[VITA_REFINERY_INTERVAL_COUNT];
static unsigned char
    g_vita_refinery_resident_slots[VITA_REFINERY_INTERVAL_COUNT]
                                     [VITA_REFINERY_SLOT_SIZE];
static uint32_t g_vita_refinery_last_tick_seconds;
static int g_vita_refinery_manager_ready;
static int g_vita_refinery_resident_valid;

typedef struct VitaRefineryRefreshThrottle {
    void *meter;
    uint64_t last_refresh_us;
} VitaRefineryRefreshThrottle;

static VitaRefineryRefreshThrottle
    g_vita_refinery_refresh_meters[VITA_REFINERY_INTERVAL_COUNT];
static uint64_t g_vita_refinery_last_any_refresh_us;
static void *g_vita_refinery_refresh_menu;
static unsigned int g_vita_refinery_refresh_generation = 1u;
static unsigned int g_vita_refinery_refresh_applied_generation;

static void vita_refinery_apply_meter_state(void *meter,
                                             unsigned int meter_index);
static int vita_refinery_meter_has_payload(unsigned int meter_index,
                                            uint32_t *state_out);

static void vita_refinery_request_meter_refresh(void) {
    g_vita_refinery_refresh_generation++;
    if (g_vita_refinery_refresh_generation == 0u) {
        g_vita_refinery_refresh_generation = 1u;
    }
}

static void vita_refinery_request_slot_refresh(unsigned int index) {
    if (index < VITA_REFINERY_INTERVAL_COUNT) {
        g_vita_refinery_refresh_meters[index].last_refresh_us = 0u;
        g_vita_refinery_last_any_refresh_us = 0u;
    }
}

static uint32_t vita_refinery_now_seconds(void) {
    struct timeval now;

    if (gettimeofday(&now, NULL) != 0 || now.tv_sec <= 0) {
        return 0u;
    }
    return (uint32_t)now.tv_sec;
}

static unsigned char *vita_refinery_slot(void *manager, unsigned int index) {
    return (unsigned char *)manager + VITA_REFINERY_SLOT_OFFSET +
           index * VITA_REFINERY_SLOT_SIZE;
}

static void vita_refinery_capture_resident(void *manager) {
    unsigned int i;

    if (!trace_is_game_range(manager, VITA_REFINERY_MANAGER_SIZE)) {
        return;
    }
    for (i = 0; i < VITA_REFINERY_INTERVAL_COUNT; ++i) {
        memcpy(g_vita_refinery_resident_slots[i],
               vita_refinery_slot(manager, i), VITA_REFINERY_SLOT_SIZE);
    }
    g_vita_refinery_resident_valid = 1;
}

static int vita_refinery_restore_resident(void *manager,
                                          const char *source) {
    unsigned int i;
    unsigned int repaired = 0u;

    if (!g_vita_refinery_resident_valid ||
        !trace_is_game_range(manager, VITA_REFINERY_MANAGER_SIZE)) {
        return 0;
    }
    for (i = 0; i < VITA_REFINERY_INTERVAL_COUNT; ++i) {
        unsigned char *slot = vita_refinery_slot(manager, i);

        if (memcmp(slot, g_vita_refinery_resident_slots[i],
                   VITA_REFINERY_SLOT_SIZE) != 0) {
            memcpy(slot, g_vita_refinery_resident_slots[i],
                   VITA_REFINERY_SLOT_SIZE);
            vita_refinery_request_slot_refresh(i);
            repaired++;
        }
    }
    if (repaired != 0u) {
        sceClibPrintf("[REFINERY][RESIDENT] restored=%u source=%s manager=%p\n",
                      repaired, source ? source : "?", manager);
        GUNBROS_PERF_LOG("[REFINERY-STATE] event=resident-restore slots=%u source=%s\n",
                         repaired, source ? source : "?");
        vita_refinery_request_meter_refresh();
    }
    return repaired != 0u;
}

static void vita_refinery_reset_slot(unsigned char *slot) {
    memset(slot, 0, VITA_REFINERY_SLOT_SIZE);
    *(uint32_t *)(void *)slot = 1u;
}

/* BIG 43: Standard tiers depend on the preceding tier. In Classic a
 * successful collection opens the next one; Premium uses the native wallet. */
static int vita_refinery_apply_progression(void *manager) {
    if (!trace_is_game_range(manager, VITA_REFINERY_MANAGER_SIZE)) return 0;
    for (unsigned i = 0; i < VITA_REFINERY_INTERVAL_COUNT; ++i) {
        unsigned char *base = manager;
        uint32_t state = *(uint32_t *)(void *)vita_refinery_slot(manager, i);
        base[VITA_REFINERY_GATE_OFFSET+i] =
            (i > 6 && state == 0) ? (unsigned char)(i-1) : 0;
        /* Native CheckForFreeUnlock reads the arrays directly, bypassing
         * the hooked getter. Supply the same verified prices there too. */
        static const uint32_t rare[12] = {0,40,80,200,480,1040,0,0,0,0,0,0};
        *(uint32_t *)(void *)(base+0x68+i*4) = 0;
        *(uint32_t *)(void *)(base+0x98+i*4) = rare[i];
    }
    return 0;
}

static int vita_refinery_write_sidecar(void *manager) {
    static const char final_path[] = DATA_PATH "vita_refinery_v1.dat";
    static const char temp_path[] = DATA_PATH "vita_refinery_v1.tmp";
    VitaRefineryDisk disk;
    uint32_t now;
    unsigned int i;
    unsigned int active = 0;
    unsigned int complete = 0;
    FILE *fp;
    size_t written;

    if (!trace_is_game_range(manager, VITA_REFINERY_MANAGER_SIZE)) {
        return 0;
    }

    now = vita_refinery_now_seconds();
    memset(&disk, 0, sizeof(disk));
    disk.magic = VITA_REFINERY_MAGIC;
    disk.version = VITA_REFINERY_VERSION;
    disk.slot_count = VITA_REFINERY_INTERVAL_COUNT;
    disk.saved_at_seconds = now;
    for (i = 0; i < VITA_REFINERY_INTERVAL_COUNT; ++i) {
        unsigned char *slot = vita_refinery_slot(manager, i);
        uint32_t state = *(const uint32_t *)(const void *)slot;
        uint32_t remaining = *(const uint32_t *)(const void *)(slot + 8u);

        if (state == 2u) {
            if (g_vita_refinery_deadlines[i] == 0u && now != 0u) {
                g_vita_refinery_deadlines[i] = now +
                    (remaining + 999u) / 1000u;
            }
            active++;
        } else {
            g_vita_refinery_deadlines[i] = 0u;
            if (state == 3u) {
                complete++;
            }
        }
        disk.deadlines[i] = g_vita_refinery_deadlines[i];
        memcpy(disk.slots[i], slot, VITA_REFINERY_SLOT_SIZE);
    }
    vita_refinery_capture_resident(manager);
    disk.checksum = 0u;
    disk.checksum = vita_profile_checksum(&disk, sizeof(disk));

    fp = fopen(temp_path, "wb");
    if (!fp) {
        sceClibPrintf("[REFINERY][SAVE][ERROR] open failed path=%s\n",
                      temp_path);
        return 0;
    }
    written = fwrite(&disk, 1, sizeof(disk), fp);
    (void)fflush(fp);
    if (fclose(fp) != 0 || written != sizeof(disk)) {
        (void)remove(temp_path);
        sceClibPrintf("[REFINERY][SAVE][ERROR] write failed path=%s bytes=%u/%u\n",
                      temp_path, (unsigned int)written,
                      (unsigned int)sizeof(disk));
        return 0;
    }
    if (rename(temp_path, final_path) != 0) {
        (void)remove(final_path);
        if (rename(temp_path, final_path) != 0) {
            (void)remove(temp_path);
            sceClibPrintf("[REFINERY][SAVE][ERROR] rename failed temp=%s final=%s\n",
                          temp_path, final_path);
            return 0;
        }
    }

    sceClibPrintf("[REFINERY][SAVE] path=%s active=%u complete=%u now=%u checksum=%08x\n",
                  final_path, active, complete, now, disk.checksum);
    GUNBROS_PERF_LOG("[REFINERY-STATE] event=saved active=%u complete=%u now=%u checksum=%08x\n",
                     active, complete, now, disk.checksum);
    return 1;
}

#include "reimpl/native_refinery.h"

static void vita_refinery_load_sidecar(void *manager) {
    static const char final_path[] = DATA_PATH "vita_refinery_v1.dat";
    VitaRefineryDisk disk;
    uint32_t stored;
    uint32_t expected;
    uint32_t now;
    unsigned int i;
    unsigned int active = 0;
    unsigned int complete = 0;
    int loaded = 0;
    int legacy_reset = 0;
    int rejected = 0;
    int normalized = 0;
    FILE *fp;
    size_t read_size = 0;

    if (!trace_is_game_range(manager, VITA_REFINERY_MANAGER_SIZE)) {
        return;
    }
    if (g_vita_refinery_manager_ready &&
        g_vita_refinery_manager == manager) {
        return;
    }

    g_vita_refinery_manager = manager;
    g_vita_refinery_manager_ready = 1;
    g_vita_refinery_last_tick_seconds = 0u;
    memset(g_vita_refinery_deadlines, 0,
           sizeof(g_vita_refinery_deadlines));
    memset(&disk, 0, sizeof(disk));

    fp = fopen(final_path, "rb");
    if (fp) {
        read_size = fread(&disk, 1, sizeof(disk), fp);
        (void)fclose(fp);
        stored = disk.checksum;
        disk.checksum = 0u;
        expected = vita_profile_checksum(&disk, sizeof(disk));
        if (read_size == sizeof(disk) &&
            disk.magic == VITA_REFINERY_MAGIC &&
            disk.version >= VITA_REFINERY_OLDEST_VERSION &&
            disk.version <= VITA_REFINERY_VERSION &&
            disk.slot_count == VITA_REFINERY_INTERVAL_COUNT &&
            stored == expected) {
            if (disk.version >= 3) {
                if (disk.version < VITA_REFINERY_VERSION) normalized = 1;
                for (i = 0; i < VITA_REFINERY_INTERVAL_COUNT; ++i) {
                    memcpy(vita_refinery_slot(manager, i), disk.slots[i],
                           VITA_REFINERY_SLOT_SIZE);
                    g_vita_refinery_deadlines[i] = disk.deadlines[i];
                }
                loaded = 1;
            } else {
                /* Versions 1/2 could clone a completed native record into
                 * several meters. Those records are indistinguishable from a
                 * real completion. Preserve strictly valid jobs that are
                 * still counting down, but migrate every idle/completed
                 * legacy record to a known-empty v3 slot instead of keeping
                 * collect icons that cannot be collected. */
                legacy_reset = 1;
                loaded = 1;
                normalized = 1;
                for (i = 0; i < VITA_REFINERY_INTERVAL_COUNT; ++i) {
                    const unsigned char *old_slot = disk.slots[i];
                    uint32_t old_state =
                        *(const uint32_t *)(const void *)old_slot;
                    uint32_t old_remaining =
                        *(const uint32_t *)(const void *)(old_slot + 8u);
                    uint32_t old_total =
                        *(const uint32_t *)(const void *)(old_slot + 0x10u);
                    uint64_t old_amount =
                        *(const uint64_t *)(const void *)(old_slot + 0x18u);

                    vita_refinery_reset_slot(vita_refinery_slot(manager, i));
                    g_vita_refinery_deadlines[i] = 0u;
                    if (old_state == 2u &&
                        old_amount != 0u && old_total != 0u &&
                        old_total == g_vita_refinery_duration_ms[i] &&
                        old_total <= VITA_REFINERY_MAX_DURATION_MS &&
                        old_remaining != 0u && old_remaining <= old_total) {
                        memcpy(vita_refinery_slot(manager, i), old_slot,
                               VITA_REFINERY_SLOT_SIZE);
                        g_vita_refinery_deadlines[i] = disk.deadlines[i];
                    }
                }
            }
        } else {
            rejected = 1;
            sceClibPrintf("[REFINERY][LOAD][ERROR] rejected path=%s bytes=%u magic=%08x version=%u slots=%u stored=%08x expected=%08x\n",
                          final_path, (unsigned int)read_size, disk.magic,
                          disk.version, disk.slot_count, stored, expected);
        }
    }

    now = vita_refinery_now_seconds();
    if (!loaded) {
        for (i = 0; i < VITA_REFINERY_INTERVAL_COUNT; ++i) {
            vita_refinery_reset_slot(vita_refinery_slot(manager, i));
            *(uint32_t *)(void *)vita_refinery_slot(manager, i) = (i == 0 || i == 6) ? 1u : 0u;
            g_vita_refinery_deadlines[i] = 0u;
        }
        normalized = 1;
    }
    for (i = 0; i < VITA_REFINERY_INTERVAL_COUNT; ++i) {
        unsigned char *slot = vita_refinery_slot(manager, i);
        uint32_t *state = (uint32_t *)(void *)slot;
        uint32_t *remaining = (uint32_t *)(void *)(slot + 8u);
        uint32_t total = *(const uint32_t *)(const void *)(slot + 0x10u);
        uint64_t amount = *(const uint64_t *)(const void *)(slot + 0x18u);
        uint32_t deadline = g_vita_refinery_deadlines[i];
        uint32_t expected_total = g_vita_refinery_duration_ms[i];
        float *efficiency = (float *)(void *)(slot + 4u);
        if (!gunbros_refinery_slot_valid(slot, expected_total)) {
            vita_refinery_reset_slot(slot);
            g_vita_refinery_deadlines[i] = 0u;
            normalized = 1;
            continue;
        }
        if (loaded && disk.version < 4 && *state == 1u && i != 0 && i != 6) {
            *state = 0;
            normalized = 1;
        }
        if (*state == 1u &&
                   (*remaining != 0u ||
                    *(const uint32_t *)(const void *)(slot + 0x0cu) != 0u ||
                    total != 0u || *efficiency != 0.0f || amount != 0u)) {
            vita_refinery_reset_slot(slot);
            normalized = 1;
        }
        if ((*state == 2u || *state == 3u) &&
            *efficiency != g_vita_refinery_efficiency[i]) {
            *efficiency = g_vita_refinery_efficiency[i];
            normalized = 1;
        }
        if (*state == 2u) {
            uint32_t max_seconds = (total + 999u) / 1000u;

            if (deadline != 0u && now != 0u &&
                ((loaded && now < disk.saved_at_seconds) ||
                 (deadline > now && deadline - now > max_seconds))) {
                deadline = now + (*remaining + 999u) / 1000u;
                g_vita_refinery_deadlines[i] = deadline;
                normalized = 1;
            }
            if (deadline == 0u && now != 0u) {
                deadline = now + (*remaining + 999u) / 1000u;
                g_vita_refinery_deadlines[i] = deadline;
                normalized = 1;
            }
            if (deadline != 0u && now >= deadline) {
                *remaining = 0u;
                *state = 3u;
                g_vita_refinery_deadlines[i] = 0u;
                complete++;
                normalized = 1;
            } else {
                if (deadline != 0u && now != 0u) {
                    *remaining = (deadline - now) * 1000u;
                }
                active++;
            }
        } else if (*state == 3u) {
            g_vita_refinery_deadlines[i] = 0u;
            complete++;
        } else {
            g_vita_refinery_deadlines[i] = 0u;
        }
    }
    if (loaded && disk.version < 4) {
        for (i = 7; i < 12; ++i) {
            if (*(uint32_t *)(void *)vita_refinery_slot(manager, i) >= 2) {
                for (unsigned j=6; j<i; ++j)
                    if (*(uint32_t *)(void *)vita_refinery_slot(manager, j) == 0)
                        *(uint32_t *)(void *)vita_refinery_slot(manager, j) = 1;
            }
        }
    }
    normalized |= vita_refinery_apply_progression(manager);
    if (now != 0u) {
        *(uint32_t *)(void *)((unsigned char *)manager + 0x258u) = now;
        g_vita_refinery_last_tick_seconds = now;
    }
    vita_refinery_capture_resident(manager);

    sceClibPrintf("[REFINERY][LOAD] path=%s loaded=%d legacy_reset=%d active=%u complete=%u now=%u\n",
                  final_path, loaded, legacy_reset, active, complete, now);
    GUNBROS_PERF_LOG("[REFINERY-STATE] event=loaded valid=%d legacy_reset=%d rejected=%d active=%u complete=%u now=%u slots=12\n",
                     loaded, legacy_reset, rejected, active, complete, now);
    if (normalized || rejected || legacy_reset) {
        (void)vita_refinery_write_sidecar(manager);
    }
}

static void trace_refinement_update_local(void *manager) {
    uint32_t now;
    unsigned int i;
    unsigned int completed = 0;
    int deadline_created = 0;

    vita_refinery_load_sidecar(manager);
    if (!trace_is_game_range(manager, VITA_REFINERY_MANAGER_SIZE)) {
        return;
    }
    (void)vita_refinery_restore_resident(manager, "UpdateRefinement");
    now = vita_refinery_now_seconds();
    if (now == 0u || now == g_vita_refinery_last_tick_seconds) {
        return;
    }
    g_vita_refinery_last_tick_seconds = now;

    for (i = 0; i < VITA_REFINERY_INTERVAL_COUNT; ++i) {
        unsigned char *slot = vita_refinery_slot(manager, i);
        uint32_t *state = (uint32_t *)(void *)slot;
        uint32_t *remaining = (uint32_t *)(void *)(slot + 8u);
        uint32_t deadline;

        if (*state != 2u) {
            continue;
        }
        deadline = g_vita_refinery_deadlines[i];
        if (deadline == 0u) {
            deadline = now + (*remaining + 999u) / 1000u;
            g_vita_refinery_deadlines[i] = deadline;
            deadline_created = 1;
        }
        if (now >= deadline) {
            *remaining = 0u;
            *state = 3u;
            g_vita_refinery_deadlines[i] = 0u;
            completed++;
            vita_refinery_request_slot_refresh(i);
            sceClibPrintf("[REFINERY][COMPLETE] slot=%u amount=%llu\n",
                          i, (unsigned long long)
                              *(const uint64_t *)(const void *)(slot + 0x18u));
            GUNBROS_PERF_LOG("[REFINERY-JOB] event=complete slot=%u phase=%s input=%llu\n",
                             i,
                             i < VITA_REFINERY_TIERS_PER_PHASE ?
                                 "premium" : "standard",
                             (unsigned long long)
                                 *(const uint64_t *)(const void *)
                                     (slot + 0x18u));
        } else {
            *remaining = (deadline - now) * 1000u;
        }
    }
    *(uint32_t *)(void *)((unsigned char *)manager + 0x258u) = now;
    vita_refinery_capture_resident(manager);
    if (completed != 0u || deadline_created) {
        (void)vita_refinery_write_sidecar(manager);
    }
}

static int trace_refinement_begin_local(void *manager,
                                        unsigned int slot_index,
                                        int interval,
                                        uint64_t amount,
                                        void *progress) {
    int result;
    int profile_saved = 0;
    unsigned char *slot;
    uint32_t now;
    uint64_t balance;

    vita_refinery_load_sidecar(manager);
    (void)vita_refinery_restore_resident(manager, "BeginRefinement");
    if (slot_index >= VITA_REFINERY_INTERVAL_COUNT ||
        !trace_is_game_range(progress, 0x38u)) {
        return 0;
    }
    if (interval != (int)slot_index) {
        sceClibPrintf("[REFINERY][BEGIN][INTERVAL] slot=%u requested=%d corrected=%u\n",
                      slot_index, interval, slot_index);
        interval = (int)slot_index;
    }
    slot = vita_refinery_slot(manager, slot_index);
    balance = *(const uint64_t *)(const void *)
        ((const unsigned char *)progress + 0x30u);
    if (*(const uint32_t *)(const void *)slot != 1u ||
        amount == 0u || amount > balance) {
        sceClibPrintf("[REFINERY][BEGIN][REJECT] slot=%u interval=%d amount=%llu balance=%llu state=%u\n",
                      slot_index, interval, (unsigned long long)amount,
                      (unsigned long long)balance,
                      *(const uint32_t *)(const void *)slot);
        GUNBROS_PERF_LOG("[REFINERY-JOB] event=begin-reject slot=%u phase=%s interval=%d input=%llu balance=%llu state=%u\n",
                         slot_index,
                         slot_index < VITA_REFINERY_TIERS_PER_PHASE ?
                             "premium" : "standard",
                         interval, (unsigned long long)amount,
                         (unsigned long long)balance,
                         *(const uint32_t *)(const void *)slot);
        return 0;
    }
    result = SO_CONTINUE(int, h_refinement_begin, manager, slot_index,
                         interval, amount, progress);
    now = vita_refinery_now_seconds();
    if (result && *(const uint32_t *)(const void *)slot == 2u && now != 0u) {
        uint32_t remaining = *(const uint32_t *)(const void *)(slot + 8u);

        if (remaining == 0u) {
            *(uint32_t *)(void *)slot = 3u;
            g_vita_refinery_deadlines[slot_index] = 0u;
        } else {
            g_vita_refinery_deadlines[slot_index] = now +
                                                   (remaining + 999u) / 1000u;
        }
    } else if (result) {
        g_vita_refinery_deadlines[slot_index] = 0u;
    }
    if (result) {
        (void)vita_refinery_write_sidecar(manager);
        vita_offline_profile_request_save();
        profile_saved = vita_offline_profile_save(1);
    }
    sceClibPrintf("[REFINERY][BEGIN] slot=%u phase=%s interval=%d amount=%llu state=%u remaining_ms=%u result=%d\n",
                  slot_index,
                  slot_index < VITA_REFINERY_TIERS_PER_PHASE ?
                      "premium" : "standard",
                  interval, (unsigned long long)amount,
                  *(const uint32_t *)(const void *)slot,
                  *(const uint32_t *)(const void *)(slot + 8u), result);
    GUNBROS_PERF_LOG("[REFINERY-JOB] event=begin slot=%u phase=%s interval=%d input=%llu state=%u remaining_ms=%u result=%d profile_saved=%d\n",
                     slot_index,
                     slot_index < VITA_REFINERY_TIERS_PER_PHASE ?
                         "premium" : "standard",
                     interval, (unsigned long long)amount,
                     *(const uint32_t *)(const void *)slot,
                     *(const uint32_t *)(const void *)(slot + 8u), result,
                     profile_saved);
    if (result) {
        vita_refinery_request_slot_refresh(slot_index);
    }
    return result;
}

static int trace_refinement_collect_local(void *manager,
                                          unsigned int slot_index,
                                          void *progress) {
    typedef uint64_t (*refinery_slot_collect_fn_t)(void *);
    typedef void (*add_common_currency_fn_t)(void *, uint64_t);
    typedef void (*save_state_change_fn_t)(void *);
    static refinery_slot_collect_fn_t slot_collect;
    static add_common_currency_fn_t add_common_currency;
    static save_state_change_fn_t save_state_change;
    static int looked_up;
    unsigned char *slot;
    uint64_t input_amount;
    uint64_t output_amount = 0u;
    uint64_t credited_output = 0u;
    int premium;
    int result = 0;
    int profile_saved = 0;

    vita_refinery_load_sidecar(manager);
    (void)vita_refinery_restore_resident(manager, "CollectResources");
    if (slot_index >= VITA_REFINERY_INTERVAL_COUNT ||
        !trace_is_game_range(progress, 0x44u)) {
        return 0;
    }
    slot = vita_refinery_slot(manager, slot_index);
    premium = slot_index < VITA_REFINERY_TIERS_PER_PHASE;
    input_amount = *(const uint64_t *)(const void *)(slot + 0x18u);

    if (!looked_up) {
        slot_collect = (refinery_slot_collect_fn_t)(uintptr_t)
            so_symbol(&so_mod,
                      "_ZN18CRefinementManager15CRefinementSlot7CollectEv");
        add_common_currency = (add_common_currency_fn_t)(uintptr_t)
            so_symbol(&so_mod,
                      "_ZN15CPlayerProgress12ProgressData17AddCommonCurrencyEy");
        save_state_change = (save_state_change_fn_t)(uintptr_t)
            so_symbol(&so_mod,
                      "_ZN18CRefinementManager15SaveStateChangeEv");
        looked_up = 1;
    }

    if (*(const uint32_t *)(const void *)slot != 3u || !slot_collect ||
        !save_state_change || !add_common_currency) {
        GUNBROS_PERF_LOG("[REFINERY-JOB] event=collect-reject slot=%u phase=%s state=%u slot_collect=%p add_common=%p save=%p\n",
                         slot_index,
                         premium ? "premium" : "standard",
                         *(const uint32_t *)(const void *)slot,
                         (void *)slot_collect, (void *)add_common_currency,
                         (void *)save_state_change);
        return 0;
    }

    /* Native CollectResources credits AddCommonCurrency for every slot;
     * Premium describes the refinery tier, not a different reward wallet. */
    output_amount = slot_collect(slot);
    if (output_amount != 0u) {
        add_common_currency((unsigned char *)progress + 0x28u, output_amount);
    }
    credited_output = output_amount;
    /* Native Collect intentionally retains +0x10 (the old duration). Clear
     * the complete 0x20-byte record so only this collected meter becomes a
     * genuinely empty/unlocked slot. */
    vita_refinery_reset_slot(slot);
    if (slot_index >= 6 && slot_index < 11 &&
        *(uint32_t *)(void *)vita_refinery_slot(manager, slot_index+1) == 0)
        *(uint32_t *)(void *)vita_refinery_slot(manager, slot_index+1) = 1;
    vita_refinery_apply_progression(manager);
    save_state_change(manager);
    result = 1;

    if (result) {
        g_vita_refinery_deadlines[slot_index] = 0u;
        (void)vita_refinery_write_sidecar(manager);
        vita_offline_profile_request_save();
        /* Persist the coin payout before a hard close can reopen an
         * already-reset slot with the old wallet balance. */
        profile_saved = vita_offline_profile_save(1);
        sceClibPrintf("[REFINERY][COLLECT] slot=%u result=1\n", slot_index);
        GUNBROS_PERF_LOG("[REFINERY-JOB] event=collect slot=%u phase=%s input=%llu output=%llu currency=%s result=1 profile_saved=%d\n",
                         slot_index,
                         slot_index < VITA_REFINERY_TIERS_PER_PHASE ?
                             "premium" : "standard",
                         (unsigned long long)input_amount,
                         (unsigned long long)credited_output,
                         "common",
                         profile_saved);
        vita_refinery_request_slot_refresh(slot_index);
    }
    return result;
}

static void trace_refinery_meter_refresh(void *meter, void *menu) {
    typedef void (*refinery_meter_refresh_fn_t)(void *, void *);
    static unsigned int perf_logs;
    uint64_t now = sceKernelGetProcessTimeWide();
    uint64_t start;
    uint64_t end;
    uint64_t refresh_interval_us = VITA_REFINERY_IDLE_REFRESH_US;
    unsigned int i;
    unsigned int meter_index = VITA_REFINERY_INTERVAL_COUNT;
    VitaRefineryRefreshThrottle *throttle;

    GUNBROS_PERF_COUNT(refinery_refresh_calls);

    if (menu != g_vita_refinery_refresh_menu) {
        memset(g_vita_refinery_refresh_meters, 0,
               sizeof(g_vita_refinery_refresh_meters));
        g_vita_refinery_last_any_refresh_us = 0u;
        g_vita_refinery_refresh_menu = menu;
    }

    if (g_vita_refinery_refresh_applied_generation !=
        g_vita_refinery_refresh_generation) {
        for (i = 0; i < VITA_REFINERY_INTERVAL_COUNT; ++i) {
            g_vita_refinery_refresh_meters[i].last_refresh_us = 0u;
        }
        g_vita_refinery_last_any_refresh_us = 0u;
        g_vita_refinery_refresh_applied_generation =
            g_vita_refinery_refresh_generation;
    }

    if (trace_is_game_range(meter, 8u)) {
        meter_index = *(const uint16_t *)(const void *)
            ((const unsigned char *)meter + 4u);
    }
    if (meter_index >= VITA_REFINERY_INTERVAL_COUNT) {
        /* An unbound/foreign meter must not alias slot zero's throttle. */
        if (g_original_refinery_meter_refresh) {
            ((refinery_meter_refresh_fn_t)
                 g_original_refinery_meter_refresh)(meter, menu);
        } else {
            (void)SO_CONTINUE(int, h_refinery_meter_refresh, meter, menu);
        }
        return;
    }
    vita_refinery_apply_meter_state(meter, meter_index);
    throttle = &g_vita_refinery_refresh_meters[meter_index];
    if (throttle->meter != meter) {
        throttle->meter = meter;
        throttle->last_refresh_us = 0u;
    }
    if (g_vita_refinery_manager_ready &&
        trace_is_game_range(g_vita_refinery_manager,
                            VITA_REFINERY_MANAGER_SIZE) &&
        *(const uint32_t *)(const void *)
            vita_refinery_slot(g_vita_refinery_manager, meter_index) == 2u) {
        refresh_interval_us = VITA_REFINERY_ACTIVE_REFRESH_US;
    }
    if (throttle->last_refresh_us != 0u &&
        now - throttle->last_refresh_us < refresh_interval_us) {
        return;
    }
    if (g_vita_refinery_last_any_refresh_us != 0u &&
        now - g_vita_refinery_last_any_refresh_us <
            VITA_REFINERY_REFRESH_SPACING_US) {
        return;
    }

    start = now;
    GUNBROS_PERF_COUNT(refinery_refresh_runs);
    if (g_original_refinery_meter_refresh) {
        ((refinery_meter_refresh_fn_t)g_original_refinery_meter_refresh)(
            meter, menu);
    } else {
        (void)SO_CONTINUE(int, h_refinery_meter_refresh, meter, menu);
    }
    /* Native Refresh can select a chapter from the stale global provider.
     * The local slot remains authoritative for inactive/active/complete. */
    vita_refinery_apply_meter_state(meter, meter_index);
    if (g_vita_refinery_manager_ready) {
        trace_refinement_update_local(g_vita_refinery_manager);
        unsigned char *slot = vita_refinery_slot(g_vita_refinery_manager, meter_index);
        if (*(uint32_t *)(void *)slot == 2u) {
            typedef void *(*assign_fn)(void *, const uint32_t *);
            static assign_fn assign;
            if (!assign) assign = (assign_fn)(uintptr_t)so_symbol(&so_mod,
                "_ZN3com3glu8platform10components9CStrWCharaSEPKw");
            unsigned seconds = (*(uint32_t *)(void *)(slot+8)+999u)/1000u;
            char label[32]; uint32_t wide[32];
            snprintf(label, sizeof(label), "%02u:%02u:%02u", seconds/3600, seconds/60%60, seconds%60);
            unsigned n=0; do { wide[n] = (unsigned char)label[n]; } while (label[n++]);
            if (assign) assign((unsigned char *)meter+0x68, wide);
        }
    }
    end = sceKernelGetProcessTimeWide();
    throttle->last_refresh_us = end;
    g_vita_refinery_last_any_refresh_us = end;
    if (perf_logs < 24u &&
        (perf_logs < 12u || end - start >= 16000u)) {
        GUNBROS_PERF_LOG("[PERF-REFINERY] meter=%p refresh_us=%llu interval_ms=%u spacing_ms=%u\n",
                         meter, (unsigned long long)(end - start),
                         (unsigned int)
                             (refresh_interval_us / 1000u),
                         (unsigned int)
                             (VITA_REFINERY_REFRESH_SPACING_US / 1000u));
        perf_logs++;
    }
}

void gunbros_flush_offline_refinery(void) {
    if (g_vita_refinery_manager_ready) {
        (void)vita_refinery_write_sidecar(g_vita_refinery_manager);
    }
}

static uint32_t trace_refinement_get_interval_duration_ms(void *self,
                                                           int interval) {
    if ((unsigned int)interval < VITA_REFINERY_INTERVAL_COUNT) {
        return g_vita_refinery_duration_ms[interval];
    }

    return SO_CONTINUE(uint32_t, h_refinement_get_interval_duration_ms,
                       self, interval);
}

static float trace_refinement_get_interval_efficiency(void *self,
                                                        int interval) {
    if ((unsigned int)interval < VITA_REFINERY_INTERVAL_COUNT) {
        return g_vita_refinery_efficiency[interval];
    }

    return SO_CONTINUE(float, h_refinement_get_interval_efficiency,
                       self, interval);
}

static uint32_t trace_refinement_get_interval_purchase_cost(void *self,
                                                             int interval,
                                                             int currency) {
    if ((unsigned int)interval < VITA_REFINERY_INTERVAL_COUNT) {
        static const uint32_t rare[12] = {0,40,80,200,480,1040,0,0,0,0,0,0};
        return currency == 1 ? rare[interval] : 0u;
    }

    return SO_CONTINUE(uint32_t, h_refinement_get_interval_purchase_cost,
                       self, interval, currency);
}

static void *vita_refinery_live_manager(void) {
    unsigned char *gunbros = (unsigned char *)g_live_gunbros_self;
    void *manager = NULL;

    /* ELF verification: CGunBros::Init constructs CRefinementManager and
     * stores it at CGunBros+0x18 before any refinery menu can be shown. */
    if (gunbros_object_registry_is_valid(g_live_gunbros_self) &&
        trace_is_game_range(gunbros, 0x1cu)) {
        manager = *(void **)(void *)(gunbros + 0x18u);
    }
    if (trace_is_game_range(manager, VITA_REFINERY_MANAGER_SIZE)) {
        return manager;
    }
    if (g_vita_refinery_manager_ready &&
        trace_is_game_range(g_vita_refinery_manager,
                            VITA_REFINERY_MANAGER_SIZE)) {
        return g_vita_refinery_manager;
    }
    return NULL;
}

static int vita_refinery_meter_has_payload(unsigned int meter_index,
                                            uint32_t *state_out) {
    void *manager = vita_refinery_live_manager();
    const unsigned char *slot;
    uint32_t state;
    uint64_t amount;

    if (meter_index >= VITA_REFINERY_INTERVAL_COUNT || !manager) {
        if (state_out) {
            *state_out = 0u;
        }
        return 0;
    }
    slot = vita_refinery_slot(manager, meter_index);
    state = *(const uint32_t *)(const void *)slot;
    amount = *(const uint64_t *)(const void *)(slot + 0x18u);
    if (state_out) {
        *state_out = state;
    }
    return (state == 2u || state == 3u) && amount != 0u;
}

static void vita_refinery_apply_meter_state(void *meter,
                                             unsigned int meter_index) {
    typedef void (*refinery_meter_state_fn_t)(void *);
    static refinery_meter_state_fn_t set_inactive;
    static refinery_meter_state_fn_t set_active;
    static refinery_meter_state_fn_t set_complete;
    static int looked_up;
    void *movie;
    uint32_t state = 0u;
    int desired_chapter = 1;
    int current_chapter = -1;

    if (!trace_is_game_range(meter, 0x7cu) ||
        meter_index >= VITA_REFINERY_INTERVAL_COUNT) {
        return;
    }
    if (vita_refinery_meter_has_payload(meter_index, &state)) {
        desired_chapter = state == 3u ? 3 : 2;
    }

    if (state == 0u) return; /* Keep the native locked/purchase presentation. */
    movie = *(void **)(void *)((unsigned char *)meter + 0x60u);
    if (trace_is_game_range(movie, 0xafu)) {
        current_chapter = (int)*(const int8_t *)(const void *)
            ((const unsigned char *)movie + 0xaeu);
    }
    if (current_chapter == desired_chapter) {
        return;
    }

    if (!looked_up) {
        set_inactive = (refinery_meter_state_fn_t)(uintptr_t)
            so_symbol(&so_mod,
                      "_ZN18CMenuGameResources14CResourceMeter11SetInActiveEv");
        set_active = (refinery_meter_state_fn_t)(uintptr_t)
            so_symbol(&so_mod,
                      "_ZN18CMenuGameResources14CResourceMeter9SetActiveEv");
        set_complete = (refinery_meter_state_fn_t)(uintptr_t)
            so_symbol(&so_mod,
                      "_ZN18CMenuGameResources14CResourceMeter11SetCompleteEv");
        looked_up = 1;
    }

    if (desired_chapter == 3 && set_complete) {
        set_complete(meter);
    } else if (desired_chapter == 2 && set_active) {
        set_active(meter);
    } else if (set_inactive) {
        set_inactive(meter);
    }
}

static void vita_refinery_normalize_menu_meters(void *menu) {
    static unsigned int repair_logs;
    unsigned int phase;

    if (!trace_is_game_range(menu, 0x808u)) {
        return;
    }
    for (phase = 0; phase < 2u; ++phase) {
        unsigned char *phase_owner = (unsigned char *)menu + phase * 8u;
        unsigned int count = *(const uint32_t *)(const void *)
            ((const unsigned char *)menu + 0x18u + phase * 4u);
        unsigned int capacity = *(const uint32_t *)(const void *)
            (phase_owner + 0x7fcu);
        unsigned char *meters = *(unsigned char **)(void *)
            (phase_owner + 0x7f8u);
        unsigned int i;

        if (count > capacity) {
            count = capacity;
        }
        if (count > VITA_REFINERY_TIERS_PER_PHASE) {
            count = VITA_REFINERY_TIERS_PER_PHASE;
        }
        if (count == 0u ||
            !trace_is_game_range(meters, count * 0xf0u)) {
            continue;
        }
        for (i = 0; i < count; ++i) {
            unsigned char *meter = meters + i * 0xf0u;
            uint16_t *index = (uint16_t *)(void *)(meter + 4u);

            /* Native Bind assigns one cumulative index across both phase
             * vectors. Preserve it: assuming six visible entries per phase
             * made a filtered layout point at the wrong slot and duplicate
             * another meter's state/artwork. */
            if (*index >= VITA_REFINERY_INTERVAL_COUNT && repair_logs < 24u) {
                sceClibPrintf("[FIX-REFINERY] invalid native meter phase=%u item=%u index=%u\n",
                              phase, i, (unsigned int)*index);
                repair_logs++;
            }
            /* Bind caches the locked state here. Keep it consistent with
             * the persisted slot, including purchased Premium refineries. */
            if (*index < VITA_REFINERY_INTERVAL_COUNT) {
                void *manager = vita_refinery_live_manager();
                if (manager) meter[7u] = *(uint32_t *)(void *)vita_refinery_slot(manager, *index) == 0;
            }
            if (*index < VITA_REFINERY_INTERVAL_COUNT) {
                vita_refinery_apply_meter_state(meter, *index);
            }
        }
    }
}

static void trace_menu_game_resources_meters_enabled(void *self,
                                                      unsigned char enabled);

static void continue_menu_game_resources_on_show(void *self) {
    typedef void (*menu_on_show_fn_t)(void *);
    menu_on_show_fn_t original;

    kuKernelCpuUnrestrictedMemcpy((void *)h_menu_game_resources_on_show.addr,
                                  h_menu_game_resources_on_show.orig_instr,
                                  sizeof(h_menu_game_resources_on_show.orig_instr));
    kuKernelFlushCaches((void *)h_menu_game_resources_on_show.addr,
                        sizeof(h_menu_game_resources_on_show.orig_instr));

    original = (menu_on_show_fn_t)(uintptr_t)
        (h_menu_game_resources_on_show.thumb_addr ?
             h_menu_game_resources_on_show.thumb_addr :
             h_menu_game_resources_on_show.addr);
    original(self);

    kuKernelCpuUnrestrictedMemcpy((void *)h_menu_game_resources_on_show.addr,
                                  h_menu_game_resources_on_show.patch_instr,
                                  sizeof(h_menu_game_resources_on_show.patch_instr));
    kuKernelFlushCaches((void *)h_menu_game_resources_on_show.addr,
                        sizeof(h_menu_game_resources_on_show.patch_instr));
}

static void trace_menu_game_resources_on_show(void *self) {
    void *progress = NULL;
    void *config = NULL;
    void *manager;

    gunbros_perf_set_context(GUNBROS_PERF_SCENE_REFINERY, -1, -1,
                             "CMenuGameResources::OnShow");
    /* Load the resident, independently validated slot records before native
     * OnShow asks its already-bound meters for state, time, and artwork. */
    manager = vita_refinery_live_manager();
    if (manager) {
        vita_refinery_load_sidecar(manager);
        (void)vita_refinery_restore_resident(manager,
                                             "CMenuGameResources::OnShow");
        (void)vita_refinery_apply_progression(manager);
    }

    /* CMenuGameResources::Init stores ProfileData+0x2e0 directly at +0x28;
     * it bypasses CGame::GetPlayerData.  Capture that exact HUD owner before
     * the store can bind to a different fallback wallet. */
    if (trace_is_game_range(self, 0x2cu)) {
        progress = *(void **)(void *)((unsigned char *)self + 0x28u);
        if (trace_is_player_progress_for_brother(progress) &&
            (uintptr_t)progress >= 0x190u) {
            config = (void *)((uintptr_t)progress - 0x190u);
            (void)vita_offline_profile_register_native_owner(
                config, progress, "CMenuGameResources::OnShow");
        }
    }

    continue_menu_game_resources_on_show(self);
    vita_refinery_normalize_menu_meters(self);
    trace_menu_game_resources_meters_enabled(self, 1u);
    vita_refinery_request_meter_refresh();
}

static void trace_menu_game_resources_meters_enabled(void *self,
                                                      unsigned char enabled) {
    static unsigned int offline_override_logs;

    /* Refining is fully maintained by CRefinementSlot::Commit/Update using
     * local elapsed time.  Only this menu's legacy online presentation gate
     * must be opened; global Wi-Fi/CNGS stubs stay offline. */
    if (!enabled && offline_override_logs < 12u) {
        sceClibPrintf("[FIX-REFINERY] enabling local refinery meters (requested=%u, this=%p)\n",
                      (unsigned int)enabled, self);
        offline_override_logs++;
    }

    (void)SO_CONTINUE(int, h_menu_game_resources_meters_enabled,
                      self, (unsigned char)1);
}

static void continue_void_self_hook(so_hook *hook, void *self);

static void trace_refinery_meter_draw(void *meter, unsigned short x,
                                       unsigned short y) {
    typedef void (*refinery_meter_draw_fn_t)(void *, unsigned short,
                                              unsigned short);
    unsigned int meter_index = VITA_REFINERY_INTERVAL_COUNT;
    void *sprite = NULL;
    int hide_sprite = 0;

    if (trace_is_game_range(meter, 0x7cu)) {
        meter_index = *(const uint16_t *)(const void *)
            ((const unsigned char *)meter + 4u);
    }
    if (meter_index < VITA_REFINERY_INTERVAL_COUNT) {
        /* Draw() renders +0x78 unconditionally when it is non-null. Hide that
         * owned sprite only for this frame when this exact slot is empty;
         * ownership and the pointer are restored after the native draw. */
        vita_refinery_apply_meter_state(meter, meter_index);
        if (!vita_refinery_meter_has_payload(meter_index, NULL)) {
            sprite = *(void **)(void *)((unsigned char *)meter + 0x78u);
            *(void **)(void *)((unsigned char *)meter + 0x78u) = NULL;
            hide_sprite = 1;
        }
    }

    if (g_original_refinery_meter_draw) {
        ((refinery_meter_draw_fn_t)g_original_refinery_meter_draw)(meter,
                                                                   x, y);
    } else {
        (void)SO_CONTINUE(int, h_refinery_meter_draw, meter, x, y);
    }

    if (hide_sprite && trace_is_game_range(meter, 0x7cu)) {
        *(void **)(void *)((unsigned char *)meter + 0x78u) = sprite;
    }
}

static void trace_refinery_transfer_effect_draw(void *self) {
    typedef void (*refinery_transfer_draw_fn_t)(void *);
    void *particle = NULL;

    /* CTransferEffect::Draw spends most of this small overlay on a particle
     * player at +0x98. Temporarily hiding that owned pointer lets the native
     * function continue drawing its sprite, amount text, and completion
     * state without changing ownership, update timing, or refinery logic. */
    if (trace_is_game_range(self, 0x9cu)) {
        particle = *(void **)(void *)((unsigned char *)self + 0x98u);
        *(void **)(void *)((unsigned char *)self + 0x98u) = NULL;
    }

    if (g_original_refinery_transfer_effect_draw) {
        ((refinery_transfer_draw_fn_t)
             g_original_refinery_transfer_effect_draw)(self);
    } else {
        continue_void_self_hook(&h_refinery_transfer_effect_draw, self);
    }

    if (particle && trace_is_game_range(self, 0x9cu)) {
        *(void **)(void *)((unsigned char *)self + 0x98u) = particle;
    }
}

static int trace_menu_splash_is_loaded(void *self) {
    int log = trace_allow("CMenuSplash::IsLoaded");
    int ret;

    ret = SO_CONTINUE(int, h_menu_splash_is_loaded, self);

    if (ret) {
        if (!g_splash_loaded_seen) {
            sceClibPrintf("[PATCH-MENU] CMenuSplash::IsLoaded became true\n");
        }
        g_splash_loaded_seen = 1;
    }

    if (log) {
        sceClibPrintf("[TRACE-MENU] CMenuSplash::IsLoaded(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int gunbros_state(const void *self) {
    const unsigned char *base = (const unsigned char *)self;

    return self ? *(const int *)(const void *)(base + 0x134) : -1;
}

static unsigned int game_text_offset_or_invalid(const void *address) {
    uintptr_t value = (uintptr_t)address & ~(uintptr_t)1u;

    if (value < so_mod.text_base || value >= so_mod.text_base + so_mod.text_size) {
        return 0xffffffffu;
    }

    return (unsigned int)(value - so_mod.text_base);
}

static void continue_void_self_hook(so_hook *hook, void *self) {
    void (*original)(void *);

    kuKernelCpuUnrestrictedMemcpy((void *)hook->addr,
                                  hook->orig_instr,
                                  sizeof(hook->orig_instr));
    kuKernelFlushCaches((void *)hook->addr, sizeof(hook->orig_instr));

    original = (void (*)(void *))(hook->thumb_addr ? hook->thumb_addr : hook->addr);
    original(self);

    kuKernelCpuUnrestrictedMemcpy((void *)hook->addr,
                                  hook->patch_instr,
                                  sizeof(hook->patch_instr));
    kuKernelFlushCaches((void *)hook->addr, sizeof(hook->patch_instr));
}

static void trace_gunbros_reinit(void *self) {
    static int count;
    void *caller = __builtin_return_address(0);
    unsigned int caller_offset = game_text_offset_or_invalid(caller);
    int state_before = gunbros_state(self);

    count++;
    if (count <= 24 || (count % 120) == 0) {
        sceClibPrintf("[TRACE-STATE] enter CGunBros::ReInit#%d(this=%p) state=%d caller=%p game+0x%08x\n",
                      count, self, state_before, caller, caller_offset);
    }

    continue_void_self_hook(&h_gunbros_reinit, self);
    (void)store_offline_purchases_query_shim();

    if (count <= 24 || (count % 120) == 0) {
        sceClibPrintf("[TRACE-STATE] leave CGunBros::ReInit#%d(this=%p) state=%d->%d\n",
                      count, self, state_before, gunbros_state(self));
    }
}

static void trace_gunbros_reinitialize_all(void *self) {
    static int count;
    int state_before = gunbros_state(self);

    count++;
    if (count <= 24 || (count % 120) == 0) {
        sceClibPrintf("[TRACE-STATE] enter CGunBros::ReInitializeAll#%d(this=%p) state=%d\n",
                      count, self, state_before);
    }

    continue_void_self_hook(&h_gunbros_reinitialize_all, self);
    (void)store_offline_purchases_query_shim();

    if (count <= 24 || (count % 120) == 0) {
        sceClibPrintf("[TRACE-STATE] leave CGunBros::ReInitializeAll#%d(this=%p) state=%d->%d\n",
                      count, self, state_before, gunbros_state(self));
    }
}

static void continue_gunbros_show_main_menu(void *self, int screen);

static void gunbros_request_main_menu_kick(void) {
    if (g_main_menu_kick_done || g_main_menu_kick_pending) {
        return;
    }

    g_main_menu_kick_pending = 1;
    g_main_menu_kick_delay = GUNBROS_MAIN_MENU_KICK_DELAY_UPDATES;
    sceClibPrintf("[PATCH-MENU] state-2 loader drained; schedule ShowMainMenu(screen=%d) in %d updates (splash_loaded=%d)\n",
                  GUNBROS_MAIN_MENU_SCREEN,
                  GUNBROS_MAIN_MENU_KICK_DELAY_UPDATES,
                  g_splash_loaded_seen);
}

static void gunbros_maybe_show_main_menu(void *self, const char *source) {
    int state;

    if (!g_main_menu_kick_pending || g_main_menu_kick_done || !h_gunbros_show_main_menu.addr) {
        return;
    }

    state = gunbros_state(self);
    if (state != GUNBROS_STATE_LOADING_SPLASH) {
        sceClibPrintf("[PATCH-MENU] %s: cancel pending ShowMainMenu(screen=%d); state=%d\n",
                      source ? source : "update", GUNBROS_MAIN_MENU_SCREEN, state);
        g_main_menu_kick_pending = 0;
        return;
    }

    if (g_main_menu_kick_delay > 0) {
        g_main_menu_kick_delay--;
        return;
    }

    sceClibPrintf("[PATCH-MENU] %s: post-bind handoff; ShowMainMenu(screen=%d)\n",
                  source ? source : "update", GUNBROS_MAIN_MENU_SCREEN);
    continue_gunbros_show_main_menu(self, GUNBROS_MAIN_MENU_SCREEN);
    g_main_menu_kick_done = 1;
    g_main_menu_kick_pending = 0;
    sceClibPrintf("[PATCH-MENU] ShowMainMenu(screen=%d) complete state=%d\n",
                  GUNBROS_MAIN_MENU_SCREEN, gunbros_state(self));
}

static void continue_gunbros_bind(void *self) {
    void (*original)(void *);

    kuKernelCpuUnrestrictedMemcpy((void *)h_gunbros_bind.addr,
                                  h_gunbros_bind.orig_instr,
                                  sizeof(h_gunbros_bind.orig_instr));
    kuKernelFlushCaches((void *)h_gunbros_bind.addr,
                        sizeof(h_gunbros_bind.orig_instr));

    original = (void (*)(void *))(h_gunbros_bind.thumb_addr
                                      ? h_gunbros_bind.thumb_addr
                                      : h_gunbros_bind.addr);
    original(self);

    kuKernelCpuUnrestrictedMemcpy((void *)h_gunbros_bind.addr,
                                  h_gunbros_bind.patch_instr,
                                  sizeof(h_gunbros_bind.patch_instr));
    kuKernelFlushCaches((void *)h_gunbros_bind.addr,
                        sizeof(h_gunbros_bind.patch_instr));
}

static void continue_gunbros_show_main_menu(void *self, int screen) {
    void (*original)(void *, int);

    if (g_original_gunbros_show_main_menu) {
        ((void (*)(void *, int))g_original_gunbros_show_main_menu)(self,
                                                                   screen);
        return;
    }
    kuKernelCpuUnrestrictedMemcpy((void *)h_gunbros_show_main_menu.addr,
                                  h_gunbros_show_main_menu.orig_instr,
                                  sizeof(h_gunbros_show_main_menu.orig_instr));
    kuKernelFlushCaches((void *)h_gunbros_show_main_menu.addr,
                        sizeof(h_gunbros_show_main_menu.orig_instr));

    original = (void (*)(void *, int))(h_gunbros_show_main_menu.thumb_addr
                                           ? h_gunbros_show_main_menu.thumb_addr
                                           : h_gunbros_show_main_menu.addr);
    original(self, screen);

    kuKernelCpuUnrestrictedMemcpy((void *)h_gunbros_show_main_menu.addr,
                                  h_gunbros_show_main_menu.patch_instr,
                                  sizeof(h_gunbros_show_main_menu.patch_instr));
    kuKernelFlushCaches((void *)h_gunbros_show_main_menu.addr,
                        sizeof(h_gunbros_show_main_menu.patch_instr));
}

static void trace_gunbros_bind(void *self) {
    int log = trace_allow("CGunBros::Bind");
    int state_before = gunbros_state(self);
    int state_after;

    remember_live_gunbros_self(self, "CGunBros::Bind/enter");

    if (log) {
        sceClibPrintf("[TRACE-LOAD] enter CGunBros::Bind(this=%p)\n", self);
    }

    /* CGunBros::Bind() is void. Its ARM epilogue does not define R0, so the
     * old int wrapper was gating the main-menu kick on an arbitrary leftover
     * return register. Preserve the real ABI and arm the transition from the
     * verified state instead. */
    continue_gunbros_bind(self);
    remember_live_gunbros_self(self, "CGunBros::Bind/leave");
    (void)store_offline_purchases_query_shim();
    state_after = gunbros_state(self);

    if (state_before == GUNBROS_STATE_LOADING_SPLASH &&
        state_after == GUNBROS_STATE_LOADING_SPLASH &&
        h_gunbros_show_main_menu.addr) {
        gunbros_request_main_menu_kick();
    }

    if (log) {
        sceClibPrintf("[TRACE-LOAD] leave CGunBros::Bind(this=%p) state=%d->%d splash_loaded=%d kick_pending=%d\n",
                      self, state_before, state_after, g_splash_loaded_seen,
                      g_main_menu_kick_pending);
    }
}

static void trace_gunbros_load_menus(void *self) {
    int log = trace_allow("CGunBros::LoadMenus");

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CGunBros::LoadMenus(this=%p)\n", self);
    }

    vita_offline_profile_begin_bulk_transition("CGunBros::LoadMenus");
    GUNBROS_PERF_MENU_BEGIN(GUNBROS_STAGE_LOAD_MENUS, perf_begin);
    continue_void_self_hook(&h_gunbros_load_menus, self);
    GUNBROS_PERF_END(GUNBROS_STAGE_LOAD_MENUS, perf_begin, 0);
    vita_offline_profile_end_bulk_transition("CGunBros::LoadMenus");

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CGunBros::LoadMenus(this=%p)\n", self);
    }
}

static void trace_gunbros_load_mission(void *self) {
    int log = trace_allow("CGunBros::LoadMission");

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CGunBros::LoadMission(this=%p)\n",
                      self);
    }

    vita_offline_profile_begin_bulk_transition("CGunBros::LoadMission");
    GUNBROS_PERF_MENU_BEGIN(GUNBROS_STAGE_LOAD_MISSION, perf_begin);
    continue_void_self_hook(&h_gunbros_load_mission, self);
    GUNBROS_PERF_END(GUNBROS_STAGE_LOAD_MISSION, perf_begin, 0);
    vita_offline_profile_end_bulk_transition("CGunBros::LoadMission");

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CGunBros::LoadMission(this=%p)\n",
                      self);
    }
}

static int trace_gunbros_update(void *self, int dt) {
    typedef int (*gunbros_update_fn_t)(void *, int);
    int count = 0;
    int log = trace_allow_count("CGunBros::Update", 30, 120, &count);
    int ret;
    int state_entry = gunbros_state(self);
    int state_after_original;

    ensure_gti2_keepalive_offline_guards("CGunBros::Update/enter");
    remember_live_gunbros_self(self, "CGunBros::Update/enter");
    /* Sidecars were read before bootstrap. As soon as CGunBros has finished
     * constructing its ProfileData and mission owners, hydrate them once here
     * so later shop/level access is an in-memory pointer lookup. */
    vita_offline_profile_prepare_live_owner("CGunBros::Update/startup");

    if (log) {
        sceClibPrintf("[TRACE-TICK] enter CGunBros::Update#%d(this=%p, dt=%d)\n",
                      count, self, dt);
    }

    if (g_original_gunbros_update) {
        ret = ((gunbros_update_fn_t)g_original_gunbros_update)(self, dt);
    } else {
        ret = SO_CONTINUE(int, h_gunbros_update, self, dt);
    }
    state_after_original = gunbros_state(self);

    remember_live_gunbros_self(self, "CGunBros::Update/leave");
    vita_offline_profile_tick(dt);
    vita_asset_preload_tick();

    gunbros_maybe_show_main_menu(self, "CGunBros::Update");

    if (log) {
        sceClibPrintf("[TRACE-TICK] leave CGunBros::Update#%d(this=%p, dt=%d) -> %d state=%d->%d->%d splash_loaded=%d kick_pending=%d kick_done=%d\n",
                      count, self, dt, ret, state_entry, state_after_original,
                      gunbros_state(self),
                      g_splash_loaded_seen, g_main_menu_kick_pending,
                      g_main_menu_kick_done);
    }

    return ret;
}


static void trace_gunbros_show_main_menu(void *self, int screen) {
    int log = trace_allow("CGunBros::ShowMainMenu");

    remember_live_gunbros_self(self, "CGunBros::ShowMainMenu");
    vita_asset_preload_restore_for_menus("CGunBros::ShowMainMenu");
    gunbros_perf_set_context(GUNBROS_PERF_SCENE_MENU, screen, -1,
                             "CGunBros::ShowMainMenu");

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CGunBros::ShowMainMenu(this=%p, screen=%d)\n",
                      self, screen);
    }

    continue_gunbros_show_main_menu(self, screen);

    if (screen == GUNBROS_MAIN_MENU_SCREEN) {
        g_main_menu_kick_done = 1;
        g_main_menu_kick_pending = 0;
    }

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CGunBros::ShowMainMenu(this=%p, screen=%d) state=%d\n",
                      self, screen, gunbros_state(self));
    }
}

static int trace_gunbros_set_menu(void *self, int screen) {
    int log = trace_allow("CGunBros::SetMenu");
    int ret;

    remember_live_gunbros_self(self, "CGunBros::SetMenu");
    gunbros_perf_set_context(GUNBROS_PERF_SCENE_MENU, screen, -1,
                             "CGunBros::SetMenu");

    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CGunBros::SetMenu(this=%p, screen=%d)\n",
                      self, screen);
    }

    GUNBROS_PERF_MENU_BEGIN(GUNBROS_STAGE_SET_MENU, perf_begin);
    ret = SO_CONTINUE(int, h_gunbros_set_menu, self, screen);
    GUNBROS_PERF_END(GUNBROS_STAGE_SET_MENU, perf_begin, 0);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CGunBros::SetMenu(this=%p, screen=%d) -> %d\n",
                      self, screen, ret);
    }

    return ret;
}

typedef struct ResourceLoaderTraceSnapshot {
    int queued;
    int processed;
    void *node;
    unsigned int node_type;
    void *callback;
    unsigned int callback_offset;
    unsigned int media_pack_index;
    unsigned int media_logical_id;
    void **media_output_slot;
} ResourceLoaderTraceSnapshot;

static ResourceLoaderTraceSnapshot resource_loader_trace_snapshot(void *self) {
    ResourceLoaderTraceSnapshot snapshot = {
        0, 0, NULL, 0xffu, NULL, 0xffffffffu, 0, 0, NULL
    };
    const unsigned char *loader = (const unsigned char *)self;
    const unsigned char *node;

    if (!trace_is_game_range(self, 0x10u)) {
        return snapshot;
    }

    snapshot.queued = *(const unsigned short *)(const void *)(loader + 0x0c);
    snapshot.processed = *(const unsigned short *)(const void *)(loader + 0x0e);
    snapshot.node = *(void * const *)(const void *)(loader + 0x08);
    node = (const unsigned char *)snapshot.node;
    if (!trace_is_game_range(node, 0x14u)) {
        return snapshot;
    }

    snapshot.node_type = node[0x06];
    if (snapshot.node_type == 3u) {
        snapshot.callback = *(void * const *)(const void *)(node + 0x08);
        snapshot.callback_offset = game_text_offset_or_invalid(snapshot.callback);
    } else if (snapshot.node_type == 4u) {
        /* CResourceLoader::AddMedia stores pack index at +4, the logical
         * resource ID at +8, and the caller's output slot at +0xc. */
        snapshot.media_pack_index =
            *(const unsigned short *)(const void *)(node + 0x04);
        snapshot.media_logical_id =
            *(const unsigned int *)(const void *)(node + 0x08);
        snapshot.media_output_slot =
            *(void ***)(void *)(node + 0x0c);
    }

    return snapshot;
}

static void register_loaded_big_media(const ResourceLoaderTraceSnapshot *before) {
    if (!before || before->node_type != 4u || !before->media_output_slot ||
        !trace_is_game_range(before->media_output_slot, sizeof(void *))) {
        return;
    }
    void *media = *before->media_output_slot;
    if (trace_is_game_ptr(media)) {
        gunbros_audio_register_big_media(media, before->media_pack_index,
                                         before->media_logical_id);
    }
}

static void trace_resource_load_next(void *self) {
    typedef void (*resource_load_next_fn_t)(void *);
#ifndef GUNBROS_QUIET_LOGS
    int count = 0;
    int log = 0;
#endif
    ResourceLoaderTraceSnapshot before;
#if !defined(GUNBROS_QUIET_LOGS) || defined(GUNBROS_ENABLE_PERF_TRACE)
    ResourceLoaderTraceSnapshot after;
#endif
#ifdef GUNBROS_ENABLE_PERF_TRACE
    uint64_t begin_us;
    uint64_t end_us;
#endif

    GUNBROS_PERF_COUNT(resource_load_next_calls);
    vita_asset_preload_note_loader(self);
#ifndef GUNBROS_QUIET_LOGS
    log = trace_allow_count("CResourceLoader::LoadNext", 20, 240, &count);
#endif

    before = resource_loader_trace_snapshot(self);
#ifdef GUNBROS_ENABLE_PERF_TRACE
    begin_us = sceKernelGetProcessTimeWide();
#endif

#ifndef GUNBROS_QUIET_LOGS
    if (log) {
        sceClibPrintf("[TRACE-LOAD] enter CResourceLoader::LoadNext#%d(this=%p) queue=%d/%d remaining=%d node=%p type=%u callback=%p game+0x%08x\n",
                      count, self, before.processed, before.queued,
                      before.queued - before.processed, before.node,
                      before.node_type, before.callback, before.callback_offset);
    }

    /* CResourceLoader::LoadNext() is void. The old int wrapper printed an
     * undefined R0 value as if it were a load result, hiding the actual queue
     * state used by CGunBros::UpdateLoading(). */
#endif

    if (before.processed < before.queued)
    GUNBROS_PERF_LOG("[PERF-REQUEST] loader=%p queue=%d/%d node_type=%u pack=%u logical=0x%08x callback=0x%08x\n",
        self, before.processed, before.queued, before.node_type,
        before.media_pack_index, before.media_logical_id, before.callback_offset);
    GUNBROS_PERF_BEGIN(resource_begin);
    if (g_original_resource_load_next) {
        ((resource_load_next_fn_t)g_original_resource_load_next)(self);
    } else {
        continue_void_self_hook(&h_resource_load_next, self);
    }
    GUNBROS_PERF_END(GUNBROS_STAGE_RESOURCE, resource_begin, 0);
    register_loaded_big_media(&before);
#if !defined(GUNBROS_QUIET_LOGS) || defined(GUNBROS_ENABLE_PERF_TRACE)
    after = resource_loader_trace_snapshot(self);
#endif
#ifdef GUNBROS_ENABLE_PERF_TRACE
    end_us = sceKernelGetProcessTimeWide();
#endif

#ifndef GUNBROS_QUIET_LOGS
    if (log) {
        sceClibPrintf("[TRACE-LOAD] leave CResourceLoader::LoadNext#%d(this=%p) queue=%d/%d remaining=%d node=%p type=%u callback=%p game+0x%08x advanced=%d\n",
                      count, self, after.processed, after.queued,
                      after.queued - after.processed, after.node,
                      after.node_type, after.callback, after.callback_offset,
                       after.processed != before.processed || after.node != before.node);
    }
#endif

#ifdef GUNBROS_ENABLE_PERF_TRACE
    if (end_us >= begin_us && end_us - begin_us >= 20000u) {
        GUNBROS_PERF_LOG("[PERF-LOAD] kind=resource elapsed_us=%llu queue=%u/%u->%u/%u node_type=%u pack=%u logical=0x%08x callback=0x%08x\n",
                         (unsigned long long)(end_us - begin_us),
                         (unsigned int)before.processed,
                         (unsigned int)before.queued,
                         (unsigned int)after.processed,
                         (unsigned int)after.queued,
                         before.node_type, before.media_pack_index,
                         before.media_logical_id, before.callback_offset);
    }
#endif
}

static int trace_menu_stack_load_menu(void *self) {
    int log = trace_allow("CMenuStack::LoadMenu");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CMenuStack::LoadMenu(this=%p)\n", self);
    }

    GUNBROS_PERF_MENU_BEGIN(GUNBROS_STAGE_STACK_LOAD, perf_begin);
    ret = SO_CONTINUE(int, h_menu_stack_load_menu, self);
    GUNBROS_PERF_END(GUNBROS_STAGE_STACK_LOAD, perf_begin, 0);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CMenuStack::LoadMenu(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

#define TRACE_SELF_LOADER(fn, hookvar, label_text) \
static int fn(void *self, void *loader) { \
    int log = trace_allow(label_text); \
    int ret; \
    if (log) { \
        sceClibPrintf("[TRACE-ASSET] enter " label_text "(this=%p, loader=%p)\n", self, loader); \
    } \
    ret = SO_CONTINUE(int, hookvar, self, loader); \
    if (log) { \
        sceClibPrintf("[TRACE-ASSET] leave " label_text "(this=%p, loader=%p) -> %d\n", self, loader, ret); \
    } \
    return ret; \
}

static int trace_menu_splash_load(void *self, void *loader) {
    int log = trace_allow("CMenuSplash::Load");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CMenuSplash::Load(this=%p, loader=%p)\n", self, loader);
    }

    g_splash_loaded_seen = 0;

    ret = SO_CONTINUE(int, h_menu_splash_load, self, loader);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CMenuSplash::Load(this=%p, loader=%p) -> %d\n",
                      self, loader, ret);
    }

    return ret;
}

TRACE_SELF_LOADER(trace_menu_greeting_load, h_menu_greeting_load, "CMenuGreeting::Load")
TRACE_SELF_LOADER(trace_menu_system_load, h_menu_system_load, "CMenuSystem::Load")
TRACE_SELF_LOADER(trace_movie_load, h_movie_load, "CMovie::Load")
TRACE_SELF_LOADER(trace_movie_sprite_load, h_movie_sprite_load, "CMovieSprite::Load")
TRACE_SELF_LOADER(trace_movie_sound_set_load, h_movie_sound_set_load, "CMovieSoundSet::Load")
TRACE_SELF_LOADER(trace_movie_tiled_sprite_load, h_movie_tiled_sprite_load, "CMovieTiledSprite::Load")

static int trace_menu_system_set_menu(void *self, int screen, unsigned short arg, int branch) {
    if (classic_menu_block(screen, arg)) return 0;
    int log = trace_allow("CMenuSystem::SetMenu");
    int ret;

    gunbros_perf_set_context(GUNBROS_PERF_SCENE_MENU, screen, branch,
                             "CMenuSystem::SetMenu");
    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuSystem::SetMenu(this=%p, screen=%d, arg=%u, branch=%d)\n",
                      self, screen, (unsigned int)arg, branch);
    }

    GUNBROS_PERF_MENU_BEGIN(GUNBROS_STAGE_SET_MENU, perf_begin);
    ret = SO_CONTINUE(int, h_menu_system_set_menu, self, screen, arg, branch);
    GUNBROS_PERF_END(GUNBROS_STAGE_SET_MENU, perf_begin, 0);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuSystem::SetMenu(this=%p, screen=%d, arg=%u, branch=%d) -> %d\n",
                      self, screen, (unsigned int)arg, branch, ret);
    }

    return ret;
}

static int trace_menu_system_push_menu(void *self, int screen, unsigned short arg, int branch) {
    if (classic_menu_block(screen, arg)) return 0;
    int log = trace_allow("CMenuSystem::PushMenu");
    int ret;

    gunbros_perf_set_context(GUNBROS_PERF_SCENE_MENU, screen, branch,
                             "CMenuSystem::PushMenu");
    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuSystem::PushMenu(this=%p, screen=%d, arg=%u, branch=%d)\n",
                      self, screen, (unsigned int)arg, branch);
    }

    GUNBROS_PERF_MENU_BEGIN(GUNBROS_STAGE_PUSH_MENU, perf_begin);
    ret = SO_CONTINUE(int, h_menu_system_push_menu, self, screen, arg, branch);
    GUNBROS_PERF_END(GUNBROS_STAGE_PUSH_MENU, perf_begin, 0);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuSystem::PushMenu(this=%p, screen=%d, arg=%u, branch=%d) -> %d\n",
                      self, screen, (unsigned int)arg, branch, ret);
    }

    return ret;
}

static int trace_menu_system_pop_menu(void *self, int branch) {
    int log = trace_allow("CMenuSystem::PopMenu");
    int ret;

    gunbros_perf_set_context(GUNBROS_PERF_SCENE_MENU, -1, branch,
                             "CMenuSystem::PopMenu");
    if (log) {
        sceClibPrintf("[TRACE-MENU] enter CMenuSystem::PopMenu(this=%p, branch=%d)\n", self, branch);
    }

    GUNBROS_PERF_MENU_BEGIN(GUNBROS_STAGE_POP_MENU, perf_begin);
    ret = SO_CONTINUE(int, h_menu_system_pop_menu, self, branch);
    GUNBROS_PERF_END(GUNBROS_STAGE_POP_MENU, perf_begin, 0);

    if (log) {
        sceClibPrintf("[TRACE-MENU] leave CMenuSystem::PopMenu(this=%p, branch=%d) -> %d\n",
                      self, branch, ret);
    }

    return ret;
}

static int trace_res_pack_toc_init(void *self) {
    int log = trace_allow("CResPackTOC::Init");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CResPackTOC::Init(this=%p)\n", self);
    }

    ret = SO_CONTINUE(int, h_res_pack_toc_init, self);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CResPackTOC::Init(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

#ifndef GUNBROS_QUIET_LOGS
static uint32_t resource_table_digest(const void *data, size_t size) {
    const unsigned char *bytes = (const unsigned char *)data;
    uint32_t hash = 2166136261u;
    size_t i;

    for (i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static void verify_res_pack_toc_bind(void *self, unsigned short pack_idx, int bind_result) {
    const unsigned char *base = (const unsigned char *)self;
    const void *entries = NULL;
    unsigned int count = 0;
    unsigned int expected_count = 0;
    unsigned int expected_digest = 0;
    unsigned int actual_digest = 0;
    unsigned int bound = 0;
    const char *pack_name = NULL;
    int expected;
    int table_valid;

    if (trace_is_game_range(self, 0x40u)) {
        entries = *(const void * const *)(const void *)(base + 0x34u);
        count = *(const unsigned int *)(const void *)(base + 0x38u);
        bound = base[0x3cu];
    }
    table_valid = bind_result != 0 && bound != 0 &&
                  count > 0u && count <= 0x10000u &&
                  trace_is_game_range(entries, (size_t)count * 8u);
    expected = gunbros_get_pack_toc_expectation(pack_idx,
                                                &expected_count,
                                                &expected_digest,
                                                &pack_name);
    if (table_valid) {
        actual_digest = resource_table_digest(entries, (size_t)count * 8u);
    }

    if (table_valid && expected &&
        count == expected_count && actual_digest == expected_digest) {
        sceClibPrintf("[VERIFY-TOC][OK] pack=%u name=%s entries=%u digest=%08x bound=%u\n",
                      (unsigned int)pack_idx,
                      pack_name ? pack_name : "?",
                      count, actual_digest, bound);
        return;
    }

    sceClibPrintf("[VERIFY-TOC][FAIL] pack=%u name=%s ret=%d bound=%u table=%p count=%u/%u digest=%08x/%08x expected=%d\n",
                  (unsigned int)pack_idx,
                  pack_name ? pack_name : "?",
                  bind_result, bound, entries, count, expected_count,
                  actual_digest, expected_digest, expected);
}

#endif /* !GUNBROS_QUIET_LOGS: verification only reports diagnostics. */

static int trace_res_pack_toc_bind(void *self, unsigned short pack_idx) {
    int log = trace_allow("CResPackTOC::Bind");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CResPackTOC::Bind(this=%p, pack=%u)\n",
                      self, (unsigned int)pack_idx);
    }

    GUNBROS_PERF_BEGIN(pack_begin);
    ret = SO_CONTINUE(int, h_res_pack_toc_bind, self, pack_idx);
    GUNBROS_PERF_END(GUNBROS_STAGE_PACK_BIND, pack_begin, 0);
#ifndef GUNBROS_QUIET_LOGS
    verify_res_pack_toc_bind(self, pack_idx, ret);
#endif

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CResPackTOC::Bind(this=%p, pack=%u) -> %d\n",
                      self, (unsigned int)pack_idx, ret);
    }

    return ret;
}

static int trace_res_toc_manager_init(void *self) {
    int log = trace_allow("CResTOCManager::Init");
    int ret;

    if (trace_is_game_range(self, 0x228u)) {
        g_live_res_toc_manager = self;
    }
    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CResTOCManager::Init(this=%p)\n", self);
    }

    ret = SO_CONTINUE(int, h_res_toc_manager_init, self);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CResTOCManager::Init(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_res_toc_manager_bind(void *self) {
    int log = trace_allow("CResTOCManager::Bind");
    int ret;

    if (trace_is_game_range(self, 0x228u)) {
        g_live_res_toc_manager = self;
    }
    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CResTOCManager::Bind(this=%p)\n", self);
    }

    ret = SO_CONTINUE(int, h_res_toc_manager_bind, self);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CResTOCManager::Bind(this=%p) -> %d\n", self, ret);
    }

    return ret;
}

static int trace_res_toc_manager_set_pack_hash(void *self, unsigned int pack_hash) {
    int log = trace_allow("CResTOCManager::SetTargetResPackHash");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CResTOCManager::SetTargetResPackHash(this=%p, hash=0x%08x)\n",
                      self, pack_hash);
    }

    ret = SO_CONTINUE(int, h_res_toc_manager_set_pack_hash, self, pack_hash);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CResTOCManager::SetTargetResPackHash(this=%p, hash=0x%08x) -> %d\n",
                      self, pack_hash, ret);
    }

    return ret;
}

static int trace_res_toc_manager_set_pack_str(void *self, const char *pack_name) {
    int log = trace_allow("CResTOCManager::SetTargetResPackStr");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CResTOCManager::SetTargetResPackStr(this=%p, str=%p '%s')\n",
                      self, pack_name, trace_cstr(pack_name));
    }

    ret = SO_CONTINUE(int, h_res_toc_manager_set_pack_str, self, pack_name);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CResTOCManager::SetTargetResPackStr(this=%p, str=%p '%s') -> %d\n",
                      self, pack_name, trace_cstr(pack_name), ret);
    }

    return ret;
}

static int trace_res_toc_manager_set_pack_internal(void *self, unsigned short pack_idx) {
    int log = trace_allow("CResTOCManager::SetTargetResPackInternal");
    int ret;

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CResTOCManager::SetTargetResPackInternal(this=%p, pack=%u)\n",
                      self, (unsigned int)pack_idx);
    }

    ret = SO_CONTINUE(int, h_res_toc_manager_set_pack_internal, self, pack_idx);

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CResTOCManager::SetTargetResPackInternal(this=%p, pack=%u) -> %d\n",
                      self, (unsigned int)pack_idx, ret);
    }

    return ret;
}

enum {
    GUNBROS_CORE_PACK_INDEX = 12u,
    /* BigFile logical IDs.  BigViewer's loose names are one entry lower
     * because the two aggregate/localization records precede this range. */
    GUNBROS_CORE_IMAGE_PERCY_TORSO = 0x139u,  /* resource58.png */
    GUNBROS_CORE_IMAGE_PANTS = 0x13au,        /* resource59.png */
    GUNBROS_CORE_IMAGE_CIGAR = 0x140u,        /* resource65.png */
    GUNBROS_CORE_IMAGE_FRANCIS_TORSO = 0x141u, /* resource66.png */
    /* CMenuSystem::Load queues 254 mixed resources in the supplied build,
     * with more image keys learned lazily from store/refinery screens.  The
     * table itself stores only keys and surface pointers (not image data), so
     * leave enough headroom to retain the complete observed menu working set.
     * Decoded surfaces remain owned by the native CImagePool. */
    VITA_ASSET_PRELOAD_MAX = 1024u,
    VITA_ASSET_PHASE_MENUS = 0,
    VITA_ASSET_PHASE_GAMEPLAY = 1,
    VITA_ASSET_PHASE_RESTORING = 2,
};

typedef struct VitaAssetPreloadEntry {
    void *pool;
    void *surface;
    int format;
    int image_id;
    unsigned short pack_idx;
    unsigned short target_pack;
    unsigned char arg3;
    int arg4;
    unsigned char arg5;
    unsigned char arg6;
    uint64_t slow_load_us;
    unsigned int load_count;
    unsigned char pending;
    unsigned char pinned;
} VitaAssetPreloadEntry;

static VitaAssetPreloadEntry
    g_vita_asset_preloads[VITA_ASSET_PRELOAD_MAX];
static unsigned int g_vita_asset_preload_count;
static unsigned int g_vita_asset_preload_pending_count;
static unsigned int g_vita_asset_preload_scan_cursor;
static void *g_vita_asset_resource_loader;
static int g_vita_asset_preload_phase = VITA_ASSET_PHASE_MENUS;
static int g_vita_asset_preload_in_operation;
static int g_vita_asset_preload_nested_load;

typedef void *(*vita_image_pool_get_fn_t)(void *, int, int, unsigned short,
                                          unsigned char, int,
                                          unsigned char, unsigned char);
typedef int (*vita_image_pool_remove_fn_t)(void *, void *, unsigned short);

static vita_image_pool_get_fn_t g_vita_image_pool_get;
static vita_image_pool_remove_fn_t g_vita_image_pool_remove;
static int g_vita_image_pool_symbols_looked_up;

static void cache_core_default_texture(unsigned int target_pack,
                                       int image_id, void *surface);

static void vita_asset_preload_resolve_symbols(void) {
    if (g_vita_image_pool_symbols_looked_up) {
        return;
    }
    g_vita_image_pool_get = (vita_image_pool_get_fn_t)(uintptr_t)
        so_symbol(&so_mod, "_ZN10CImagePool8GetImageE11ImageFormatithihh");
    g_vita_image_pool_remove = (vita_image_pool_remove_fn_t)(uintptr_t)
        so_symbol(&so_mod,
                  "_ZN10CImagePool6RemoveEPN3com3glu8platform8graphics15ICRenderSurfaceEt");
    g_vita_image_pool_symbols_looked_up = 1;
}

static int vita_asset_loader_is_valid(const void *loader) {
    const unsigned char *base = (const unsigned char *)loader;
    const void *pools;
    unsigned int count;

    if (!trace_is_game_range(loader, 0x18u)) {
        return 0;
    }
    pools = *(void * const *)(const void *)(base + 0x10u);
    count = *(const unsigned int *)(const void *)(base + 0x14u);
    return count > 0u && count <= 0x100u &&
           trace_is_game_range(pools, (size_t)count * 0x18u);
}

static void vita_asset_preload_note_loader(void *loader) {
    if (vita_asset_loader_is_valid(loader)) {
        g_vita_asset_resource_loader = loader;
    }
}

static void *vita_asset_preload_live_pool(const VitaAssetPreloadEntry *entry) {
    const unsigned char *loader;
    unsigned char *pool;
    unsigned int count;

    if (!entry) {
        return NULL;
    }
    if (vita_asset_loader_is_valid(g_vita_asset_resource_loader)) {
        loader = (const unsigned char *)g_vita_asset_resource_loader;
        count = *(const unsigned int *)(const void *)(loader + 0x14u);
        if ((unsigned int)entry->target_pack < count) {
            pool = *(unsigned char **)(void *)(loader + 0x10u) +
                   (size_t)entry->target_pack * 0x18u;
            if (trace_is_game_range(pool, 0x18u) &&
                *(const unsigned short *)(const void *)(pool + 0x14u) ==
                    entry->target_pack) {
                return pool;
            }
        }
    }
    if (trace_is_game_range(entry->pool, 0x18u) &&
        *(const unsigned short *)(const void *)
            ((const unsigned char *)entry->pool + 0x14u) ==
                entry->target_pack) {
        return entry->pool;
    }
    return NULL;
}

static int vita_asset_preload_matches(const VitaAssetPreloadEntry *entry,
                                      void *pool, int format, int image_id,
                                      unsigned short pack_idx,
                                      unsigned int target_pack,
                                      unsigned char arg3, int arg4,
                                      unsigned char arg5,
                                      unsigned char arg6) {
    (void)pool;
    return entry && entry->format == format &&
           entry->image_id == image_id && entry->pack_idx == pack_idx &&
           entry->target_pack == target_pack && entry->arg3 == arg3 &&
           entry->arg4 == arg4 && entry->arg5 == arg5 &&
           entry->arg6 == arg6;
}

static void vita_asset_preload_record(void *pool, int format, int image_id,
                                      unsigned short pack_idx,
                                      unsigned int target_pack,
                                      unsigned char arg3, int arg4,
                                      unsigned char arg5,
                                      unsigned char arg6, void *surface,
                                      uint64_t elapsed_us) {
    VitaAssetPreloadEntry *entry = NULL;
    unsigned int i;
    int was_pinned = 0;

    if (g_vita_asset_preload_in_operation) {
        g_vita_asset_preload_nested_load = 1;
        return;
    }

    if (g_vita_asset_preload_phase != VITA_ASSET_PHASE_MENUS ||
        target_pack == 0xffffu || !surface) {
        return;
    }
    for (i = 0; i < g_vita_asset_preload_count; ++i) {
        if (vita_asset_preload_matches(&g_vita_asset_preloads[i],
                                       pool, format, image_id, pack_idx,
                                       target_pack, arg3, arg4, arg5, arg6)) {
            entry = &g_vita_asset_preloads[i];
            break;
        }
    }
    if (!entry) {
        if (g_vita_asset_preload_count >= VITA_ASSET_PRELOAD_MAX) {
            return;
        }
        entry = &g_vita_asset_preloads[g_vita_asset_preload_count++];
        memset(entry, 0, sizeof(*entry));
        entry->pool = pool;
        entry->format = format;
        entry->image_id = image_id;
        entry->pack_idx = pack_idx;
        entry->target_pack = (unsigned short)target_pack;
        entry->arg3 = arg3;
        entry->arg4 = arg4;
        entry->arg5 = arg5;
        entry->arg6 = arg6;
        if (g_vita_asset_preload_count <= 16u ||
            (g_vita_asset_preload_count % 64u) == 0u) {
            GUNBROS_PERF_LOG("[PERF-PRELOAD] kind=menu-image event=learned catalog=%u/%u target_pack=%u image=%d logical=%u load_us=%llu\n",
                             g_vita_asset_preload_count,
                             (unsigned int)VITA_ASSET_PRELOAD_MAX,
                             target_pack, image_id,
                             (unsigned int)image_id & 0x7fffu,
                             (unsigned long long)elapsed_us);
        }
    }

    was_pinned = entry->pinned != 0u;
    entry->pool = pool;
    entry->surface = surface;
    entry->slow_load_us = elapsed_us;
    entry->load_count++;
    if (entry->load_count > 1u) {
        GUNBROS_PERF_LOG("[PERF-RELOAD] pack=%u image=%d count=%u load_us=%llu pinned_before=%d\n",
            target_pack, image_id, entry->load_count,
            (unsigned long long)elapsed_us, was_pinned);
    }
    if (!entry->pending) {
        g_vita_asset_preload_pending_count++;
    }
    entry->pending = 1u;
    entry->pinned = 0u;
    if (was_pinned) {

        GUNBROS_PERF_LOG("[PERF-PRELOAD] kind=menu-image event=cache-lost target_pack=%u image=%d loads=%u load_us=%llu\n",
                         target_pack, image_id, entry->load_count,
                         (unsigned long long)elapsed_us);
    }
}

static void vita_asset_preload_tick(void) {
    VitaAssetPreloadEntry *entry = NULL;
    void *pool = NULL;
#ifdef GUNBROS_ENABLE_PERF_TRACE
    uint64_t begin_us;
    uint64_t end_us;
#endif
    void *surface;
    unsigned int i;
#ifdef GUNBROS_ENABLE_PERF_TRACE
    static unsigned int pin_logs;
    static uint64_t last_residency_report;
    uint64_t now = sceKernelGetProcessTimeWide();
    if (g_vita_asset_preload_count && now - last_residency_report >= 2000000u) {
        unsigned int pinned = 0u, reloads = 0u;
        for (unsigned int j = 0; j < g_vita_asset_preload_count; ++j) {
            pinned += g_vita_asset_preloads[j].pinned != 0u;
            if (g_vita_asset_preloads[j].load_count > 1u)
                reloads += g_vita_asset_preloads[j].load_count - 1u;
        }
        GUNBROS_PERF_LOG("[PERF-RESIDENCY] phase=%u catalog=%u pinned=%u pending=%u repeat_loads=%u catalog_bytes=%u\n",
            g_vita_asset_preload_phase, g_vita_asset_preload_count, pinned,
            g_vita_asset_preload_pending_count, reloads,
            (unsigned int)sizeof(g_vita_asset_preloads));
        last_residency_report = now;
    }
#endif

    if (g_vita_asset_preload_phase != VITA_ASSET_PHASE_MENUS ||
        g_vita_asset_preload_in_operation ||
        g_vita_asset_preload_pending_count == 0u ||
        g_vita_asset_preload_count == 0u) {
        return;
    }
    for (i = 0; i < g_vita_asset_preload_count; ++i) {
        unsigned int index =
            (g_vita_asset_preload_scan_cursor + i) %
                g_vita_asset_preload_count;
        if (g_vita_asset_preloads[index].pending) {
            pool = vita_asset_preload_live_pool(
                &g_vita_asset_preloads[index]);
            if (pool) {
                entry = &g_vita_asset_preloads[index];
                g_vita_asset_preload_scan_cursor =
                    (index + 1u) % g_vita_asset_preload_count;
                break;
            }
        }
    }
    if (!entry) {
        return;
    }
    vita_asset_preload_resolve_symbols();
    if (!g_vita_image_pool_get) {
        entry->pending = 0u;
        if (g_vita_asset_preload_pending_count != 0u) {
            g_vita_asset_preload_pending_count--;
        }
        GUNBROS_PERF_LOG("[PERF-PRELOAD] kind=menu-image event=unavailable symbol=GetImage\n");
        return;
    }

#ifdef GUNBROS_ENABLE_PERF_TRACE
    begin_us = sceKernelGetProcessTimeWide();
#endif
    g_vita_asset_preload_nested_load = 0;
    g_vita_asset_preload_in_operation = 1;
    surface = g_vita_image_pool_get(pool, entry->format, entry->image_id,
                                    entry->pack_idx, entry->arg3, entry->arg4,
                                    entry->arg5, entry->arg6);
    g_vita_asset_preload_in_operation = 0;
#ifdef GUNBROS_ENABLE_PERF_TRACE
    end_us = sceKernelGetProcessTimeWide();
#endif

    entry->pending = 0u;
    if (g_vita_asset_preload_pending_count != 0u) {
        g_vita_asset_preload_pending_count--;
    }
    entry->pinned = surface ? 1u : 0u;
    entry->surface = surface;
    entry->pool = pool;
    cache_core_default_texture(entry->target_pack, entry->image_id, surface);
#ifdef GUNBROS_ENABLE_PERF_TRACE
    pin_logs++;
    if (!surface || pin_logs <= 16u || (pin_logs % 64u) == 0u) {
        GUNBROS_PERF_LOG("[PERF-PRELOAD] kind=menu-image event=%s mode=%s catalog=%u target_pack=%u image=%d logical=%u pin_us=%llu surface=%p\n",
                         surface ? "pinned" : "failed",
                         g_vita_asset_preload_nested_load ? "reload" : "cache-hit",
                         g_vita_asset_preload_count,
                         (unsigned int)entry->target_pack, entry->image_id,
                         (unsigned int)entry->image_id & 0x7fffu,
                         (unsigned long long)(end_us >= begin_us ?
                             end_us - begin_us : 0u), surface);
    }
#endif
}

static void vita_asset_preload_enter_gameplay(void *loader,
                                               const char *source) {
#ifdef GUNBROS_ENABLE_PERF_TRACE
    uint64_t begin_us;
    uint64_t end_us;
#endif
    unsigned int released = 0u;
    unsigned int missing = 0u;
    unsigned int i;

    vita_asset_preload_note_loader(loader);
    if (g_vita_asset_preload_phase == VITA_ASSET_PHASE_GAMEPLAY) {
        return;
    }
    vita_asset_preload_resolve_symbols();
#ifdef GUNBROS_ENABLE_PERF_TRACE
    begin_us = sceKernelGetProcessTimeWide();
#endif
    g_vita_asset_preload_phase = VITA_ASSET_PHASE_GAMEPLAY;
    for (i = 0; i < g_vita_asset_preload_count; ++i) {
        VitaAssetPreloadEntry *entry = &g_vita_asset_preloads[i];
        void *pool;

        entry->pending = 0u;
        if (!entry->pinned || !entry->surface) {
            entry->pinned = 0u;
            entry->surface = NULL;
            continue;
        }
        pool = vita_asset_preload_live_pool(entry);

        if (g_vita_image_pool_remove && pool &&
            g_vita_image_pool_remove(pool, entry->surface, 0xffffu)) {
            released++;
        } else {

            missing++;
        }
        entry->pinned = 0u;
        entry->surface = NULL;
    }
    g_vita_asset_preload_pending_count = 0u;
    g_vita_asset_preload_scan_cursor = 0u;
    memset(&g_core_default_textures, 0, sizeof(g_core_default_textures));
#ifdef GUNBROS_ENABLE_PERF_TRACE
    end_us = sceKernelGetProcessTimeWide();
    GUNBROS_PERF_LOG("[PERF-PRELOAD] kind=menu-image event=game-enter source=%s catalog=%u released=%u missing=%u elapsed_us=%llu\n",
                     source ? source : "?", g_vita_asset_preload_count,
                     released, missing,
                     (unsigned long long)(end_us >= begin_us ?
                         end_us - begin_us : 0u));
#else
    (void)source;
#endif
}

static void vita_asset_preload_restore_for_menus(const char *source) {
    unsigned int i;

    if (g_vita_asset_preload_phase != VITA_ASSET_PHASE_GAMEPLAY) {
        return;
    }


    for (i = 0; i < g_vita_asset_preload_count; ++i) {
        VitaAssetPreloadEntry *entry = &g_vita_asset_preloads[i];
        entry->pending = 0u;
        entry->pinned = 0u;
        entry->surface = NULL;
    }
    g_vita_asset_preload_pending_count = 0u;
    g_vita_asset_preload_scan_cursor = 0u;
    g_vita_asset_preload_in_operation = 0;
    g_vita_asset_preload_phase = VITA_ASSET_PHASE_MENUS;
    memset(&g_core_default_textures, 0, sizeof(g_core_default_textures));
    GUNBROS_PERF_LOG("[PERF-PRELOAD] kind=menu-image event=menu-demand-residency source=%s catalog=%u\n",
                     source ? source : "?", g_vita_asset_preload_count);
}

static void cache_core_default_texture(unsigned int target_pack,
                                       int image_id, void *surface) {
    unsigned int logical_id = (unsigned int)image_id & 0x7fffu;
    void **slot = NULL;
    const char *role = NULL;

    if (target_pack != GUNBROS_CORE_PACK_INDEX ||
        !trace_is_game_range(surface, 0x24u)) {
        return;
    }

    switch (logical_id) {
        case GUNBROS_CORE_IMAGE_PERCY_TORSO:
            slot = &g_core_default_textures.percy_torso;
            role = "percy-torso";
            break;
        case GUNBROS_CORE_IMAGE_PANTS:
            slot = &g_core_default_textures.pants;
            role = "pants";
            break;
        case GUNBROS_CORE_IMAGE_CIGAR:
            slot = &g_core_default_textures.cigar;
            role = "cigar";
            break;
        case GUNBROS_CORE_IMAGE_FRANCIS_TORSO:
            slot = &g_core_default_textures.francis_torso;
            role = "francis-torso";
            break;
        default:
            return;
    }

    if (*slot != surface) {
        *slot = surface;
        sceClibPrintf("[FIX-TEXTURE] cached core default role=%s logical=0x%03x encoded=0x%08x surface=%p\n",
                      role, logical_id, (unsigned int)image_id, surface);
    }
}

static void *trace_image_pool_load_image(void *self, int format, int image_id,
                                         unsigned short pack_idx, unsigned char arg3,
                                         int arg4, unsigned char arg5, unsigned char arg6) {
    typedef void *(*image_pool_load_fn_t)(void *, int, int, unsigned short,
                                          unsigned char, int,
                                          unsigned char, unsigned char);
    int log = trace_allow("CImagePool::LoadImage");
    void *ret;
    unsigned int target_pack = 0xffffu;
#ifdef GUNBROS_ENABLE_PERF_TRACE
    uint64_t begin_us;
    uint64_t end_us;
#endif
    uint64_t elapsed_us = 0u;

    GUNBROS_PERF_COUNT(image_load_calls);

    if (g_vita_asset_preload_in_operation) {
        g_vita_asset_preload_nested_load = 1;
    }


    if (trace_is_game_range(self, 0x16u)) {
        target_pack = *(const unsigned short *)(const void *)
            ((const unsigned char *)self + 0x14);
    }

    if (log) {
        sceClibPrintf("[TRACE-ASSET] enter CImagePool::LoadImage(this=%p, fmt=%d, image=%d, pack=%u, target_pack=%u, a3=%u, a4=%d, a5=%u, a6=%u)\n",
                      self, format, image_id, (unsigned int)pack_idx,
                      target_pack,
                      (unsigned int)arg3, arg4, (unsigned int)arg5, (unsigned int)arg6);
    }

#ifdef GUNBROS_ENABLE_PERF_TRACE
    begin_us = sceKernelGetProcessTimeWide();
#endif
    GUNBROS_PERF_BEGIN(image_begin);
    if (g_original_image_pool_load_image) {
        ret = ((image_pool_load_fn_t)g_original_image_pool_load_image)(
            self, format, image_id, pack_idx, arg3, arg4, arg5, arg6);
    } else {
        ret = SO_CONTINUE(void *, h_image_pool_load_image, self, format,
                          image_id, pack_idx, arg3, arg4, arg5, arg6);
    }
    GUNBROS_PERF_END(GUNBROS_STAGE_IMAGE, image_begin, 0);
#ifdef GUNBROS_ENABLE_PERF_TRACE
    end_us = sceKernelGetProcessTimeWide();
    elapsed_us = end_us >= begin_us ? end_us - begin_us : 0u;
#endif

    cache_core_default_texture(target_pack, image_id, ret);

    if (!ret && trace_allow_ex("CSpritePlayer::texture-load-fail", 24, 180)) {
        sceClibPrintf("[FIX-GFX] CImagePool::LoadImage failed pool=%p fmt=%d image=%d pack=%u target_pack=%u args=(%u,%d,%u,%u)\n",
                      self, format, image_id, (unsigned int)pack_idx,
                      target_pack,
                      (unsigned int)arg3, arg4, (unsigned int)arg5, (unsigned int)arg6);
    }

    if (log) {
        sceClibPrintf("[TRACE-ASSET] leave CImagePool::LoadImage(this=%p, fmt=%d, image=%d, pack=%u) -> %p\n",
                      self, format, image_id, (unsigned int)pack_idx, ret);
    }

    vita_asset_preload_record(self, format, image_id, pack_idx,
                              target_pack, arg3, arg4, arg5, arg6,
                              ret, elapsed_us);
#ifdef GUNBROS_ENABLE_PERF_TRACE
    if (elapsed_us >= 20000u) {
        GUNBROS_PERF_LOG("[PERF-LOAD] kind=image elapsed_us=%llu pool=%p fmt=%d image=%d pack=%u target_pack=%u result=%p\n",
                         (unsigned long long)elapsed_us, self,
                         format, image_id, (unsigned int)pack_idx,
                         target_pack, ret);
    }
#endif

    return ret;
}

#ifdef GUNBROS_ENABLE_PERF_TRACE
/* Keep post-queue binding visible: the supplied trace stalled after LoadNext
 * returned, so resource timing alone cannot account for the whole transition. */
static so_hook h_perf_mission_bind, h_perf_mission_info_bind, h_perf_list_bind;
static uintptr_t g_perf_mission_bind, g_perf_mission_info_bind, g_perf_list_bind;
#define DEFINE_MENU_BIND_PROFILE(fn, hook, original, label) \
static void fn(void *self) { \
    GUNBROS_PERF_LOG("[PERF-BIND-BEGIN] menu=" label " self=%p\n", self); \
    gunbros_perf_log_flush(); \
    GUNBROS_PERF_BEGIN(begin); \
    if (original) ((void (*)(void *))original)(self); \
    else (void)SO_CONTINUE(int, hook, self); \
    uint64_t elapsed = gunbros_perf_now() - begin; \
    GUNBROS_PERF_END(GUNBROS_STAGE_MENU_BIND, begin, 0); \
    GUNBROS_PERF_LOG("[PERF-BIND-END] menu=" label " elapsed_us=%llu\n", (unsigned long long)elapsed); \
    gunbros_perf_log_flush(); \
}
DEFINE_MENU_BIND_PROFILE(perf_mission_bind, h_perf_mission_bind, g_perf_mission_bind, "mission")
DEFINE_MENU_BIND_PROFILE(perf_mission_info_bind, h_perf_mission_info_bind, g_perf_mission_info_bind, "mission-info")
DEFINE_MENU_BIND_PROFILE(perf_list_bind, h_perf_list_bind, g_perf_list_bind, "list")
#undef DEFINE_MENU_BIND_PROFILE
#endif
