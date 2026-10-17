#ifndef GUNBROS_AUDIO_H
#define GUNBROS_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int gunbros_audio_init(void);
void gunbros_audio_destroy(void);

void gunbros_audio_configure_pcm(unsigned int sample_rate,
                                 unsigned int channels,
                                 unsigned int sample_bits);
int gunbros_audio_start_pcm(void);
void gunbros_audio_stop_pcm(void);
void gunbros_audio_service(void);

int gunbros_music_load(const char *requested_path);
void gunbros_music_start(void);
int gunbros_music_is_playing(void);
void gunbros_music_pause(void);
void gunbros_music_stop(void);
void gunbros_music_seek_ms(int position_ms);
int gunbros_music_get_position_ms(void);
void gunbros_music_set_looping(int looping);
void gunbros_music_set_volume(float left, float right);
void gunbros_music_release(void);

void gunbros_audio_set_game_vorbis_decoder(uintptr_t decoder);

int gunbros_audio_redirect_big_sound(void *binary);

void gunbros_audio_register_big_media(void *binary,
                                      unsigned int pack_index,
                                      unsigned int logical_id);

void gunbros_fill_game_pcm(int16_t *samples, unsigned int byte_count);

#ifdef __cplusplus
}
#endif

#endif /* GUNBROS_AUDIO_H */
