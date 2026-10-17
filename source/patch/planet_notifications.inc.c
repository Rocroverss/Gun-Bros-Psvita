/* v3.1.4 PlanetCallback uses type 13 and tracker UserData at tracker+8.
 * Keep native availability, selection acknowledgement and red sprite rendering;
 * persist only the tiny planet seen-state missing from offline profile saves. */
#include "utils/planet_notifications.h"

static so_hook h_planet_callback, h_planet_seen;
static uintptr_t g_original_planet_callback;
static VitaPlanetNotices g_planet_notices;
static int g_planet_notices_loaded, g_planet_notices_baselined;
static int g_planet_notices_dirty;
static void *g_planet_notice_user_data;
typedef int (*planet_unflatten_fn)(const void *, int, uint16_t, uint16_t *, uint8_t *);
typedef int (*planet_query_fn)(const void *, int, uint16_t, uint8_t);
typedef void (*planet_seen_fn)(void *, int, uint16_t, uint8_t, uint8_t);
static planet_unflatten_fn g_planet_unflatten;
static planet_query_fn g_planet_available, g_planet_is_seen;
static planet_seen_fn g_planet_set_seen;

static void vita_planet_notices_load(void) {
    if (g_planet_notices_loaded) return;
    g_planet_notices_loaded = 1;
    VitaPlanetNotices disk;
    FILE *f = fopen(DATA_PATH "vita_planet_notices_v1.dat", "rb");
    if (!f) f = fopen(DATA_PATH "vita_planet_notices_v1.tmp", "rb");
    if (f) {
        size_t n = fread(&disk, 1, sizeof(disk), f);
        int extra = fgetc(f);
        fclose(f);
        if (n == sizeof(disk) && extra == EOF && vita_planet_notices_valid(&disk)) {
            g_planet_notices = disk;
            g_planet_notices_baselined = 1;
            return;
        }
    }
    vita_planet_notices_init(&g_planet_notices);
}

static void vita_planet_notices_save(void) {
    if (!g_planet_notices_dirty) return;
    g_planet_notices.checksum = vita_planet_notices_checksum(&g_planet_notices);
    FILE *f = fopen(DATA_PATH "vita_planet_notices_v1.tmp", "wb");
    if (!f) return;
    size_t n = fwrite(&g_planet_notices, 1, sizeof(g_planet_notices), f);
    int flushed = fflush(f);
    int closed = fclose(f);
    if (n != sizeof(g_planet_notices) || flushed || closed) return;
    if (rename(DATA_PATH "vita_planet_notices_v1.tmp",
               DATA_PATH "vita_planet_notices_v1.dat") != 0) {
        /* Vita's stdio backend may not replace an existing destination. Keep
         * the complete temporary file recoverable if the second rename fails. */
        remove(DATA_PATH "vita_planet_notices_v1.dat");
        if (rename(DATA_PATH "vita_planet_notices_v1.tmp",
                   DATA_PATH "vita_planet_notices_v1.dat") != 0) return;
    }
    g_planet_notices_dirty = 0;
}

static void trace_planet_object_seen(void *user, int type, uint16_t pack,
                                     uint8_t index, uint8_t seen) {
    (void)SO_CONTINUE(int, h_planet_seen, user, type, pack, index, seen);
    if (type != 13 || user != g_planet_notice_user_data || !seen) return;
    VitaPlanetNotice *notice = vita_planet_notice_find(&g_planet_notices, pack, index);
    if (notice && !notice->seen && g_planet_available(user, type, pack, index) == 1 &&
        g_planet_is_seen(user, type, pack, index) == 1) {
        notice->seen = 1;
        g_planet_notices_dirty = 1;
        vita_planet_notices_save();
    }
}

static void vita_planet_notices_prepare(void *menu, int selected) {
    if (!g_planet_unflatten || !g_planet_available || !g_planet_is_seen ||
        !g_planet_set_seen || !trace_is_game_range(menu, 0x25u) ||
        !trace_is_game_range(g_live_gunbros_self, 0x54u)) return;
    const unsigned char *m = menu;
    /* Bind refreshes its movie before setting this native ready flag. */
    if (!m[0x24]) return;
    unsigned count = *(const unsigned *)(const void *)(m + 0x20);
    const uint16_t *list = *(const uint16_t *const *)(const void *)(m + 0x1c);
    if (!count || count > VITA_PLANET_NOTICE_MAX ||
        !trace_is_game_range(list, count * sizeof(*list))) return;
    void *tracker = *(void **)((unsigned char *)g_live_gunbros_self + 0x50);
    if (!trace_is_game_range(tracker, 0x24u)) return;
    unsigned char *user = (unsigned char *)tracker + 8;
    unsigned packs = *(const unsigned *)(const void *)(user + 0x18);
    const void *data = *(void **)(void *)(user + 0x14);
    if (!packs || packs > 64 || !trace_is_game_range(data, packs * 0x50u)) return;
    vita_planet_notices_load();
    g_planet_notice_user_data = user;

    /* Resolve the complete list before establishing the one-time baseline.
     * Partial initialization must not hide a later unlock as old content. */
    uint16_t pack[VITA_PLANET_NOTICE_MAX];
    uint8_t index[VITA_PLANET_NOTICE_MAX];
    for (unsigned i = 0; i < count; ++i) {
        if (!g_planet_unflatten(g_live_gunbros_self, 13, list[i], &pack[i], &index[i]) ||
            pack[i] >= packs) return;
    }
    for (unsigned i = 0; i < count; ++i) {
        int available = g_planet_available(user, 13, pack[i], index[i]);
        g_planet_notices_dirty |= vita_planet_notice_add(&g_planet_notices,
            pack[i], index[i], available == 1, !g_planet_notices_baselined);
    }
    g_planet_notices_baselined = 1;
    if (selected >= 0 && (unsigned)selected < count) {
        VitaPlanetNotice *p = vita_planet_notice_find(&g_planet_notices,
            pack[selected], index[selected]);
        if (p && p->seen && g_planet_available(user, 13, p->pack, p->index) == 1 &&
            !g_planet_is_seen(user, 13, p->pack, p->index))
            g_planet_set_seen(user, 13, p->pack, p->index, 1);
    }
    vita_planet_notices_save();
}

static void trace_planet_callback(void *menu, int index, const void *rect) {
    vita_planet_notices_prepare(menu, index);
    if (g_original_planet_callback)
        ((void (*)(void *, int, const void *))g_original_planet_callback)(menu, index, rect);
    else
        (void)SO_CONTINUE(int, h_planet_callback, menu, index, rect);
}
