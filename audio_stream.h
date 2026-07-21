#ifndef AUDIO_STREAM_H
#define AUDIO_STREAM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

bool stream_buf_init(size_t capacity);
void stream_buf_free(void);
bool stream_buf_write(const uint8_t* data, size_t len, uint32_t timeout_ms);
size_t stream_buf_read(uint8_t* out, size_t max_len);
void stream_buf_signal_end(void);
bool stream_buf_is_ended(void);
size_t stream_buf_available(void);
void stream_buf_set_reader(TaskHandle_t t);
void stream_buf_reset_stream(void);

#ifdef __cplusplus
}
#endif

#endif
