#ifndef GUNBROS_WEAPON_MASTERY_H
#define GUNBROS_WEAPON_MASTERY_H

#include <stdint.h>
#include <string.h>

/* Verified against CWeaponMastery and CCollection<WeaponMasteryWeapon,128>:
 * object+0/+4 are native pointers; +8 has 128 twelve-byte entries;
 * +0x608 is the count. Entries contain pack/index/type, sync flags, and XP.
 * Persist only identities and XP, never pointers, padding, or server flags. */
enum { VITA_MASTERY_MAX = 128, VITA_MASTERY_OBJECT_SIZE = 0x610 };
typedef struct VitaMasteryEntry {
    uint16_t pack;
    uint8_t index, type;
    uint32_t xp;
} VitaMasteryEntry;
typedef struct VitaMasteryDisk {
    uint32_t magic, version, count, checksum;
    VitaMasteryEntry entries[VITA_MASTERY_MAX];
} VitaMasteryDisk;

static uint32_t vita_mastery_checksum(const VitaMasteryDisk *disk) {
    VitaMasteryDisk copy = *disk;
    copy.checksum = 0;
    const unsigned char *p = (const unsigned char *)&copy;
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < sizeof(copy); ++i) hash = (hash ^ p[i]) * 16777619u;
    return hash;
}

static int vita_mastery_valid(const VitaMasteryDisk *disk) {
    if (disk->magic != 0x57564247u || disk->version != 1 ||
        disk->count > VITA_MASTERY_MAX || disk->checksum != vita_mastery_checksum(disk)) return 0;
    for (unsigned i = 0; i < disk->count; ++i) {
        if (disk->entries[i].type != 6) return 0;
        for (unsigned j = 0; j < i; ++j)
            if (!memcmp(&disk->entries[i], &disk->entries[j], 4)) return 0;
    }
    return 1;
}

static int vita_mastery_capture(VitaMasteryDisk *disk, const void *object) {
    const unsigned char *p = object;
    uint32_t count;
    memcpy(&count, p + 0x608, 4);
    if (count > VITA_MASTERY_MAX) return 0;
    memset(disk, 0, sizeof(*disk));
    disk->magic = 0x57564247u;
    disk->version = 1;
    disk->count = count;
    for (unsigned i = 0; i < count; ++i) {
        memcpy(&disk->entries[i], p + 8 + i * 12, 4);
        memcpy(&disk->entries[i].xp, p + 16 + i * 12, 4);
    }
    disk->checksum = vita_mastery_checksum(disk);
    return vita_mastery_valid(disk);
}

/* Merge instead of replacing so existing native progress is never downgraded.
 * Transactional in memory: reject capacity/validation failures before mutation. */
static int vita_mastery_restore(void *object, const VitaMasteryDisk *disk) {
    VitaMasteryDisk current;
    if (!vita_mastery_valid(disk) || !vita_mastery_capture(&current, object)) return 0;
    for (unsigned i = 0; i < disk->count; ++i) {
        unsigned j;
        for (j = 0; j < current.count; ++j)
            if (!memcmp(&disk->entries[i], &current.entries[j], 4)) break;
        if (j == current.count) {
            if (current.count == VITA_MASTERY_MAX) return 0;
            current.entries[current.count++] = disk->entries[i];
        } else if (current.entries[j].xp < disk->entries[i].xp) {
            current.entries[j].xp = disk->entries[i].xp;
        }
    }
    unsigned char *p = object;
    uint32_t old_count;
    memcpy(&old_count, p + 0x608, 4);
    for (unsigned i = 0; i < current.count; ++i) {
        uint32_t old_xp = 0;
        if (i < old_count) memcpy(&old_xp, p + 16 + i * 12, 4);
        else memset(p + 8 + i * 12, 0, 12);
        memcpy(p + 8 + i * 12, &current.entries[i], 4);
        memcpy(p + 16 + i * 12, &current.entries[i].xp, 4);
        if (i >= old_count || old_xp != current.entries[i].xp) p[12 + i * 12] = 0;
    }
    memcpy(p + 0x608, &current.count, 4);
    return 1;
}
#endif
