#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

void ws_init(void);
void ws_task(void *arg);
bool ws_is_connected(void);
void ws_send_audio(const uint8_t* wav_buf, uint32_t wav_size);
void ws_request_reconnect(void);
uint8_t* ws_get_audio_buffer(void);
size_t ws_get_audio_size(void);
void ws_free_audio_buffer(void);
bool ws_audio_is_pcm(void);
int ws_audio_get_sample_rate(void);
int ws_audio_get_channels(void);
int ws_audio_get_bits(void);

#endif
