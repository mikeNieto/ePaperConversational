#include "messages.h"

const LangMessages MSG_ES = {
    .continuar_conversacion = "Continuar Conversacion",
    .nueva_conversacion     = "Nueva Conversacion",
    .grabar_mensaje         = "Grabar Mensaje",
    .escuchando             = "Escuchando...",
    .enviando_mensaje       = "Enviando Mensaje...",
    .mensaje_descartado     = "Mensaje descartado!!",
    .esperando_respuesta    = "Esperando Respuesta...",
    .durmiendo              = "Durmiendo...",
    .enviar                 = "Enviar",
    .cancelar               = "Cancelar",
    .wifi_ok                = "WIFI: OK",
    .wifi_off               = "WIFI: --",
    .error_transcribir      = "Error al transcribir",
    .error_servidor         = "Error del servidor",
};

LangMessages* currentLang = (LangMessages*)&MSG_ES;
