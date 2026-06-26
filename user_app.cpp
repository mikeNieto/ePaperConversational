#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "user_app.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "user_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "src/ui/screens.h"
#include "src/ui/status_bar.h"
#include "src/button_bsp/button_bsp.h"
#include "src/i2c_bsp/i2c_bsp.h"
#include "src/touch_bsp/ft6336_bsp.h"
#include "wifi_bsp.h"
#include "src/battery/battery_bsp.h"
#include "audio_bsp.h"
#include "src/codec_board/codec_init.h"
#include "api_client.h"

RTC_DATA_ATTR int boot_count = 0;
RTC_DATA_ATTR int sleep_counter = 0;
RTC_DATA_ATTR char conversation_uuid[37] = {0};
RTC_DATA_ATTR bool uuid_is_null = true;

static const char *TAG = "user_app";

epaper_driver_display *driver = NULL;
board_power_bsp_t board_div(EPD_PWR_PIN, Audio_PWR_PIN, VBAT_PWR_PIN);
bool hasTouch = false;
EventGroupHandle_t touch_event_group = NULL;
static QueueHandle_t gpio_evt_queue = NULL;
TaskHandle_t sleep_timer_handle = NULL;

AppState g_app_state = STATE_ACTIVE;
lv_obj_t* g_btn_continuar = NULL;
lv_obj_t* g_btn_nueva = NULL;
lv_obj_t* g_lbl_continuar = NULL;
lv_obj_t* g_lbl_nueva = NULL;
int g_selected_option = 0;
char g_transcribed_text[512] = {0};
char g_agent_text[1024] = {0};
char g_audio_url[256] = {0};

QueueHandle_t state_queue = NULL;

void generate_uuid(char* buf, size_t len)
{
    snprintf(buf, len, "%08x-%04x-%04x-%04x-%04x%08x",
        esp_random(), esp_random() & 0xFFFF,
        (esp_random() & 0x0FFF) | 0x4000,
        (esp_random() & 0x3FFF) | 0x8000,
        esp_random() & 0xFFFF, esp_random());
}

void highlight_selection(void)
{
    if (uuid_is_null) return;
    if (g_btn_continuar && g_lbl_continuar) {
        bool sel = (g_selected_option == 0);
        lv_obj_set_style_bg_color(g_btn_continuar, sel ? lv_color_black() : lv_color_white(), LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(g_lbl_continuar, sel ? lv_color_white() : lv_color_black(), LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(g_btn_continuar, lv_color_black(), LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(g_btn_continuar, sel ? 0 : 1, LV_STATE_DEFAULT);
    }
    if (g_btn_nueva && g_lbl_nueva) {
        bool sel = (g_selected_option == 1);
        lv_obj_set_style_bg_color(g_btn_nueva, sel ? lv_color_black() : lv_color_white(), LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(g_lbl_nueva, sel ? lv_color_white() : lv_color_black(), LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(g_btn_nueva, lv_color_black(), LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(g_btn_nueva, sel ? 0 : 1, LV_STATE_DEFAULT);
    }
}

static void transcription_task(void *arg)
{
    uint8_t* wav = audio_get_wav_buffer();
    uint32_t size = audio_get_wav_size();
    char text[512] = {0};
    AppEvent evt;

    if (wav && size > 44 && api_transcribe_audio(wav, size, text, sizeof(text))) {
        strncpy(g_transcribed_text, text, sizeof(g_transcribed_text) - 1);
        Serial.printf("Transcribed: %s\n", g_transcribed_text);
        evt.type = EVT_TRANSCRIBE_OK;
    } else {
        Serial.printf("Transcription failed\n");
        evt.type = EVT_TRANSCRIBE_FAIL;
    }
    evt.data = 0;
    xQueueSend(state_queue, &evt, 0);
    vTaskDelete(NULL);
}

static void on_screen1_activate(int option)
{
    if (uuid_is_null || option == 1) {
        generate_uuid(conversation_uuid, sizeof(conversation_uuid));
        uuid_is_null = false;
        Serial.printf("New UUID: %s\n", conversation_uuid);
    }
    switch_state(STATE_RECORD);
}

void switch_state(AppState new_state)
{
    Serial.printf("switch_state: %d -> %d\n", g_app_state, new_state);
    Serial.flush();
    if (!lvgl_lock(2000)) return;

    g_btn_continuar = NULL;
    g_btn_nueva = NULL;
    g_lbl_continuar = NULL;
    g_lbl_nueva = NULL;

    lv_obj_t* old_scr = lv_scr_act();
    lv_obj_t* new_scr = NULL;

    if (new_state == STATE_RECORD) {
        new_scr = create_screen_2_record();
    } else if (new_state == STATE_LISTENING) {
        audio_play_init();
        audio_start_recording();
        new_scr = create_screen_2b_listening();
    } else if (new_state == STATE_ACTIVE) {
        new_scr = create_screen_1_active(hasTouch, uuid_is_null);
    } else if (new_state == STATE_DISCARDED) {
        new_scr = create_screen_3b_discarded();
    } else if (new_state == STATE_SENDING) {
        new_scr = create_screen_3_sending();
    } else if (new_state == STATE_CONFIRM) {
        new_scr = create_screen_4_confirm(g_transcribed_text, hasTouch);
    }

    if (new_scr) {
        lv_scr_load(new_scr);
        lv_timer_handler();
        if (old_scr && old_scr != lv_layer_top()) {
            lv_obj_del(old_scr);
        }
    }

    g_app_state = new_state;
    lvgl_unlock();

    if (new_state == STATE_DISCARDED) {
        vTaskDelay(pdMS_TO_TICKS(1500));
        switch_state(STATE_RECORD);
    } else if (new_state == STATE_SENDING) {
        xTaskCreatePinnedToCore(transcription_task, "stt_task", 8 * 1024, NULL, 3, NULL, 1);
    } else if (new_state == STATE_CONFIRM) {
        g_btn_continuar = NULL;
        g_btn_nueva = NULL;
    }

    activity_feed();
    Serial.printf("switch_state: done\n");
    Serial.flush();
}

static void state_task(void *arg)
{
    AppEvent evt;
    for (;;) {
        if (xQueueReceive(state_queue, &evt, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (g_app_state == STATE_ACTIVE) {
                if (evt.type == EVT_TOGGLE_SELECTION && !uuid_is_null) {
                    if (lvgl_lock(200)) {
                        g_selected_option = !g_selected_option;
                        highlight_selection();
                        lvgl_unlock();
                    }
                } else if (evt.type == EVT_ACTIVATE_OPTION) {
                    int opt = uuid_is_null ? 1 : g_selected_option;
                    on_screen1_activate(opt);
                } else if (evt.type == EVT_TOUCH_OPTION) {
                    on_screen1_activate(evt.data);
                }
            } else if (g_app_state == STATE_RECORD) {
                if (evt.type == EVT_ACTIVATE_OPTION) {
                    switch_state(STATE_LISTENING);
                }
            } else if (g_app_state == STATE_LISTENING) {
                if (evt.type == EVT_ACTIVATE_OPTION) {
                    audio_stop_recording();
                    switch_state(STATE_SENDING);
                } else if (evt.type == EVT_RECORDING_DONE) {
                    Serial.printf("Recording auto-stopped (timeout)\n");
                    audio_discard_recording();
                    switch_state(STATE_DISCARDED);
                }
            } else if (g_app_state == STATE_SENDING) {
                if (evt.type == EVT_TRANSCRIBE_OK) {
                    switch_state(STATE_CONFIRM);
                } else if (evt.type == EVT_TRANSCRIBE_FAIL) {
                    switch_state(STATE_RECORD);
                }
            } else if (g_app_state == STATE_CONFIRM) {
                if (evt.type == EVT_TOGGLE_SELECTION) {
                    if (lvgl_lock(200)) {
                        g_selected_option = !g_selected_option;
                        highlight_selection();
                        lvgl_unlock();
                    }
                } else if (evt.type == EVT_ACTIVATE_OPTION || evt.type == EVT_TOUCH_OPTION) {
                    if (evt.data == 0) {
                        switch_state(STATE_RECORD);
                    } else {
                        switch_state(STATE_RECORD);
                    }
                }
            }
        }
    }
}

static SemaphoreHandle_t lvgl_mux = NULL;

static bool example_lvgl_lock(int timeout_ms)
{
    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;
}

static void example_lvgl_unlock(void)
{
    xSemaphoreGive(lvgl_mux);
}

bool lvgl_lock(int timeout_ms)
{
    return example_lvgl_lock(timeout_ms);
}

void lvgl_unlock(void)
{
    example_lvgl_unlock();
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    uint16_t *buffer = (uint16_t *)color_map;
    driver->EPD_Clear();
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            uint8_t color = (*buffer < 0x7fff) ? DRIVER_COLOR_BLACK : DRIVER_COLOR_WHITE;
            driver->EPD_DrawColorPixel(x, y, color);
            buffer++;
        }
    }
    driver->EPD_DisplayPart();
    lv_disp_flush_ready(drv);
}

static void example_increase_lvgl_tick(void *arg)
{
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static void lvgl_port_task(void *arg)
{
    uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
    for (;;) {
        if (example_lvgl_lock(-1)) {
            task_delay_ms = lv_timer_handler();
            example_lvgl_unlock();
        }
        if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS) {
            task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS) {
            task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

void lvgl_port_init(void)
{
    lv_init();

    lv_color_t *buffer_1 = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_SPIRAM);
    if (!buffer_1) {
        ESP_LOGW(TAG, "SPIRAM alloc for LVGL buf1 failed, using internal RAM");
        buffer_1 = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_INTERNAL);
    }
    lv_color_t *buffer_2 = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_SPIRAM);
    if (!buffer_2) {
        ESP_LOGW(TAG, "SPIRAM alloc for LVGL buf2 failed, using internal RAM");
        buffer_2 = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_INTERNAL);
    }
    assert(buffer_1 && buffer_2);

    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buffer_1, buffer_2, LVGL_BUF_PIXELS);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EPD_WIDTH;
    disp_drv.ver_res = EPD_HEIGHT;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);

    esp_timer_create_args_t lvgl_tick_timer_args = {};
    lvgl_tick_timer_args.callback = &example_increase_lvgl_tick;
    lvgl_tick_timer_args.name = "lvgl_tick";
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    if (!lvgl_mux) {
        lvgl_mux = xSemaphoreCreateMutex();
    }
    assert(lvgl_mux);
    xTaskCreatePinnedToCore(lvgl_port_task, "LVGL", 8 * 1024, NULL, 4, NULL, 1);
}

void user_ui_init(void)
{
    lv_obj_t* screen1 = create_screen_1_active(hasTouch, uuid_is_null);
    lv_scr_load(screen1);
    lv_timer_handler();
    Serial.printf("Screen 1 loaded. Sleep timer active (%ds).\n", INACTIVITY_TIMEOUT_MS / 1000);
}

void button_task(void *arg)
{
    for (;;) {
        EventBits_t boot_ev = xEventGroupWaitBits(boot_groups, BTN_ALL_BITS, pdTRUE, pdFALSE, pdMS_TO_TICKS(200));
        EventBits_t pwr_ev  = xEventGroupWaitBits(pwr_groups,  BTN_ALL_BITS, pdTRUE, pdFALSE, pdMS_TO_TICKS(200));

        if (boot_ev || pwr_ev) activity_feed();

        if (g_app_state == STATE_ACTIVE) {
            if (BTN_GET(boot_ev, BOOT_BIT_DOUBLE)) {
                AppEvent evt = { EVT_TOGGLE_SELECTION, 0 };
                xQueueSend(state_queue, &evt, 0);
            }
            if (BTN_GET(boot_ev, BOOT_BIT_SINGLE)) {
                AppEvent evt = { EVT_ACTIVATE_OPTION, 0 };
                xQueueSend(state_queue, &evt, 0);
            }
        } else if (g_app_state == STATE_RECORD) {
            if (BTN_GET(boot_ev, BOOT_BIT_SINGLE)) {
                AppEvent evt = { EVT_ACTIVATE_OPTION, 0 };
                xQueueSend(state_queue, &evt, 0);
            }
            if (BTN_GET(pwr_ev, PWR_BIT_SINGLE)) {
                switch_state(STATE_ACTIVE);
            }
        } else if (g_app_state == STATE_LISTENING) {
            if (BTN_GET(boot_ev, BOOT_BIT_SINGLE)) {
                AppEvent evt = { EVT_ACTIVATE_OPTION, 0 };
                xQueueSend(state_queue, &evt, 0);
            }
            if (BTN_GET(pwr_ev, PWR_BIT_SINGLE)) {
                audio_discard_recording();
                switch_state(STATE_DISCARDED);
            }
        } else if (g_app_state == STATE_CONFIRM) {
            if (BTN_GET(boot_ev, BOOT_BIT_DOUBLE)) {
                AppEvent evt = { EVT_TOGGLE_SELECTION, 0 };
                xQueueSend(state_queue, &evt, 0);
            }
            if (BTN_GET(boot_ev, BOOT_BIT_SINGLE)) {
                AppEvent evt = { EVT_ACTIVATE_OPTION, 0 };
                xQueueSend(state_queue, &evt, 0);
            }
            if (BTN_GET(pwr_ev, PWR_BIT_SINGLE)) {
                switch_state(STATE_RECORD);
            }
        }

        if (BTN_GET(boot_ev, BOOT_BIT_SINGLE)) Serial.printf("BOOT single_click\n");
        if (BTN_GET(boot_ev, BOOT_BIT_DOUBLE)) Serial.printf("BOOT double_click\n");
        if (BTN_GET(boot_ev, BOOT_BIT_LONG))   Serial.printf("BOOT long_press\n");
        if (BTN_GET(boot_ev, BOOT_BIT_UP))     Serial.printf("BOOT press_up\n");

        if (BTN_GET(pwr_ev, PWR_BIT_SINGLE))   Serial.printf("PWR single_click\n");
        if (BTN_GET(pwr_ev, PWR_BIT_DOUBLE))   Serial.printf("PWR double_click\n");
        if (BTN_GET(pwr_ev, PWR_BIT_LONG))     Serial.printf("PWR long_press\n");
        if (BTN_GET(pwr_ev, PWR_BIT_UP))       Serial.printf("PWR press_up\n");

        Serial.flush();
    }
}

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void gpio_touchint_init(void)
{
    gpio_evt_queue = xQueueCreate(3, sizeof(uint32_t));
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.pin_bit_mask = (1ULL << EPD_TP_INT_PIN);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(EPD_TP_INT_PIN, gpio_isr_handler, (void *)EPD_TP_INT_PIN);
}

bool detectTouch(void)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pin_bit_mask = (1ULL << EPD_TP_RST_PIN);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level((gpio_num_t)EPD_TP_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)EPD_TP_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)EPD_TP_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    I2cMasterBus *i2c_bus = I2cMasterBus::requestInstance(ESP32_I2C_SCL_PIN, ESP32_I2C_SDA_PIN, ESP32_I2C_DEV_NUM);

    if (i2c_bus->i2c_probe_addr(I2C_FT6336_DEV_Address) == ESP_OK) {
        I2cFt6336Dev *ft6336 = I2cFt6336Dev::requestInstance(
            i2c_bus->Get_I2cBusHandle(), I2C_FT6336_DEV_Address, EPD_WIDTH, EPD_HEIGHT);
        ft6336->Ft6336_Reset(EPD_TP_RST_PIN);
        gpio_touchint_init();
        return true;
    }
    return false;
}

void touch_task(void *arg)
{
    I2cFt6336Dev *ft6336 = I2cFt6336Dev::instance_;
    uint32_t io_num;
    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            activity_feed();
            uint16_t x, y;
            if (ft6336->GetTouchPoint(&x, &y)) {
                xEventGroupSetBits(touch_event_group, TOUCH_BIT_TAP);
                Serial.printf("Touch: (%d,%d)\n", x, y);
                Serial.flush();
                activity_feed();

                if (g_app_state == STATE_ACTIVE) {
                    int opt = -1;
                    if (uuid_is_null) {
                        if (x >= 10 && x <= 190 && y >= 80 && y <= 120) opt = 1;
                    } else {
                        if (x >= 10 && x <= 190 && y >= 52 && y <= 92) opt = 0;
                        if (x >= 10 && x <= 190 && y >= 108 && y <= 148) opt = 1;
                    }
                    if (opt >= 0) {
                        AppEvent evt = { EVT_TOUCH_OPTION, (uint8_t)opt };
                        xQueueSend(state_queue, &evt, 0);
                    }
                } else if (g_app_state == STATE_RECORD) {
                    AppEvent evt = { EVT_ACTIVATE_OPTION, 0 };
                    xQueueSend(state_queue, &evt, 0);
                } else if (g_app_state == STATE_LISTENING) {
                    AppEvent evt = { EVT_ACTIVATE_OPTION, 0 };
                    xQueueSend(state_queue, &evt, 0);
                }
            }
        }
    }
}

void battery_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(5000));
    for (;;) {
        int pct = battery_get_percentage();
        float v = battery_get_voltage();
        Serial.printf("Battery: %.2fV %d%%\n", v, pct);
        if (lvgl_lock(100)) {
            update_status_bar(NULL, wifi_is_connected(), pct);
            lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void enter_deep_sleep(void)
{
    Serial.printf("Entering deep sleep...\n");
    Serial.flush();

    if (driver) {
        driver->EPD_Init();
        driver->EPD_Display();
    }

    esp_sleep_enable_ext1_wakeup_io(
        (1ULL << BOOT_BUTTON_PIN) | (1ULL << PWR_BUTTON_PIN),
        ESP_EXT1_WAKEUP_ANY_LOW
    );
    rtc_gpio_hold_en(GPIO_NUM_17);

    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_SEC * 1000000ULL);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_deep_sleep_start();
}

void enter_deep_sleep_light(void)
{
    Serial.printf("Entering deep sleep (light)...\n");
    Serial.flush();

    esp_sleep_enable_ext1_wakeup_io(
        (1ULL << BOOT_BUTTON_PIN) | (1ULL << PWR_BUTTON_PIN),
        ESP_EXT1_WAKEUP_ANY_LOW
    );
    rtc_gpio_hold_en(GPIO_NUM_17);

    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_SEC * 1000000ULL);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_deep_sleep_start();
}

void activity_feed(void)
{
    if (sleep_timer_handle) {
        xTaskNotifyGive(sleep_timer_handle);
    }
}

void deep_sleep_timer_task(void *arg)
{
    for (;;) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(INACTIVITY_TIMEOUT_MS)) == 0) {
            if (g_app_state == STATE_LISTENING ||
                g_app_state == STATE_SENDING ||
                g_app_state == STATE_WAITING ||
                g_app_state == STATE_RESPONSE) {
                continue;
            }
            Serial.printf("Inactivity timeout, sleeping...\n");
            if (lvgl_lock(1000)) {
                status_bar_set_visible(false);
                lv_obj_t* s0 = create_screen_0_deep_sleep(sleep_counter);
                lv_scr_load(s0);
                lv_timer_handler();
                lvgl_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            enter_deep_sleep();
        }
    }
}

void display_light_init(void)
{
    board_div.POWEER_EPD_ON();
    delay(50);

    custom_lcd_spi_t driver_config = {};
    driver_config.cs = EPD_CS_PIN;
    driver_config.dc = EPD_DC_PIN;
    driver_config.rst = EPD_RST_PIN;
    driver_config.busy = EPD_BUSY_PIN;
    driver_config.mosi = EPD_MOSI_PIN;
    driver_config.scl = EPD_SCK_PIN;
    driver_config.spi_host = EPD_SPI_NUM;
    driver_config.buffer_len = 5000;

    driver = new epaper_driver_display(EPD_WIDTH, EPD_HEIGHT, driver_config);
    driver->EPD_Init();
    driver->EPD_Clear();
    driver->EPD_DisplayPartBaseImage();
    driver->EPD_Init_Partial();
}

void user_app_init(void)
{
    Serial.begin(115200);
    delay(100);
    Serial.printf("ePaperConversational v%s\n", FIRMWARE_VERSION);
    Serial.flush();
    ESP_LOGI(TAG, "ePaperConversational v%s starting...", FIRMWARE_VERSION);

    size_t psram_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t dram_size  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    Serial.printf("PSRAM free: %u  DRAM free: %u\n", psram_size, dram_size);
    Serial.flush();

    board_div.VBAT_POWER_ON();
    board_div.POWEER_EPD_ON();
    board_div.POWEER_Audio_ON();
    delay(50);

    ESP_LOGI(TAG, "Power: EPD ON, Audio ON, VBAT ON");

    custom_lcd_spi_t driver_config = {};
    driver_config.cs = EPD_CS_PIN;
    driver_config.dc = EPD_DC_PIN;
    driver_config.rst = EPD_RST_PIN;
    driver_config.busy = EPD_BUSY_PIN;
    driver_config.mosi = EPD_MOSI_PIN;
    driver_config.scl = EPD_SCK_PIN;
    driver_config.spi_host = EPD_SPI_NUM;
    driver_config.buffer_len = 5000;

    driver = new epaper_driver_display(EPD_WIDTH, EPD_HEIGHT, driver_config);

    driver->EPD_Init();
    driver->EPD_Clear();
    driver->EPD_DisplayPartBaseImage();
    driver->EPD_Init_Partial();

    ESP_LOGI(TAG, "Display init complete. Heap free: %d", esp_get_free_heap_size());
    Serial.printf("ePaperConversational v%s ready.\n", FIRMWARE_VERSION);

    state_queue = xQueueCreate(10, sizeof(AppEvent));
    user_button_init();
    xTaskCreatePinnedToCore(button_task, "btn_task", 4 * 1024, NULL, 3, NULL, 1);

    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);

    xTaskCreatePinnedToCore(state_task, "state_task", 4 * 1024, NULL, 3, NULL, 1);

    hasTouch = detectTouch();
    if (hasTouch) {
        Serial.printf("FT6336 detected. Touch enabled.\n");
        touch_event_group = xEventGroupCreate();
        xTaskCreatePinnedToCore(touch_task, "touch_task", 4 * 1024, NULL, 3, NULL, 1);
    } else {
        Serial.printf("FT6336 not detected, touch disabled.\n");
    }
    Serial.flush();

    set_i2c_bus_handle(ESP32_I2C_DEV_NUM,
        I2cMasterBus::instance_->Get_I2cBusHandle());

    wifi_init();
    xTaskCreatePinnedToCore(wifi_task, "wifi_task", 4 * 1024, NULL, 2, NULL, 1);

    battery_init();
    xTaskCreatePinnedToCore(battery_task, "bat_task", 4 * 1024, NULL, 1, NULL, 1);

    xTaskCreatePinnedToCore(deep_sleep_timer_task, "sleep_timer", 4 * 1024, NULL, 1, &sleep_timer_handle, 1);
    activity_feed();

    audio_bsp_init();

    Serial.printf("Boot count: %d  Sleep counter: %d\n", boot_count, sleep_counter);
    Serial.flush();
}
