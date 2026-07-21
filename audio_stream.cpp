#include <Arduino.h>
#include "audio_stream.h"
#include "esp_heap_caps.h"

static uint8_t* buf = NULL;
static size_t capacity = 0;
static volatile size_t read_idx = 0;
static volatile size_t write_idx = 0;
static volatile bool ended = false;
static TaskHandle_t reader_task = NULL;
static SemaphoreHandle_t space_sem = NULL;

static inline size_t buf_available(void)
{
    if (write_idx >= read_idx) return write_idx - read_idx;
    else return capacity - read_idx + write_idx;
}

static inline size_t buf_free_space(void)
{
    if (capacity == 0) return 0;
    if (write_idx >= read_idx) return capacity - write_idx + read_idx - 1;
    else return read_idx - write_idx - 1;
}

static inline void notify_space(void)
{
    if (space_sem) {
        xSemaphoreGive(space_sem);
    }
}

bool stream_buf_init(size_t cap)
{
    stream_buf_free();
    if (cap < 4096) cap = 4096;
    buf = (uint8_t*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) {
        Serial.printf("STREAM: failed to alloc %u bytes PSRAM\n", (unsigned int)cap);
        return false;
    }
    capacity = cap;
    read_idx = 0;
    write_idx = 0;
    ended = false;
    if (!space_sem) {
        space_sem = xSemaphoreCreateBinary();
    }
    Serial.printf("STREAM: ring buf init %u bytes\n", (unsigned int)cap);
    return true;
}

void stream_buf_free(void)
{
    if (buf) { free(buf); buf = NULL; }
    capacity = 0;
    read_idx = 0;
    write_idx = 0;
    ended = false;
    reader_task = NULL;
}

void stream_buf_set_reader(TaskHandle_t t)
{
    reader_task = t;
}

bool stream_buf_write(const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    if (!buf || len == 0) return false;
    if (len > capacity - 1) {
        Serial.printf("STREAM: chunk %u exceeds capacity %u\n", (unsigned int)len, (unsigned int)capacity);
        return false;
    }

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (buf_free_space() < len) {
        if (ended) {
            Serial.printf("STREAM: write aborted, stream ended\n");
            return false;
        }
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) {
            Serial.printf("STREAM: write timeout after %ums\n", (unsigned int)timeout_ms);
            return false;
        }
        if (space_sem) {
            xSemaphoreTake(space_sem, deadline - now);
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    size_t first = capacity - write_idx;
    if (len <= first) {
        memcpy(buf + write_idx, data, len);
    } else {
        memcpy(buf + write_idx, data, first);
        memcpy(buf, data + first, len - first);
    }
    write_idx = (write_idx + len) % capacity;

    if (reader_task) {
        xTaskNotifyGive(reader_task);
    }
    return true;
}

size_t stream_buf_read(uint8_t* out, size_t max_len)
{
    if (!buf || max_len == 0) return 0;
    size_t avail = buf_available();
    if (avail == 0) return 0;
    if (avail > max_len) avail = max_len;

    size_t first = capacity - read_idx;
    if (avail <= first) {
        memcpy(out, buf + read_idx, avail);
    } else {
        memcpy(out, buf + read_idx, first);
        memcpy(out + first, buf, avail - first);
    }
    read_idx = (read_idx + avail) % capacity;

    notify_space();
    return avail;
}

void stream_buf_signal_end(void)
{
    ended = true;
    if (reader_task) {
        xTaskNotifyGive(reader_task);
    }
    notify_space();
    Serial.printf("STREAM: end signaled\n");
}

bool stream_buf_is_ended(void)
{
    return ended;
}

size_t stream_buf_available(void)
{
    if (!buf) return 0;
    return buf_available();
}

void stream_buf_reset_stream(void)
{
    read_idx = 0;
    write_idx = 0;
    ended = false;
}
