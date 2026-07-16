#ifndef MESSAGES_H
#define MESSAGES_H

struct LangMessages {
    const char* conectando;
    const char* grabar_mensaje;
    const char* escuchando;
    const char* transcribiendo;
    const char* pensando;
    const char* hablando;
    const char* durmiendo;
    const char* wifi_ok;
    const char* wifi_off;
    const char* error_conexion;
    const char* error_servidor;
};

extern const LangMessages MSG_ES;
extern LangMessages* currentLang;

#endif
