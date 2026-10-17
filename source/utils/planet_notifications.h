#ifndef GUNBROS_PLANET_NOTIFICATIONS_H
#define GUNBROS_PLANET_NOTIFICATIONS_H

#include <stdint.h>
#include <string.h>

enum { VITA_PLANET_NOTICE_MAX = 64, VITA_PLANET_NOTICE_MAGIC = 0x4e504247 };
typedef struct VitaPlanetNotice {
    uint16_t pack;
    uint8_t index;
    uint8_t seen;
} VitaPlanetNotice;

typedef struct VitaPlanetNotices {
    uint32_t magic, version, count, checksum;
    VitaPlanetNotice planets[VITA_PLANET_NOTICE_MAX];
} VitaPlanetNotices;

static inline uint32_t vita_planet_notices_checksum(const VitaPlanetNotices *s) {
    const unsigned char *bytes = (const unsigned char *)s;
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < sizeof(*s); ++i) {
        /* Checksum itself occupies bytes 12..15. */
        hash ^= (i >= 12 && i < 16) ? 0 : bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static inline int vita_planet_notices_valid(const VitaPlanetNotices *s) {
    if (s->magic != VITA_PLANET_NOTICE_MAGIC || s->version != 1 ||
        s->count > VITA_PLANET_NOTICE_MAX ||
        s->checksum != vita_planet_notices_checksum(s)) return 0;
    for (unsigned i = 0; i < s->count; ++i) {
        if (s->planets[i].seen > 1) return 0;
        for (unsigned j = 0; j < i; ++j) {
            if (s->planets[j].pack == s->planets[i].pack &&
                s->planets[j].index == s->planets[i].index) return 0;
        }
    }
    return 1;
}

static inline void vita_planet_notices_init(VitaPlanetNotices *s) {
    memset(s, 0, sizeof(*s));
    s->magic = VITA_PLANET_NOTICE_MAGIC;
    s->version = 1;
}

static inline VitaPlanetNotice *vita_planet_notice_find(VitaPlanetNotices *s,
                                                       uint16_t pack, uint8_t index) {
    for (unsigned i = 0; i < s->count; ++i) {
        if (s->planets[i].pack == pack && s->planets[i].index == index)
            return &s->planets[i];
    }
    return NULL;
}

/* Only migration treats currently available content as already seen. Locked
 * planets remain unseen, so future availability creates a native red '!'. */
static inline int vita_planet_notice_add(VitaPlanetNotices *s, uint16_t pack,
                                         uint8_t index, int available, int migration) {
    if (vita_planet_notice_find(s, pack, index) || s->count >= VITA_PLANET_NOTICE_MAX)
        return 0;
    VitaPlanetNotice *p = &s->planets[s->count++];
    p->pack = pack;
    p->index = index;
    p->seen = migration && available;
    return 1;
}
#endif
