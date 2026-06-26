#ifndef AUDIO_BSP_H
#define AUDIO_BSP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_bsp_init(void);
void audio_play_init(void);
void audio_playback_set_vol(uint8_t vol);
void audio_playback_read(void *data_ptr, uint32_t len);
void audio_playback_write(void *data_ptr, uint32_t len);

void audio_start_recording(void);
uint32_t audio_stop_recording(void);
void audio_discard_recording(void);
uint8_t* audio_get_wav_buffer(void);
uint32_t audio_get_wav_size(void);

#ifdef __cplusplus
}
#endif

#endif
