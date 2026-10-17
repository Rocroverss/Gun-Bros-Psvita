#include "audio.h"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>

#include <soloud.h>
#include <soloud_wavstream.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr unsigned int kDefaultPcmRate = 22050;
constexpr unsigned int kDefaultPcmChannels = 2;
constexpr unsigned int kDefaultPcmBits = 16;
constexpr uint32_t kDecodedPcmMimeKey = 0xfd94b3c1u;
constexpr size_t kMaxBigAudioMappings = 512;
constexpr size_t kMaxLoadedBigMedia = 512;
constexpr size_t kMaxBigAudioMisses = 512;
constexpr long kMaxBigAudioOggBytes = 4 * 1024 * 1024;
constexpr uint64_t kFnv64OffsetBasis = 0xcbf29ce484222325ULL;
constexpr uint64_t kFnv64Prime = 0x100000001b3ULL;

/* Audio diagnostics use the same opt-in policy as every other runtime log. */
#ifdef GUNBROS_QUIET_LOGS
#define BIG_AUDIO_LOG(...) ((void)0)
#define PCM_AUDIO_LOG(...) ((void)0)
#define MUSIC_AUDIO_LOG(...) ((void)0)
#else
#define BIG_AUDIO_LOG(...) (sceClibPrintf)(__VA_ARGS__)
#define PCM_AUDIO_LOG(...) (sceClibPrintf)(__VA_ARGS__)
#define MUSIC_AUDIO_LOG(...) (sceClibPrintf)(__VA_ARGS__)
#endif

std::atomic<unsigned int> g_pcm_pull_count{0};
std::atomic<unsigned int> g_pcm_signal_logged{0};
std::atomic<unsigned int> g_pcm_silence_logged{0};

struct BigAudioMapEntry {
    uint64_t pcm_hash = 0;
    uint32_t pcm_size = 0;
    uint32_t resource_id = 0;
    uint32_t pack_index = 0;
    int32_t logical_id = -1;
    char pack[64] = {};
    char path[224] = {};
};

struct LoadedBigMediaEntry {
    void *binary = nullptr;
    uint32_t pack_index = 0;
    uint32_t logical_id = 0;
};

struct DecodedBigMediaEntry {
    void *binary = nullptr;
    unsigned char *pcm_data = nullptr;
    uint32_t pcm_size = 0;
};

struct RejectedBigMediaEntry {
    void *binary = nullptr;
    const unsigned char *pcm_data = nullptr;
    uint32_t pcm_size = 0;
};

using GameVorbisDecodeFn = bool (*)(const unsigned char *, unsigned int,
                                    unsigned char *&, unsigned int &,
                                    unsigned char, unsigned int &,
                                    unsigned int &, unsigned int &);

struct BigAudioMapState {
    std::mutex mutex;
    bool load_attempted = false;
    size_t count = 0;
    BigAudioMapEntry entries[kMaxBigAudioMappings];
    LoadedBigMediaEntry loaded[kMaxLoadedBigMedia];
    size_t loaded_count = 0;
    size_t loaded_replace = 0;
    DecodedBigMediaEntry decoded[kMaxLoadedBigMedia];
    size_t decoded_count = 0;
    size_t decoded_replace = 0;
    RejectedBigMediaEntry rejected[kMaxLoadedBigMedia];
    size_t rejected_count = 0;
    size_t rejected_replace = 0;
    GameVorbisDecodeFn vorbis_decoder = nullptr;
    bool decoder_missing_reported = false;
    uint64_t misses[kMaxBigAudioMisses] = {};
    size_t miss_count = 0;
};

BigAudioMapState g_big_audio_map;

static bool media_was_decoded_locked(void *binary,
                                     const unsigned char *data,
                                     uint32_t size) {
    for (size_t i = 0; i < g_big_audio_map.decoded_count; ++i) {
        DecodedBigMediaEntry &entry = g_big_audio_map.decoded[i];
        if (entry.binary != binary) {
            continue;
        }
        if (entry.pcm_data == data && entry.pcm_size == size) {
            return true;
        }

        /* The old CMedia was destroyed and its address was reused. */
        entry = {};
        return false;
    }
    return false;
}

static void remember_decoded_media_locked(void *binary,
                                          unsigned char *data,
                                          uint32_t size) {
    size_t slot = g_big_audio_map.decoded_count;
    for (size_t i = 0; i < g_big_audio_map.decoded_count; ++i) {
        if (!g_big_audio_map.decoded[i].binary ||
            g_big_audio_map.decoded[i].binary == binary) {
            slot = i;
            break;
        }
    }
    if (slot >= kMaxLoadedBigMedia) {
        slot = g_big_audio_map.decoded_replace++ % kMaxLoadedBigMedia;
    } else if (slot == g_big_audio_map.decoded_count) {
        ++g_big_audio_map.decoded_count;
    }
    g_big_audio_map.decoded[slot] = {binary, data, size};
}

static bool media_was_rejected_locked(void *binary,
                                      const unsigned char *data,
                                      uint32_t size) {
    for (size_t i = 0; i < g_big_audio_map.rejected_count; ++i) {
        RejectedBigMediaEntry &entry = g_big_audio_map.rejected[i];
        if (entry.binary != binary) {
            continue;
        }
        if (entry.pcm_data == data && entry.pcm_size == size) {
            return true;
        }
        entry = {};
        return false;
    }
    return false;
}

static void remember_rejected_media_locked(void *binary,
                                           const unsigned char *data,
                                           uint32_t size) {
    size_t slot = g_big_audio_map.rejected_count;
    for (size_t i = 0; i < g_big_audio_map.rejected_count; ++i) {
        if (!g_big_audio_map.rejected[i].binary ||
            g_big_audio_map.rejected[i].binary == binary) {
            slot = i;
            break;
        }
    }
    if (slot >= kMaxLoadedBigMedia) {
        slot = g_big_audio_map.rejected_replace++ % kMaxLoadedBigMedia;
    } else if (slot == g_big_audio_map.rejected_count) {
        ++g_big_audio_map.rejected_count;
    }
    g_big_audio_map.rejected[slot] = {binary, data, size};
}

static uint64_t fnv1a64(const unsigned char *data, size_t size) {
    uint64_t result = kFnv64OffsetBasis;
    for (size_t i = 0; i < size; ++i) {
        result ^= data[i];
        result *= kFnv64Prime;
    }
    return result;
}

static bool parse_u64_hex(const char *text, uint64_t &value) {
    char *end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(text, &end, 16);
    if (errno != 0 || !end || end == text || *end != '\0') {
        return false;
    }
    value = static_cast<uint64_t>(parsed);
    return true;
}

static bool parse_u32_decimal(const char *text, uint32_t &value) {
    char *end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (errno != 0 || !end || end == text || *end != '\0' ||
        parsed > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

static bool parse_i32_decimal(const char *text, int32_t &value) {
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text, &end, 10);
    if (errno != 0 || !end || end == text || *end != '\0' ||
        parsed < std::numeric_limits<int32_t>::min() ||
        parsed > std::numeric_limits<int32_t>::max()) {
        return false;
    }
    value = static_cast<int32_t>(parsed);
    return true;
}

static bool safe_relative_audio_path(const char *path) {
    return path && path[0] != '\0' && path[0] != '/' && path[0] != '\\' &&
           std::strchr(path, ':') == nullptr && std::strstr(path, "..") == nullptr &&
           std::strncmp(path, "audio/", 6) == 0;
}

static bool parse_pack_index(const char *pack, uint32_t &pack_index) {
    if (!pack || std::strncmp(pack, "pack", 4) != 0) {
        return false;
    }
    const char *digits = pack + 4;
    char *end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(digits, &end, 10);
    if (errno != 0 || !end || end == digits || *end != '_' ||
        parsed > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    pack_index = static_cast<uint32_t>(parsed);
    return true;
}

static bool split_manifest_line(char *line, char *fields[6]) {
    char *cursor = line;
    for (size_t i = 0; i < 6; ++i) {
        fields[i] = cursor;
        char *comma = std::strchr(cursor, ',');
        if (i == 5) {
            if (comma) {
                return false;
            }
            char *end = fields[i] + std::strlen(fields[i]);
            while (end > fields[i] && (end[-1] == '\r' || end[-1] == '\n')) {
                *--end = '\0';
            }
            return fields[i][0] != '\0';
        }
        if (!comma) {
            return false;
        }
        *comma = '\0';
        cursor = comma + 1;
    }
    return false;
}

static void load_big_audio_manifest_locked() {
    if (g_big_audio_map.load_attempted) {
        return;
    }
    g_big_audio_map.load_attempted = true;

    const char *manifest_path = DATA_PATH "audio/runtime/audio_map.csv";
    FILE *manifest = std::fopen(manifest_path, "rb");
    if (!manifest) {
        BIG_AUDIO_LOG("[AUDIO-BIG][DISABLED] manifest missing: %s\n", manifest_path);
        return;
    }

    char line[512];
    unsigned int line_number = 0;
    unsigned int rejected = 0;
    while (std::fgets(line, sizeof(line), manifest)) {
        ++line_number;
        if (line_number == 1 && std::strncmp(line, "pcm_fnv64,", 10) == 0) {
            continue;
        }
        if (g_big_audio_map.count >= kMaxBigAudioMappings) {
            ++rejected;
            continue;
        }

        char *fields[6] = {};
        BigAudioMapEntry entry;
        if (!split_manifest_line(line, fields) ||
            !parse_u64_hex(fields[0], entry.pcm_hash) ||
            !parse_u32_decimal(fields[1], entry.pcm_size) ||
            !parse_u32_decimal(fields[3], entry.resource_id) ||
            !parse_i32_decimal(fields[4], entry.logical_id) ||
            !parse_pack_index(fields[2], entry.pack_index) ||
            !safe_relative_audio_path(fields[5]) ||
            std::strlen(fields[2]) >= sizeof(entry.pack) ||
            std::strlen(fields[5]) >= sizeof(entry.path)) {
            ++rejected;
            continue;
        }
        std::strcpy(entry.pack, fields[2]);
        std::strcpy(entry.path, fields[5]);
        g_big_audio_map.entries[g_big_audio_map.count++] = entry;
    }
    std::fclose(manifest);

    BIG_AUDIO_LOG("[AUDIO-BIG][MAP] loaded %u WAV->OGG mappings from %s%s\n",
                  static_cast<unsigned int>(g_big_audio_map.count), manifest_path,
                  rejected ? " (some invalid rows rejected)" : "");
}

static bool hash_was_reported_missing_locked(uint64_t hash) {
    for (size_t i = 0; i < g_big_audio_map.miss_count; ++i) {
        if (g_big_audio_map.misses[i] == hash) {
            return true;
        }
    }
    if (g_big_audio_map.miss_count < kMaxBigAudioMisses) {
        g_big_audio_map.misses[g_big_audio_map.miss_count++] = hash;
        return false;
    }
    return true;
}

static bool hash_is_known_unusable_locked(uint64_t hash) {
    for (size_t i = 0; i < g_big_audio_map.miss_count; ++i) {
        if (g_big_audio_map.misses[i] == hash) {
            return true;
        }
    }
    return false;
}

static unsigned char *read_ogg_file(const char *relative_path, uint32_t &size_out) {
    char full_path[512];
    const int length = std::snprintf(full_path, sizeof(full_path), DATA_PATH "%s",
                                     relative_path);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(full_path)) {
        return nullptr;
    }

    FILE *file = std::fopen(full_path, "rb");
    if (!file) {
        return nullptr;
    }
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return nullptr;
    }
    const long length_bytes = std::ftell(file);
    if (length_bytes <= 4 || length_bytes > kMaxBigAudioOggBytes ||
        std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return nullptr;
    }

    auto *data = static_cast<unsigned char *>(
        std::malloc(static_cast<size_t>(length_bytes)));
    if (!data) {
        std::fclose(file);
        return nullptr;
    }
    const size_t read = std::fread(data, 1, static_cast<size_t>(length_bytes), file);
    std::fclose(file);
    if (read != static_cast<size_t>(length_bytes) ||
        std::memcmp(data, "OggS", 4) != 0) {
        std::free(data);
        return nullptr;
    }

    size_out = static_cast<uint32_t>(length_bytes);
    return data;
}

static std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

static bool file_exists(const std::string &path) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) {
        return false;
    }
    std::fclose(file);
    return true;
}

static std::string normalize_request(const char *requested_path) {
    std::string request = requested_path ? requested_path : "";
    std::replace(request.begin(), request.end(), '\\', '/');

    if (request.rfind("file://", 0) == 0) {
        request.erase(0, 7);
    }
    while (request.rfind("./", 0) == 0) {
        request.erase(0, 2);
    }
    if (request.rfind("/ux0:", 0) == 0) {
        request.erase(0, 1);
    }
    return request;
}

static std::string with_vita_audio_extension(std::string request) {
    const size_t slash = request.find_last_of('/');
    const size_t dot = request.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        const std::string extension = lower_copy(request.substr(dot));
        if (extension == ".mp3" || extension == ".ogg" || extension == ".wav") {
            request.erase(dot);
        }
    }
    request += ".ogg";
    return request;
}

static bool resolve_music_path(const char *requested_path,
                               std::string &request,
                               std::string &resolved) {
    request = normalize_request(requested_path);
    if (request.empty()) {
        resolved = std::string(DATA_PATH) + "files/";
        MUSIC_AUDIO_LOG("[AUDIO-MUSIC][MAP] request='%s' -> '%s' exists=0\n",
                        requested_path ? requested_path : "(null)",
                        resolved.c_str());
        return false;
    }

    const std::string ogg_request = with_vita_audio_extension(request);
    const size_t slash = ogg_request.find_last_of('/');
    const std::string basename = slash == std::string::npos
        ? ogg_request
        : ogg_request.substr(slash + 1);
    resolved = std::string(DATA_PATH) + "files/" + basename;
    const bool exists = file_exists(resolved);
    MUSIC_AUDIO_LOG("[AUDIO-MUSIC][MAP] request='%s' -> '%s' exists=%d\n",
                    requested_path ? requested_path : "(null)",
                    resolved.c_str(), exists ? 1 : 0);
    return exists;
}

class GunBrosPcmInstance final : public SoLoud::AudioSourceInstance {
public:
    explicit GunBrosPcmInstance(unsigned int channels)
        : channels_(channels == 1 ? 1u : 2u) {
    }

    void getAudio(float *buffer, unsigned int samples_to_read) override {
        const size_t sample_count = static_cast<size_t>(samples_to_read) * channels_;
        if (pcm_.size() < sample_count) {
            pcm_.resize(sample_count);
        }

        std::fill(pcm_.begin(), pcm_.begin() + sample_count, 0);
        gunbros_fill_game_pcm(pcm_.data(),
                              static_cast<unsigned int>(sample_count * sizeof(int16_t)));

        const unsigned int pull =
            g_pcm_pull_count.fetch_add(1, std::memory_order_relaxed) + 1;
        if (pull == 1) {
            PCM_AUDIO_LOG("[AUDIO-PCM][PULL] mixer callback active bytes=%u\n",
                          static_cast<unsigned int>(sample_count * sizeof(int16_t)));
        }
        if (!g_pcm_signal_logged.load(std::memory_order_relaxed) &&
            (pull <= 512 || (pull & 63u) == 0)) {
            bool has_signal = false;
            for (size_t i = 0; i < sample_count; ++i) {
                if (pcm_[i] != 0) {
                    has_signal = true;
                    break;
                }
            }
            if (has_signal &&
                g_pcm_signal_logged.exchange(1, std::memory_order_relaxed) == 0) {
                PCM_AUDIO_LOG("[AUDIO-PCM][SIGNAL] game mixer produced audio "
                              "at pull=%u\n", pull);
            } else if (!has_signal && pull == 512 &&
                       g_pcm_silence_logged.exchange(1, std::memory_order_relaxed) == 0) {
                PCM_AUDIO_LOG("[AUDIO-PCM][SILENT] game mixer returned only "
                              "zero samples through pull=%u\n", pull);
            }
        }

        constexpr float scale = 1.0f / 32768.0f;
        if (channels_ == 1) {
            for (unsigned int i = 0; i < samples_to_read; ++i) {
                buffer[i] = static_cast<float>(pcm_[i]) * scale;
            }
        } else {
            for (unsigned int i = 0; i < samples_to_read; ++i) {
                buffer[i] = static_cast<float>(pcm_[i * 2]) * scale;
                buffer[samples_to_read + i] = static_cast<float>(pcm_[i * 2 + 1]) * scale;
            }
        }
    }

    bool hasEnded() override {
        return false;
    }

private:
    unsigned int channels_;
    std::vector<int16_t> pcm_;
};

class GunBrosPcmSource final : public SoLoud::AudioSource {
public:
    GunBrosPcmSource() {
        configure(kDefaultPcmRate, kDefaultPcmChannels);
    }

    void configure(unsigned int sample_rate, unsigned int channels) {
        mBaseSamplerate = static_cast<float>(sample_rate ? sample_rate : kDefaultPcmRate);
        mChannels = channels == 1 ? 1u : 2u;
    }

    SoLoud::AudioSourceInstance *createInstance() override {
        return new (std::nothrow) GunBrosPcmInstance(mChannels);
    }
};

struct AudioState {
    SoLoud::Soloud soloud;
    SoLoud::WavStream music;
    GunBrosPcmSource pcm_source;
    std::mutex mutex;
    bool initialized = false;
    bool music_files_checked = false;
    bool music_loaded = false;
    bool music_looping = false;
    bool pcm_requested = false;
    unsigned int pcm_rate = kDefaultPcmRate;
    unsigned int pcm_channels = kDefaultPcmChannels;
    unsigned int pcm_bits = kDefaultPcmBits;
    SoLoud::handle pcm_handle = 0;
    SoLoud::handle music_handle = 0;
    float music_volume = 1.0f;
    double pending_seek_seconds = 0.0;
    uint64_t music_clock_anchor_us = 0;
    bool music_clock_running = false;
    std::string current_request;
};

AudioState g_audio;

static bool init_locked() {
    if (g_audio.initialized) {
        return true;
    }

    const SoLoud::result result = g_audio.soloud.init(
        SoLoud::Soloud::CLIP_ROUNDOFF,
        SoLoud::Soloud::VITA_HOMEBREW,
        44100,
        1024,
        2);
    if (result != SoLoud::SO_NO_ERROR) {
        PCM_AUDIO_LOG("[AUDIO][ERROR] SoLoud init failed: %s (%d)\n",
                      g_audio.soloud.getErrorString(result), result);
        return false;
    }

    g_audio.soloud.setGlobalVolume(1.0f);
    g_audio.soloud.setMaxActiveVoiceCount(16);
    g_audio.soloud.setVisualizationEnable(false);
    g_audio.pcm_source.configure(g_audio.pcm_rate, g_audio.pcm_channels);
    g_audio.initialized = true;
    PCM_AUDIO_LOG("[AUDIO][OUTPUT] SoLoud ready backend=VITA_HOMEBREW "
                  "rate=44100 buffer=1024 channels=2\n");
    return true;
}

static void log_music_file_set_locked() {
    if (g_audio.music_files_checked) {
        return;
    }
    g_audio.music_files_checked = true;

    static const char *const filenames[] = {
        "game_0.ogg", "1.ogg", "2.ogg", "3.ogg",
        "4.ogg", "5.ogg", "6.ogg",
    };
    unsigned int ready = 0;
    for (const char *filename : filenames) {
        const std::string path = std::string(DATA_PATH) + "files/" + filename;
        if (file_exists(path)) {
            ++ready;
        } else {
            MUSIC_AUDIO_LOG("[AUDIO-MUSIC][FILES][MISSING] %s\n", path.c_str());
        }
    }
    MUSIC_AUDIO_LOG("[AUDIO-MUSIC][FILES] ready=%u/%u base='%sfiles/'\n",
                    ready,
                    static_cast<unsigned int>(sizeof(filenames) / sizeof(filenames[0])),
                    DATA_PATH);
}

static bool valid_handle(SoLoud::handle handle) {
    return handle != 0 && g_audio.soloud.isValidVoiceHandle(handle);
}

static double music_position_locked() {
    double position = g_audio.pending_seek_seconds;
    if (g_audio.music_clock_running && g_audio.music_clock_anchor_us != 0) {
        const uint64_t now = sceKernelGetProcessTimeWide();
        if (now >= g_audio.music_clock_anchor_us) {
            position += static_cast<double>(now - g_audio.music_clock_anchor_us) / 1000000.0;
        }
    }
    return position;
}

static void freeze_music_clock_locked() {
    g_audio.pending_seek_seconds = music_position_locked();
    g_audio.music_clock_anchor_us = 0;
    g_audio.music_clock_running = false;
}

static void start_music_clock_locked() {
    if (!g_audio.music_clock_running) {
        g_audio.music_clock_anchor_us = sceKernelGetProcessTimeWide();
        g_audio.music_clock_running = true;
    }
}

static void reset_music_clock_locked() {
    g_audio.pending_seek_seconds = 0.0;
    g_audio.music_clock_anchor_us = 0;
    g_audio.music_clock_running = false;
}

} // namespace

extern "C" int gunbros_audio_init(void) {
    {
        std::lock_guard<std::mutex> map_lock(g_big_audio_map.mutex);
        load_big_audio_manifest_locked();
    }
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (!init_locked()) {
        return 0;
    }
    log_music_file_set_locked();
    return 1;
}

extern "C" void gunbros_audio_set_game_vorbis_decoder(uintptr_t decoder) {
    std::lock_guard<std::mutex> lock(g_big_audio_map.mutex);
    g_big_audio_map.vorbis_decoder =
        reinterpret_cast<GameVorbisDecodeFn>(decoder);
    g_big_audio_map.decoder_missing_reported = false;
    if (g_big_audio_map.vorbis_decoder) {
        BIG_AUDIO_LOG("[AUDIO-BIG][DECODER] game Vorbis decoder ready at %p\n",
                      reinterpret_cast<void *>(decoder));
    } else {
        BIG_AUDIO_LOG("[AUDIO-BIG][ERROR] game Vorbis decoder unavailable; "
                      "BIG WAV fallback enabled\n");
    }
}

extern "C" void gunbros_audio_register_big_media(void *binary,
                                                   unsigned int pack_index,
                                                   unsigned int logical_id) {
    if (!binary) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_big_audio_map.mutex);
    load_big_audio_manifest_locked();

    size_t slot = g_big_audio_map.loaded_count;
    bool same_registration = false;
    for (size_t i = 0; i < g_big_audio_map.loaded_count; ++i) {
        if (g_big_audio_map.loaded[i].binary == binary) {
            slot = i;
            same_registration =
                g_big_audio_map.loaded[i].pack_index == pack_index &&
                g_big_audio_map.loaded[i].logical_id == logical_id;
            break;
        }
    }
    if (slot >= kMaxLoadedBigMedia) {
        slot = g_big_audio_map.loaded_replace++ % kMaxLoadedBigMedia;
    } else if (slot == g_big_audio_map.loaded_count) {
        ++g_big_audio_map.loaded_count;
    }
    g_big_audio_map.loaded[slot] = {
        binary,
        static_cast<uint32_t>(pack_index),
        static_cast<uint32_t>(logical_id),
    };

    if (same_registration) {
        return;
    }
    for (size_t i = 0; i < g_big_audio_map.count; ++i) {
        const BigAudioMapEntry &entry = g_big_audio_map.entries[i];
        if (entry.pack_index == pack_index && entry.logical_id >= 0 &&
            static_cast<uint32_t>(entry.logical_id) == logical_id) {
            BIG_AUDIO_LOG("[AUDIO-BIG][LOAD] pack=%s resource=%u logical=%d "
                          "media=%p -> %s\n",
                          entry.pack, entry.resource_id, entry.logical_id,
                          binary, entry.path);
            return;
        }
    }
}

extern "C" int gunbros_audio_redirect_big_sound(void *binary) {
    if (!binary) {
        return 0;
    }

    auto *object = static_cast<unsigned char *>(binary);
    auto **data_slot = reinterpret_cast<unsigned char **>(object + 8);
    auto *size_slot = reinterpret_cast<uint32_t *>(object + 12);
    auto *mime_slot = reinterpret_cast<uint32_t *>(object + 16);
    auto *channels_slot = reinterpret_cast<uint32_t *>(object + 24);
    auto *sample_rate_slot = reinterpret_cast<uint32_t *>(object + 28);
    auto *sample_bits_slot = reinterpret_cast<uint32_t *>(object + 32);

    std::lock_guard<std::mutex> lock(g_big_audio_map.mutex);
    load_big_audio_manifest_locked();
    if (g_big_audio_map.count == 0) {
        return 0;
    }

    unsigned char *pcm_data = *data_slot;
    const uint32_t pcm_size = *size_slot;
    if (!pcm_data || pcm_size == 0) {
        return 0;
    }
    if (media_was_decoded_locked(binary, pcm_data, pcm_size)) {
        return 1;
    }
    if (media_was_rejected_locked(binary, pcm_data, pcm_size)) {
        return 0;
    }
    const uint64_t hash = fnv1a64(pcm_data, pcm_size);
    if (hash_is_known_unusable_locked(hash)) {
        remember_rejected_media_locked(binary, pcm_data, pcm_size);
        return 0;
    }

    uint32_t loaded_pack_index = 0;
    uint32_t loaded_logical_id = 0;
    bool have_loaded_identity = false;
    for (size_t i = 0; i < g_big_audio_map.loaded_count; ++i) {
        if (g_big_audio_map.loaded[i].binary == binary) {
            loaded_pack_index = g_big_audio_map.loaded[i].pack_index;
            loaded_logical_id = g_big_audio_map.loaded[i].logical_id;
            have_loaded_identity = true;
            break;
        }
    }

    size_t aliases = 0;
    for (size_t i = 0; i < g_big_audio_map.count; ++i) {
        const BigAudioMapEntry &candidate = g_big_audio_map.entries[i];
        if (candidate.pcm_hash == hash && candidate.pcm_size == pcm_size) {
            ++aliases;
        }
    }

    if (aliases != 0 && !g_big_audio_map.vorbis_decoder) {
        if (!g_big_audio_map.decoder_missing_reported) {
            BIG_AUDIO_LOG("[AUDIO-BIG][ERROR] mapped OGG cannot be decoded; "
                          "keeping BIG WAV\n");
            g_big_audio_map.decoder_missing_reported = true;
        }
        remember_rejected_media_locked(binary, pcm_data, pcm_size);
        return 0;
    }

    for (unsigned int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < g_big_audio_map.count; ++i) {
            const BigAudioMapEntry &entry = g_big_audio_map.entries[i];
            if (entry.pcm_hash != hash || entry.pcm_size != pcm_size) {
                continue;
            }
            const bool identity_match = have_loaded_identity &&
                entry.pack_index == loaded_pack_index && entry.logical_id >= 0 &&
                static_cast<uint32_t>(entry.logical_id) == loaded_logical_id;
            if ((pass == 0 && !identity_match) ||
                (pass == 1 && identity_match)) {
                continue;
            }

            uint32_t ogg_size = 0;
            unsigned char *ogg_data = read_ogg_file(entry.path, ogg_size);
            if (!ogg_data) {
                continue;
            }

            unsigned char *decoded_pcm = nullptr;
            unsigned int decoded_size = 0;
            unsigned int channels = 0;
            unsigned int sample_rate = 0;
            unsigned int sample_bits = 0;
            const bool decoded = g_big_audio_map.vorbis_decoder(
                ogg_data, ogg_size, decoded_pcm, decoded_size, 1,
                channels, sample_bits, sample_rate);
            std::free(ogg_data);

            if (!decoded || !decoded_pcm || decoded_size == 0 ||
                (channels != 1 && channels != 2) ||
                sample_rate < 8000 || sample_rate > 192000 ||
                (sample_bits != 8 && sample_bits != 16)) {
                if (decoded_pcm) {
                    std::free(decoded_pcm);
                }
                continue;
            }

            *data_slot = decoded_pcm;
            *size_slot = decoded_size;
            *mime_slot = kDecodedPcmMimeKey;
            *channels_slot = channels;
            *sample_rate_slot = sample_rate;
            *sample_bits_slot = sample_bits;
            remember_decoded_media_locked(binary, decoded_pcm, decoded_size);
            std::free(pcm_data);

            if (have_loaded_identity) {
                BIG_AUDIO_LOG("[AUDIO-BIG][WAV->OGG] mode=decoded-pcm "
                              "loaded=pack%u/logical%u "
                              "map=%s/resource%u/logical%d pcm=%u hash=%016llx "
                              "-> %s (%u bytes) -> pcm=%u %uHz/%uch/%ubit "
                              "(aliases=%u)\n",
                              loaded_pack_index, loaded_logical_id, entry.pack,
                              entry.resource_id, entry.logical_id, pcm_size,
                              static_cast<unsigned long long>(hash), entry.path,
                              ogg_size, decoded_size, sample_rate, channels,
                              sample_bits, static_cast<unsigned int>(aliases));
            } else {
                BIG_AUDIO_LOG("[AUDIO-BIG][WAV->OGG] mode=decoded-pcm "
                              "loaded=hash-match "
                              "map=%s/resource%u/logical%d pcm=%u hash=%016llx "
                              "-> %s (%u bytes) -> pcm=%u %uHz/%uch/%ubit "
                              "(aliases=%u)\n",
                              entry.pack, entry.resource_id, entry.logical_id,
                              pcm_size, static_cast<unsigned long long>(hash),
                              entry.path, ogg_size, decoded_size, sample_rate,
                              channels, sample_bits,
                              static_cast<unsigned int>(aliases));
            }
            return 2;
        }
    }

    if (aliases != 0 && !hash_was_reported_missing_locked(hash)) {
        BIG_AUDIO_LOG("[AUDIO-BIG][ERROR] mapped OGG missing or invalid "
                      "pcm=%u hash=%016llx aliases=%u; keeping BIG WAV\n",
                      pcm_size, static_cast<unsigned long long>(hash),
                      static_cast<unsigned int>(aliases));
    } else if (!hash_was_reported_missing_locked(hash)) {
        BIG_AUDIO_LOG("[AUDIO-BIG][MISS] no OGG mapping pcm=%u hash=%016llx; "
                      "keeping BIG WAV\n",
                      pcm_size, static_cast<unsigned long long>(hash));
    }
    remember_rejected_media_locked(binary, pcm_data, pcm_size);
    return 0;
}

extern "C" void gunbros_audio_destroy(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (!g_audio.initialized) {
        return;
    }

    g_audio.soloud.stopAll();
    g_audio.music.stop();
    g_audio.soloud.deinit();
    g_audio.initialized = false;
    g_audio.pcm_requested = false;
    g_audio.music_loaded = false;
    g_audio.pcm_handle = 0;
    g_audio.music_handle = 0;
    reset_music_clock_locked();
    g_audio.current_request.clear();
}

extern "C" void gunbros_audio_configure_pcm(unsigned int sample_rate,
                                              unsigned int channels,
                                              unsigned int sample_bits) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    const unsigned int old_rate = g_audio.pcm_rate;
    const unsigned int old_channels = g_audio.pcm_channels;
    const unsigned int old_bits = g_audio.pcm_bits;

    if (sample_rate >= 8000 && sample_rate <= 48000) {
        g_audio.pcm_rate = sample_rate;
    }
    if (channels == 1 || channels == 2) {
        g_audio.pcm_channels = channels;
    }
    if (sample_bits == 16) {
        g_audio.pcm_bits = sample_bits;
    }

    if ((old_rate != g_audio.pcm_rate ||
         old_channels != g_audio.pcm_channels ||
         old_bits != g_audio.pcm_bits) &&
        g_audio.initialized && valid_handle(g_audio.pcm_handle)) {
        g_audio.soloud.stop(g_audio.pcm_handle);
        g_audio.pcm_handle = 0;
    }
    g_audio.pcm_source.configure(g_audio.pcm_rate, g_audio.pcm_channels);
}

extern "C" int gunbros_audio_start_pcm(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (!init_locked() || g_audio.pcm_bits != 16) {
        return 0;
    }
    g_audio.pcm_requested = true;
    if (valid_handle(g_audio.pcm_handle)) {
        g_audio.soloud.setPause(g_audio.pcm_handle, false);
        g_audio.soloud.setProtectVoice(g_audio.pcm_handle, true);
        return 1;
    }

    g_audio.pcm_source.configure(g_audio.pcm_rate, g_audio.pcm_channels);
    g_pcm_pull_count.store(0, std::memory_order_relaxed);
    g_pcm_signal_logged.store(0, std::memory_order_relaxed);
    g_pcm_silence_logged.store(0, std::memory_order_relaxed);
    g_audio.pcm_handle = g_audio.soloud.play(g_audio.pcm_source, 1.0f);
    const int started = valid_handle(g_audio.pcm_handle) ? 1 : 0;
    if (started) {
        g_audio.soloud.setProtectVoice(g_audio.pcm_handle, true);
    }
    PCM_AUDIO_LOG("[AUDIO-PCM][START] rate=%u channels=%u bits=%u result=%d "
                  "handle=%u\n", g_audio.pcm_rate, g_audio.pcm_channels,
                  g_audio.pcm_bits, started,
                  static_cast<unsigned int>(g_audio.pcm_handle));
    return started;
}

extern "C" void gunbros_audio_stop_pcm(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    g_audio.pcm_requested = false;
    if (g_audio.initialized && valid_handle(g_audio.pcm_handle)) {
        g_audio.soloud.stop(g_audio.pcm_handle);
    }
    g_audio.pcm_handle = 0;
}

extern "C" void gunbros_audio_service(void) {
    bool recover = false;
    {
        std::lock_guard<std::mutex> lock(g_audio.mutex);
        recover = g_audio.initialized && g_audio.pcm_requested &&
                  !valid_handle(g_audio.pcm_handle);
        if (recover) {
            PCM_AUDIO_LOG("[AUDIO-PCM][LOST] voice=%u; restarting stream\n",
                          static_cast<unsigned int>(g_audio.pcm_handle));
            g_audio.pcm_handle = 0;
        }
    }
    if (recover) {
        (void)gunbros_audio_start_pcm();
    }
}

extern "C" int gunbros_music_load(const char *requested_path) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    std::string request;
    std::string resolved;

    if (!init_locked()) {
        return 0;
    }
    if (!resolve_music_path(requested_path, request, resolved)) {
        MUSIC_AUDIO_LOG("[AUDIO-MUSIC][ERROR] OGG replacement missing for "
                        "'%s' (expected '%s')\n",
                        requested_path ? requested_path : "(null)",
                        resolved.c_str());
        return 0;
    }
    if (g_audio.music_loaded && g_audio.current_request == request) {
        return 1;
    }

    if (valid_handle(g_audio.music_handle)) {
        g_audio.soloud.stop(g_audio.music_handle);
    }
    g_audio.music_handle = 0;
    g_audio.music.stop();
    g_audio.music_loaded = false;
    reset_music_clock_locked();

    const SoLoud::result result = g_audio.music.load(resolved.c_str());
    if (result != SoLoud::SO_NO_ERROR) {
        MUSIC_AUDIO_LOG("[AUDIO-MUSIC][ERROR] decoder rejected '%s': %s (%d)\n",
                        resolved.c_str(), g_audio.soloud.getErrorString(result),
                        result);
        g_audio.current_request.clear();
        return 0;
    }

    g_audio.music_loaded = true;
    g_audio.current_request = request;
    MUSIC_AUDIO_LOG("[AUDIO-MUSIC][LOAD] redirect '%s' -> '%s'\n",
                    requested_path ? requested_path : "", resolved.c_str());
    return 1;
}

extern "C" void gunbros_music_start(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (!g_audio.initialized || !g_audio.music_loaded) {
        return;
    }
    if (valid_handle(g_audio.music_handle)) {
        g_audio.soloud.setPause(g_audio.music_handle, false);
        g_audio.soloud.setProtectVoice(g_audio.music_handle, true);
        start_music_clock_locked();
        return;
    }

    if (g_audio.music_looping) {
        g_audio.music.mFlags |= SoLoud::AudioSource::SHOULD_LOOP;
    } else {
        g_audio.music.mFlags &= ~SoLoud::AudioSource::SHOULD_LOOP;
    }
    g_audio.music_handle = g_audio.soloud.playBackground(g_audio.music, g_audio.music_volume);
    const bool started = valid_handle(g_audio.music_handle);
    if (started) {
        g_audio.soloud.setProtectVoice(g_audio.music_handle, true);
        g_audio.soloud.setLooping(g_audio.music_handle, g_audio.music_looping);
        if (g_audio.pending_seek_seconds > 0.0) {
            (void)g_audio.soloud.seek(g_audio.music_handle, g_audio.pending_seek_seconds);
        }
        start_music_clock_locked();
    }
    MUSIC_AUDIO_LOG("[AUDIO-MUSIC][START] request='%s' result=%d handle=%u "
                    "loop=%d volume=%.3f\n",
                    g_audio.current_request.c_str(), started ? 1 : 0,
                    static_cast<unsigned int>(g_audio.music_handle),
                    g_audio.music_looping ? 1 : 0, g_audio.music_volume);
}

extern "C" int gunbros_music_is_playing(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    return g_audio.initialized && g_audio.music_loaded &&
           g_audio.music_clock_running && valid_handle(g_audio.music_handle)
        ? 1
        : 0;
}

extern "C" void gunbros_music_pause(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (g_audio.music_clock_running) {
        freeze_music_clock_locked();
    }
    if (g_audio.initialized && valid_handle(g_audio.music_handle)) {
        g_audio.soloud.setPause(g_audio.music_handle, true);
    }
}

extern "C" void gunbros_music_stop(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (g_audio.initialized && valid_handle(g_audio.music_handle)) {
        g_audio.soloud.stop(g_audio.music_handle);
    }
    g_audio.music_handle = 0;
    reset_music_clock_locked();
}

extern "C" void gunbros_music_seek_ms(int position_ms) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    g_audio.pending_seek_seconds = std::max(0, position_ms) / 1000.0;
    if (g_audio.music_clock_running) {
        g_audio.music_clock_anchor_us = sceKernelGetProcessTimeWide();
    }
    if (g_audio.initialized && valid_handle(g_audio.music_handle)) {
        (void)g_audio.soloud.seek(g_audio.music_handle, g_audio.pending_seek_seconds);
    }
}

extern "C" int gunbros_music_get_position_ms(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    const double milliseconds = std::max(0.0, music_position_locked() * 1000.0);
    const double maximum = static_cast<double>(std::numeric_limits<int>::max());
    return static_cast<int>(std::min(milliseconds, maximum));
}

extern "C" void gunbros_music_set_looping(int looping) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    g_audio.music_looping = looping != 0;
    if (g_audio.initialized && valid_handle(g_audio.music_handle)) {
        g_audio.soloud.setLooping(g_audio.music_handle, g_audio.music_looping);
    }
}

extern "C" void gunbros_music_set_volume(float left, float right) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    left = std::clamp(left, 0.0f, 1.0f);
    right = std::clamp(right, 0.0f, 1.0f);
    g_audio.music_volume = (left + right) * 0.5f;
    if (g_audio.initialized && valid_handle(g_audio.music_handle)) {
        g_audio.soloud.setVolume(g_audio.music_handle, g_audio.music_volume);
    }
}

extern "C" void gunbros_music_release(void) {
    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (g_audio.initialized && valid_handle(g_audio.music_handle)) {
        g_audio.soloud.stop(g_audio.music_handle);
    }
    g_audio.music_handle = 0;
    g_audio.music.stop();
    g_audio.music_loaded = false;
    reset_music_clock_locked();
    g_audio.current_request.clear();
}
