#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "user_app.h"
#include "driver/gpio.h"
#include "user_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "src/ui/screens.h"
#include "src/button_bsp/button_bsp.h"

static const char *TAG = "user_app";

epaper_driver_display *driver = NULL;
board_power_bsp_t board_div(EPD_PWR_PIN, Audio_PWR_PIN, VBAT_PWR_PIN);

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
    lv_disp_draw_buf_init(&disp_buf, buffer_1, buffer_2, EPD_WIDTH * EPD_HEIGHT);

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

    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);
    xTaskCreatePinnedToCore(lvgl_port_task, "LVGL", 8 * 1024, NULL, 4, NULL, 1);

    if (example_lvgl_lock(-1)) {
        user_ui_init();
        example_lvgl_unlock();
    }
}

void user_ui_init(void)
{
    Serial.printf("LVGL: Creating screens demo...\n");
    Serial.flush();

    lv_obj_t* screens[8];

    screens[0] = create_screen_1_active(true, true);
    screens[1] = create_screen_2_record();
    screens[2] = create_screen_2b_listening();
    screens[3] = create_screen_3_sending();
    screens[4] = create_screen_3b_discarded();
    screens[5] = create_screen_4_confirm("Hola, este es un mensaje transcrito de prueba.", true);
    screens[6] = create_screen_5_waiting();
    screens[7] = create_screen_6_response("Esta es la respuesta del agente IA. Puede tener varias lineas de texto.");

    lv_obj_t* prev = NULL;
    for (int i = 0; i < 8; i++) {
        ESP_LOGI(TAG, "Loading screen %d", i);
        lv_scr_load(screens[i]);
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(3000));
        if (prev) lv_obj_del(prev);
        prev = screens[i];
    }

    lv_obj_t* screen0 = create_screen_0_deep_sleep(3);
    lv_scr_load(screen0);
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(3000));
    if (prev) lv_obj_del(prev);

    ESP_LOGI(TAG, "Screen demo complete. Heap free: %d", esp_get_free_heap_size());
    Serial.printf("ALL SCREENS TESTED. Init OK.\n");
}

void button_task(void *arg)
{
    for (;;) {
        EventBits_t boot_ev = xEventGroupWaitBits(boot_groups, BTN_ALL_BITS, pdTRUE, pdFALSE, pdMS_TO_TICKS(200));
        EventBits_t pwr_ev  = xEventGroupWaitBits(pwr_groups,  BTN_ALL_BITS, pdTRUE, pdFALSE, pdMS_TO_TICKS(200));

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

    user_button_init();
    xTaskCreatePinnedToCore(button_task, "btn_task", 4 * 1024, NULL, 3, NULL, 1);
}
