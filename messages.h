#ifndef MESSAGES_H
#define MESSAGES_H

struct LangMessages {
    const char* name;
    const char* connecting;
    const char* record_message;
    const char* listening;
    const char* transcribing;
    const char* thinking;
    const char* speaking;
    const char* sleeping;
    const char* wifi_ok;
    const char* wifi_off;
    const char* connection_error;
    const char* server_error;
    const char* settings;
    const char* language;
    const char* wifi_label;
};

extern const LangMessages MSG_ES;
extern const LangMessages MSG_EN;
extern LangMessages* currentLang;

void lang_init(void);
void lang_toggle(void);
const char* lang_get_name(void);
int lang_get_index(void);

#endif
