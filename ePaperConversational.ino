#include "user_app.h"

void setup()
{
  user_app_init();
  lvgl_port_init();
}

void loop()
{
  vTaskDelay(pdMS_TO_TICKS(1000));
}
