#include "button_bsp.h"
#include "multi_button.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "user_config.h"

static const char *TAG_BTN = "button";

EventGroupHandle_t boot_groups;
EventGroupHandle_t pwr_groups;

static Button button1;
static Button button2;

static uint8_t read_button_GPIO(uint8_t button_id)
{
    switch (button_id) {
        case 1: return gpio_get_level((gpio_num_t)BOOT_BUTTON_PIN);
        case 2: return gpio_get_level((gpio_num_t)PWR_BUTTON_PIN);
        default: return 1;
    }
}

static void on_boot_single_click(Button* btn)
{
    xEventGroupSetBits(boot_groups, BTN_BIT(BOOT_BIT_SINGLE));
}
static void on_boot_long_press(Button* btn)
{
    xEventGroupSetBits(boot_groups, BTN_BIT(BOOT_BIT_LONG));
}
static void on_boot_press_up(Button* btn)
{
    xEventGroupSetBits(boot_groups, BTN_BIT(BOOT_BIT_UP));
}
static void on_boot_double_click(Button* btn)
{
    xEventGroupSetBits(boot_groups, BTN_BIT(BOOT_BIT_DOUBLE));
}

static void on_pwr_single_click(Button* btn)
{
    xEventGroupSetBits(pwr_groups, BTN_BIT(PWR_BIT_SINGLE));
}
static void on_pwr_long_press(Button* btn)
{
    xEventGroupSetBits(pwr_groups, BTN_BIT(PWR_BIT_LONG));
}
static void on_pwr_double_click(Button* btn)
{
    xEventGroupSetBits(pwr_groups, BTN_BIT(PWR_BIT_DOUBLE));
}
static void on_pwr_press_up(Button* btn)
{
    xEventGroupSetBits(pwr_groups, BTN_BIT(PWR_BIT_UP));
}

static void clock_task_callback(void *arg)
{
    button_ticks();
}

void user_button_init(void)
{
    boot_groups = xEventGroupCreate();
    pwr_groups = xEventGroupCreate();

    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_conf.mode = GPIO_MODE_INPUT;
    gpio_conf.pin_bit_mask = (0x1ULL << BOOT_BUTTON_PIN) | (0x1ULL << PWR_BUTTON_PIN);
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    button_init(&button1, read_button_GPIO, 0, 1);
    button_attach(&button1, BTN_SINGLE_CLICK,     on_boot_single_click);
    button_attach(&button1, BTN_LONG_PRESS_START, on_boot_long_press);
    button_attach(&button1, BTN_PRESS_UP,          on_boot_press_up);
    button_attach(&button1, BTN_DOUBLE_CLICK,      on_boot_double_click);

    button_init(&button2, read_button_GPIO, 0, 2);
    button_attach(&button2, BTN_SINGLE_CLICK,     on_pwr_single_click);
    button_attach(&button2, BTN_LONG_PRESS_START, on_pwr_long_press);
    button_attach(&button2, BTN_DOUBLE_CLICK,      on_pwr_double_click);
    button_attach(&button2, BTN_PRESS_UP,          on_pwr_press_up);

    esp_timer_create_args_t clock_tick_timer_args = {};
    clock_tick_timer_args.callback = &clock_task_callback;
    clock_tick_timer_args.name = "btn_tick";
    esp_timer_handle_t clock_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&clock_tick_timer_args, &clock_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(clock_tick_timer, 5 * 1000));

    button_start(&button1);
    button_start(&button2);
    ESP_LOGI(TAG_BTN, "Buttons initialized: BOOT(GPIO%d) PWR(GPIO%d)",
             BOOT_BUTTON_PIN, PWR_BUTTON_PIN);
}
