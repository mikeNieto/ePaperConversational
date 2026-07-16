#include "messages.h"

const LangMessages MSG_ES = {
    .conectando         = "Conectando...",
    .grabar_mensaje     = "Grabar Mensaje",
    .escuchando         = "Escuchando...",
    .transcribiendo     = "Transcribiendo...",
    .pensando           = "Pensando...",
    .hablando           = "Hablando...",
    .durmiendo          = "Durmiendo...",
    .wifi_ok            = "WIFI: OK",
    .wifi_off           = "WIFI: --",
    .error_conexion     = "Error de conexion",
    .error_servidor     = "Error del servidor",
};

LangMessages* currentLang = (LangMessages*)&MSG_ES;
