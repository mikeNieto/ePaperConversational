#ifndef API_CLIENT_H
#define API_CLIENT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool api_health_check(void);
bool api_transcribe_audio(uint8_t* wav_buffer, size_t wav_size, char* transcribed_text, size_t text_maxlen);
bool api_send_message(const char* thread_id, const char* message, char* agent_text, size_t text_maxlen, char* audio_url, size_t url_maxlen);
bool api_download_audio(const char* audio_url, uint8_t** mp3_buffer, size_t* mp3_size);
void json_unescape_utf8(char* str);

#ifdef __cplusplus
}
#endif

#endif
