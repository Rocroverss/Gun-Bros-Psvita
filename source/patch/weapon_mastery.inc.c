/* Offline weapon XP/upgrade persistence. Keep the native collection alive and
 * retain its vtable, profile owner and server bookkeeping. */
#include "utils/weapon_mastery.h"

static so_hook h_mastery_upgrade, h_mastery_get_xp;
static so_hook h_mastery_load_file, h_mastery_load_stream;
static uintptr_t g_original_mastery_upgrade, g_original_mastery_get_xp;
static VitaMasteryDisk g_mastery_disk;
static int g_mastery_staged, g_mastery_valid, g_mastery_dirty;
static void *g_mastery_hydrated;

static void *vita_mastery_local_owner(void) {
    static uintptr_t vtable;
    if (!gunbros_object_registry_is_valid(g_live_gunbros_self)) return NULL;
    void *owner = *(void **)((unsigned char *)g_live_gunbros_self + 0x20);
    if (!vtable) vtable = so_symbol(&so_mod, "_ZTV14CWeaponMastery");
    if (!vtable || !trace_is_game_range(owner, VITA_MASTERY_OBJECT_SIZE) ||
        *(uintptr_t *)owner != vtable + 8) return NULL;
    return owner;
}

static int vita_mastery_read(const char *path, VitaMasteryDisk *disk) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(disk, 1, sizeof(*disk), f);
    int extra = fgetc(f);
    fclose(f);
    return n == sizeof(*disk) && extra == EOF && vita_mastery_valid(disk);
}

static void vita_weapon_mastery_prepare(void *owner) {
    if (!owner || owner != vita_mastery_local_owner()) return;
    if (!g_mastery_staged) {
        g_mastery_staged = 1;
        /* A complete pending commit is newer than the final file. Incomplete
         * writes are rejected and fall back to the previous valid save. */
        g_mastery_valid = vita_mastery_read(DATA_PATH "vita_weapon_mastery_v1.tmp", &g_mastery_disk) ||
                         vita_mastery_read(DATA_PATH "vita_weapon_mastery_v1.dat", &g_mastery_disk) ||
                         vita_mastery_read(DATA_PATH "vita_weapon_mastery_v1.bak", &g_mastery_disk);
    }
    if (g_mastery_hydrated == owner) return;
    if (g_mastery_valid && !vita_mastery_restore(owner, &g_mastery_disk)) return;
    g_mastery_hydrated = owner;
}

static int vita_weapon_mastery_save(void *owner) {
    VitaMasteryDisk disk;
    if (!owner || owner != vita_mastery_local_owner()) return 0;
    vita_weapon_mastery_prepare(owner);
    if (g_mastery_hydrated != owner || !vita_mastery_capture(&disk, owner)) return 0;
    if (!g_mastery_valid || memcmp(&disk, &g_mastery_disk, sizeof(disk))) {
        g_mastery_disk = disk;
        g_mastery_valid = g_mastery_dirty = 1;
    }
    if (!g_mastery_dirty) return 1;
    const char *temp = DATA_PATH "vita_weapon_mastery_v1.tmp";
    const char *final = DATA_PATH "vita_weapon_mastery_v1.dat";
    const char *backup = DATA_PATH "vita_weapon_mastery_v1.bak";
    FILE *f = fopen(temp, "wb");
    if (!f) return 0;
    size_t n = fwrite(&g_mastery_disk, 1, sizeof(g_mastery_disk), f);
    int flushed = fflush(f);
    int closed = fclose(f);
    if (n != sizeof(g_mastery_disk) || flushed || closed) return 0;
    if (rename(temp, final)) {
        /* Some Vita stdio backends cannot replace an existing file. Preserve
         * the previous save until the complete new file is in place. */
        remove(backup);
        if (rename(final, backup)) return 0;
        if (rename(temp, final)) {
            (void)rename(backup, final);
            return 0;
        }
    }
    g_mastery_dirty = 0;
    return 1;
}

static unsigned int trace_mastery_get_xp(void *self, unsigned short pack,
                                        unsigned char type, unsigned char index) {
    vita_weapon_mastery_prepare(self);
    if (g_original_mastery_get_xp)
        return ((unsigned int (*)(void *, unsigned short, unsigned char, unsigned char))
                g_original_mastery_get_xp)(self, pack, type, index);
    return SO_CONTINUE(unsigned int, h_mastery_get_xp, self, pack, type, index);
}

static unsigned int trace_mastery_upgrade(void *self, void *gun,
    unsigned short pack, unsigned char type, unsigned char index, unsigned int reason) {
    vita_weapon_mastery_prepare(self);
    unsigned int result;
    if (g_original_mastery_upgrade)
        result = ((unsigned int (*)(void *, void *, unsigned short, unsigned char,
                   unsigned char, unsigned int))g_original_mastery_upgrade)(
                       self, gun, pack, type, index, reason);
    else
        result = SO_CONTINUE(unsigned int, h_mastery_upgrade, self, gun, pack, type, index, reason);
    if (self == vita_mastery_local_owner()) {
        (void)vita_weapon_mastery_save(self);
        /* BuyAction deducts the price before Upgrade. Commit that wallet and
         * any player XP awarded by XPChanged without waiting for a UI tick. */
        vita_offline_profile_request_save();
        (void)vita_offline_profile_save(1);
    }
    return result;
}

static int trace_mastery_load_file(void *self, void *loader) {
    int result = SO_CONTINUE(int, h_mastery_load_file, self, loader);
    if (self == vita_mastery_local_owner()) g_mastery_hydrated = NULL;
    vita_weapon_mastery_prepare(self);
    return result;
}

static int trace_mastery_load_stream(void *self, void *stream, int version) {
    int result = SO_CONTINUE(int, h_mastery_load_stream, self, stream, version);
    if (self == vita_mastery_local_owner()) g_mastery_hydrated = NULL;
    vita_weapon_mastery_prepare(self);
    return result;
}
