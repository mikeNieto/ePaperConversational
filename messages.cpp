#include "messages.h"
#include <stdint.h>
#include "esp_sleep.h"

static const LangMessages* lang_table[] = { &MSG_ES, &MSG_EN };
static const int lang_count = sizeof(lang_table) / sizeof(lang_table[0]);

RTC_DATA_ATTR int g_lang_index = 0;

const LangMessages MSG_ES = {
    .name            = "Español",
    .connecting      = "Conectando...",
    .record_message  = "Grabar Mensaje",
    .listening       = "Escuchando...",
    .transcribing    = "Transcribiendo...",
    .thinking        = "Pensando...",
    .speaking        = "Hablando...",
    .sleeping        = "Durmiendo...",
    .wifi_ok         = "WIFI: OK",
    .wifi_off        = "WIFI: --",
    .connection_error = "Error de conexion",
    .server_error    = "Error del servidor",
    .settings        = "AJUSTES",
    .language        = "Idioma",
    .wifi_label      = "WiFi",
};

const LangMessages MSG_EN = {
    .name            = "English",
    .connecting      = "Connecting...",
    .record_message  = "Record Message",
    .listening       = "Listening...",
    .transcribing    = "Transcribing...",
    .thinking        = "Thinking...",
    .speaking        = "Speaking...",
    .sleeping        = "Sleeping...",
    .wifi_ok         = "WIFI: OK",
    .wifi_off        = "WIFI: --",
    .connection_error = "Connection error",
    .server_error    = "Server error",
    .settings        = "SETTINGS",
    .language        = "Language",
    .wifi_label      = "WiFi",
};

LangMessages* currentLang = (LangMessages*)&MSG_ES;

void lang_init(void)
{
    if (g_lang_index < 0 || g_lang_index >= lang_count) g_lang_index = 0;
    currentLang = (LangMessages*)lang_table[g_lang_index];
}

void lang_toggle(void)
{
    g_lang_index = (g_lang_index + 1) % lang_count;
    currentLang = (LangMessages*)lang_table[g_lang_index];
}

const char* lang_get_name(void)
{
    return currentLang->name;
}

int lang_get_index(void)
{
    return g_lang_index;
}
