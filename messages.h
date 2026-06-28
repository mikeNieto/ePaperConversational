#ifndef MESSAGES_H
#define MESSAGES_H

struct LangMessages {
    const char* continuar_conversacion;
    const char* nueva_conversacion;
    const char* grabar_mensaje;
    const char* escuchando;
    const char* enviando_mensaje;
    const char* mensaje_descartado;
    const char* esperando_respuesta;
    const char* durmiendo;
    const char* enviar;
    const char* cancelar;
    const char* wifi_ok;
    const char* wifi_off;
    const char* error_transcribir;
    const char* error_servidor;
};

extern const LangMessages MSG_ES;
extern LangMessages* currentLang;

#endif
