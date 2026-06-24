# Plan de Implementacion Iterativo e Incremental

## Proyecto: ePaperConversational — Firmware ESP32-S3-Touch-ePaper-1.54

Basado en `TechnicalSpec.md` v1.0 y codigo de referencia en `ESP32-S3-ePaper-1.54/02_Example/`.

---

## Metodologia

Cada historia de usuario (HU) produce un incremento **verificable e independiente** del firmware. Las historias se implementan en orden secuencial; cada una se prueba y valida antes de pasar a la siguiente. Al final de cada HU, el firmware compila, se flashea y demuestra la funcionalidad descrita.

### Criterios de aceptacion por historia

Cada HU incluye:
- **Entregables**: archivos creados o modificados.
- **Prueba de validacion**: pasos concretos para verificar que la HU funciona.
- **Dependencias**: historias previas requeridas.

### Stack tecnologico
- **IDE**: Arduino (platformio o arduino-cli compatible)
- **SO**: FreeRTOS (nativo en ESP32-S3)
- **UI**: LVGL v8.4
- **Audio**: esp_codec_dev + minimp3 (single-header C)
- **Red**: WiFi + HTTPClient (Arduino built-in)
- **Memoria**: PSRAM para buffers grandes, RTC_DATA_ATTR para variables persistentes

---

## Historia de Usuario 1: Fundacion del Proyecto y Display E-Paper

**Objetivo:** Crear la estructura base del proyecto, inicializar todos los GPIOs de potencia, el bus SPI, y lograr que el display e-paper funcione con full refresh y partial refresh.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `ePaperConversational.ino` | Punto de entrada (setup/loop) |
| `user_config.h` | Pines, constantes de hardware, version del firmware |
| `user_app.h` / `user_app.cpp` | `user_app_init()`: secuencia de inicializacion completa |
| `src/display/epaper_driver_bsp.h` | Clase `epaper_driver_display` |
| `src/display/epaper_driver_bsp.cpp` | Driver SPI, LUTs, full/partial refresh, buffer 1bpp |
| `src/power/board_power_bsp.h` | Clase `board_power_bsp_t` |
| `src/power/board_power_bsp.cpp` | Control GPIOs: EPD(6), Audio(42), VBAT(17) |

### Implementacion
1. Copiar/adaptar `epaper_driver_bsp` y `board_power_bsp` del ejemplo `07_BATT_PWR_Test`.
2. `user_config.h`: definir `EPD_WIDTH=200`, `EPD_HEIGHT=200`, todos los pines del TechnicalSpec seccion 9.
3. `user_app_init()`:
   - Iniciar Serial (115200).
   - `VBAT_POWER_ON()`, `POWEER_EPD_ON()`, `POWEER_Audio_ON()`.
   - Inicializar SPI2_HOST a 40 MHz.
   - Crear `epaper_driver_display`.
   - `EPD_Init()` → `EPD_Clear()` → `EPD_DisplayPartBaseImage()` → `EPD_Init_Partial()`.
4. Dibujar un patron de prueba (rectangulos alternantes blanco/negro en el buffer, `EPD_DisplayPart()`).
5. `FIRMWARE_VERSION "1.0.0"` impreso por Serial.

### Prueba de validacion
- Compilar y flashear. El display muestra el patron de prueba. Serial muestra "ePaperConversational v1.0.0" y logs de inicializacion.
- Verificar que `EPD_Clear()` deja la pantalla blanca tras cada test.
- Verificar partial refresh funcional (cambiar algunos pixeles sin parpadeo completo).

---

## Historia de Usuario 2: Integracion LVGL y Renderizado de Pantallas

**Objetivo:** Integrar LVGL v8, crear el flush callback para e-paper, implementar el patron de creacion/destruccion de pantallas (`lv_obj_create(NULL)` + `lv_scr_load()` + `lv_obj_del()`), y renderizar una pantalla simple con texto centrado.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `user_app.cpp` (modificar) | Agregar `lvgl_port_init()` y tarea LVGL |
| `src/ui/screens.h` | Declaracion de funciones creadoras de pantallas |
| `src/ui/screens.cpp` | Implementacion de cada pantalla como funcion |
| `src/ui/status_bar.h` | Declaracion `create_status_bar()` |
| `src/ui/status_bar.cpp` | Widget de barra de estado (placeholder sin WiFi/Bateria aun) |

### Implementacion
1. Configurar LVGL:
   - 2 buffers de `EPD_WIDTH * EPD_HEIGHT * 2` bytes en SPIRAM (80 KB c/u).
   - `lv_disp_draw_buf_init()`, `lv_disp_drv_init()`, `full_refresh = 1`.
   - Flush callback: convertir RGB565 → 1-bit (threshold en `0x7FFF`), escribir pixeles, `EPD_DisplayPart()`, `lv_disp_flush_ready()`.
   - Timer de tick de 5ms (`esp_timer`).
   - Tarea LVGL en Core 1, prioridad 4, stack 8192, con mutex.
2. Implementar `create_status_bar(screen, wifiOk, batteryPct)` — renderiza placeholder con labels.
3. Implementar `create_screen_X()` para pantallas 1, 2, 2b, 3, 3b, 4, 5, 6 con textos estaticos.
4. Demo: cargar cada pantalla por 3 segundos en secuencia (`lv_scr_load()`), destruyendo la anterior (`lv_obj_del()`).

### Prueba de validacion
- El display muestra secuencialmente cada una de las 8 pantallas con sus textos centrados y la barra de estado (valores placeholder).
- No hay crashes de memoria (verificar heap libre por Serial).
- Las transiciones entre pantallas son limpias, sin ghosting excesivo.

---

## Historia de Usuario 3: Sistema de Botones Fisicos

**Objetivo:** Integrar la libreria `multi_button`, configurar BOOT (GPIO 0) y PWR (GPIO 18), generar eventos via FreeRTOS Event Groups, y consumirlos desde una tarea que imprime el evento por Serial.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `src/button_bsp/button_bsp.h` | API de botones |
| `src/button_bsp/button_bsp.cpp` | Init, timer 5ms, callbacks, event groups |
| `src/button_bsp/multi_button.h` | Libreria multi-press |
| `src/button_bsp/multi_button.c` | Implementacion multi-press |

### Implementacion
1. Copiar/adaptar `multi_button` y `button_bsp` del ejemplo `07_BATT_PWR_Test`.
2. Event bits:
   - BOOT: BIT0=singe_click, BIT1=long_press, BIT2=press_up, BIT3=double_click
   - PWR: BIT0=single_click, BIT1=long_press, BIT2=double_click, BIT3=press_up
3. Crear `button_task` (Core 1, prio 3, stack 4096) que hace polling de ambos event groups e imprime el evento.
4. Integrar en `user_app_init()` la llamada a `user_button_init()`.

### Prueba de validacion
- Pulsar BOOT single click → Serial muestra "BOOT single_click".
- Pulsar BOOT doble click → Serial muestra "BOOT double_click".
- Pulsar BOOT long press → Serial muestra "BOOT long_press".
- Pulsar PWR single click → Serial muestra "PWR single_click".
- Todos los eventos se detectan consistentemente.

---

## Historia de Usuario 4: Deteccion de Touch (FT6336)

**Objetivo:** Detectar en runtime si el FT6336 esta presente en I2C 0x38. Si esta, inicializar el driver, configurar la interrupcion GPIO 21, y leer coordenadas de toque. Si no esta, establecer flag `hasTouch = false` para que las pantallas se adapten.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `src/i2c_bsp/i2c_bsp.h` / `.cpp` | Inicializacion del bus I2C_NUM_0 (SDA=47, SCL=48, 400kHz) |
| `src/touch_bsp/ft6336_bsp.h` | Clase `I2cFt6336Dev` |
| `src/touch_bsp/ft6336_bsp.cpp` | Reset, lectura de registros 0x02-0x05, ISR, cola de eventos |
| `user_app.cpp` (modificar) | Agregar `detectTouch()`, `touch_task` |

### Implementacion
1. `i2c_master_Init()`: inicializar bus I2C_NUM_0. Agregar dispositivos RTC (0x51), SHTC3 (0x70).
2. `detectTouch()`: intentar leer registro 0x00 del FT6336 en 0x38. Si responde → `hasTouch = true`, inicializar `I2cFt6336Dev`, reset hardware en GPIO 7, configurar ISR en GPIO 21 (NEGEDGE).
3. `touch_task` (Core 1, prio 3, stack 4096): esperar en cola `gpio_evt_queue`, leer coordenadas, publicar en `touch_event_group` (evento TAP + coordenadas).
4. Si `hasTouch == false`, no crear `touch_task` y las pantallas con botones usaran solo navegacion por botones fisicos.

### Prueba de validacion
- Con placa que SI tiene FT6336: Serial muestra "FT6336 detected". Tocar la pantalla imprime coordenadas X,Y.
- Con placa que NO tiene FT6336 (o simulando desconexion): Serial muestra "FT6336 not detected, touch disabled". `hasTouch = false`.

---

## Historia de Usuario 5: WiFi Management

**Objetivo:** Conectar a WiFi en modo STA al iniciar, mantener la conexion, reconectar si se pierde, y publicar el estado (conectado/desconectado) via event group para que la barra de estado lo refleje.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `wifi_bsp.h` | API: `wifi_init()`, `wifi_is_connected()`, event group |
| `wifi_bsp.cpp` | WiFi STA connect, reconnect timer, WiFi event handler |
| `user_config.h` (modificar) | Agregar `WIFI_SSID`, `WIFI_PASSWORD` |

### Implementacion
1. `wifi_init()`:
   - `WiFi.mode(WIFI_STA)`, `WiFi.begin(SSID, PASSWORD)`.
   - Esperar hasta 15 segundos para conexion.
   - Timeout → publicar `WIFI_DISCONNECTED` en event group.
   - Conectado → publicar `WIFI_CONNECTED`.
2. `wifi_task` (Core 1, prio 2, stack 4096):
   - Monitorear `WiFi.status()`, reconectar si se pierde (cada 10s).
   - Publicar eventos en `wifi_event_group`.
3. Actualizar `create_status_bar()`: label "WIFI: OK" o "WIFI: --" segun estado del event group.

### Prueba de validacion
- Al iniciar con WiFi disponible: barra muestra "WIFI: OK". Serial muestra IP asignada.
- Al iniciar sin WiFi: barra muestra "WIFI: --". Reconecta cuando WiFi aparece.
- Desconectar WiFi durante operacion: barra cambia a "WIFI: --" y vuelve a "WIFI: OK" al reconectar.

---

## Historia de Usuario 6: Medicion de Bateria y Barra de Estado Completa

**Objetivo:** Leer el ADC de bateria (GPIO 4, ADC1_CH3), calcular voltaje y porcentaje, renderizar el icono de bateria en la barra de estado, y actualizar periodicamente.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `src/battery/battery_bsp.h` | API: `battery_init()`, `battery_get_voltage()`, `battery_get_percentage()` |
| `src/battery/battery_bsp.cpp` | ADC oneshot, calibracion, lectura, calculo porcentaje |
| `src/ui/status_bar.cpp` (modificar) | Icono de bateria grafico + porcentaje actualizado |

### Implementacion
1. `battery_init()`: configurar ADC1_CH3, atenuacion 12dB, 12 bits, calibracion curve-fitting.
2. `battery_get_voltage()`: `adc_oneshot_read()` + `adc_cali_raw_to_voltage()` + `* 2.0` (divisor 1:2).
3. `battery_get_percentage()`: `(voltage - 3.0) / (4.12 - 3.0) * 100`, clamp a [0, 100].
4. Actualizar barra de estado:
   - Label "WIFI: OK/--" izquierda.
   - Icono bateria (4 barras) + "XX%" derecha.
   - Separador horizontal con `lv_line`.
5. Tarea que cada 30s lee bateria y actualiza labels de la barra.

### Prueba de validacion
- Barra de estado muestra WiFi y bateria correctamente en cada pantalla.
- Desconectar USB-C y medir con bateria: el porcentaje refleja el voltaje real.
- El icono de bateria cambia segun el nivel.

---

## Historia de Usuario 7: Pantalla 0 — Deep Sleep con RTC Memory

**Objetivo:** Implementar el estado de deep sleep: contador de inactividad (60s), entrada a deep sleep tras inactividad, full refresh del display antes de dormir, auto-despertar cada 60 minutos con incremento de contador, y despertar por boton del usuario (PWR/BOOT) con reset del contador.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `user_app.cpp` (modificar) | Logica de deep sleep: `enter_deep_sleep()`, `deep_sleep_timer_task` |
| `user_config.h` (modificar) | `INACTIVITY_TIMEOUT_MS = 60000`, `SLEEP_DURATION_MIN = 60` |
| `src/ui/screens.cpp` (modificar) | `create_screen_0_deep_sleep(N)` funcional |

### Implementacion
1. Variables `RTC_DATA_ATTR`:
   ```cpp
   RTC_DATA_ATTR char conversation_uuid[37] = {0};
   RTC_DATA_ATTR bool uuid_is_null = true;
   RTC_DATA_ATTR int sleep_counter = 0;
   RTC_DATA_ATTR int boot_count = 0;
   ```
2. En `setup()`:
   - Leer `esp_sleep_get_wakeup_cause()`.
   - Si `ESP_SLEEP_WAKEUP_EXT1` (PWR o BOOT): reinicializar perifericos, `sleep_counter = 0`, mostrar Pantalla 1.
   - Si `ESP_SLEEP_WAKEUP_TIMER`: solo display, limpiar pantalla (full refresh), incrementar `sleep_counter`, mostrar Pantalla 0, volver a `enter_deep_sleep()`.
   - Si primer boot (`boot_count == 0`): inicializar `uuid_is_null = true`, `sleep_counter = 0`, camino normal.
3. `enter_deep_sleep()`:
   - Full refresh del display (pantalla completamente blanca con `EPD_Display()`).
   - `esp_sleep_enable_ext1_wakeup_io(GPIO0|GPIO18, ESP_EXT1_WAKEUP_ANY_LOW)`.
   - `rtc_gpio_hold_en(GPIO_NUM_17)` para VBAT_PWR_PIN.
   - `esp_sleep_enable_timer_wakeup(60 * 60 * 1000000ULL)`.
   - `esp_deep_sleep_start()`.
4. `deep_sleep_timer_task`: contador de 60s, al expirar llama `enter_deep_sleep()`. Se pausa en pantallas 2b, 3, 5, 6.
5. `create_screen_0_deep_sleep(int N)`: texto "Durmiendo... N" centrado, sin barra de estado.

### Prueba de validacion
- Esperar 60s sin tocar nada → el display se limpia completamente a blanco y el dispositivo entra en deep sleep.
- Al despertar con BOOT o PWR: `sleep_counter` se resetea a 0, se muestra Pantalla 1.
- Al auto-despertar (usar timer corto de prueba, ej. 1 min en vez de 60): se muestra "Durmiendo... 1", luego "Durmiendo... 2", etc. No se enciende WiFi.
- Verificar que GPIO 17 mantiene estado durante deep sleep.

---

## Historia de Usuario 8: Pantalla 1 — Activo (Seleccion de Conversacion)

**Objetivo:** Implementar la pantalla de seleccion de conversacion con dos botones (o uno si UUID nulo), navegacion por touch y botones fisicos, y generacion de UUID para nueva conversacion.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `src/ui/screens.cpp` (modificar) | `create_screen_1_active(hasTouch, uuidIsNull)` completa |
| `user_app.cpp` (modificar) | Logica de navegacion desde Pantalla 1, generacion UUID |

### Implementacion
1. Si `uuidIsNull`: solo un boton "Nueva Conversacion", centrado.
   Si `!uuidIsNull`: dos botones "Continuar Conversacion" (seleccionado por defecto, resaltado) y "Nueva Conversacion".
2. Interaccion tactil (si `hasTouch`):
   - Toque en boton → activa la opcion, transita a Pantalla 2.
3. Interaccion por botones:
   - BOOT double click: toggle entre los dos botones (cambiar resaltado).
   - BOOT single click: activa opcion seleccionada.
   - PWR: sin efecto en esta pantalla.
4. Al activar "Nueva Conversacion": generar UUID con `esp_random()` (formato 8-4-4-4-12 hex), guardar en `RTC_DATA_ATTR`, `uuid_is_null = false`.
5. Al activar "Continuar Conversacion": usar UUID existente.
6. Transitar a Pantalla 2 con `lv_scr_load()`.

### Prueba de validacion
- Primer boot (UUID nulo): solo se muestra "Nueva Conversacion". Al seleccionarlo, se genera UUID (visible por Serial) y se transita a Pantalla 2.
- Segundo boot (UUID existe): se muestran ambas opciones. "Continuar Conversacion" resaltado. BOOT doble click cambia la seleccion. BOOT simple click activa la opcion y transita a Pantalla 2.
- Si hasTouch=true: tocar un boton lo activa directamente.

---

## Historia de Usuario 9: Codec de Audio e Inicializacion I2S

**Objetivo:** Inicializar el codec ES8311 via esp_codec_dev, configurar I2S para entrada (microfono) y salida (parlante), verificar que el hardware de audio responde.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `src/codec_board/board_cfg.h` | Configuracion de pines I2S/I2C para S3_ePaper_1_54 |
| `src/codec_board/codec_board.h` / `.cpp` | Inicializacion de codec (wrapper de esp_codec_dev) |
| `src/codec_board/codec_init.h` / `.c` | Funciones `init_codec()`, `get_playback_handle()`, `get_record_handle()` |
| `src/esp_codec_dev/` (directorio) | Libreria esp_codec_dev copiada de `08_Audio_Test` |
| `audio_bsp.h` / `audio_bsp.cpp` | API de alto nivel: `audio_bsp_init()`, `audio_play_init()` |

### Implementacion
1. Copiar `codec_board` y `audio_bsp` de `08_Audio_Test` y adaptar.
2. Configurar `board_cfg.txt` o `board_cfg.h` con pines:
   - I2S: MCLK=14, BCLK=15, WS=38, DIN=16, DOUT=45
   - I2C: SDA=47, SCL=48
   - Codec: ES8311, PA=46, pa_gain=6
3. `audio_bsp_init()` → `init_codec()` → crea handles de playback y record.
4. `audio_play_init()`:
   - `esp_codec_dev_set_out_vol(playback, 100.0)`.
   - `esp_codec_dev_set_in_gain(record, 45.0)`.
   - Abrir con sample_rate=16000, channel=2, bits_per_sample=16.
5. Verificar inicializacion exitosa por Serial.

### Prueba de validacion
- Serial muestra "Audio codec initialized: ES8311 OK".
- `esp_codec_dev_open()` en ambos handles retorna ESP_OK.
- No hay crashes ni timeouts en I2C al configurar el codec.
- (Opcional) Hacer eco de audio: leer 1024 bytes del microfono y escribirlos al parlante.

---

## Historia de Usuario 10: Captura de Audio y Codificacion WAV

**Objetivo:** Implementar la grabacion de audio desde el microfono a un buffer en PSRAM, con duracion maxima de 30 segundos, y construccion del header WAV en memoria para envio HTTP.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `audio_bsp.cpp` (modificar) | `audio_start_recording()`, `audio_stop_recording()`, `audio_get_wav_buffer()`, `audio_get_wav_size()` |
| `audio_bsp.h` (modificar) | Declaraciones correspondientes |

### Implementacion
1. `audio_start_recording()`:
   - Alocar buffer en PSRAM: `16000 * 2ch * 2bytes * 30s = 1,920,000 bytes`.
   - Crear tarea `audio_task` (Core 1, prio 5, stack 8192) que lee de `esp_codec_dev_read(record, buf, chunk_size)` en chunks y acumula en el buffer PSRAM.
   - Si se llena el buffer (30s), detener automaticamente.
   - Flag `recording_active = true`.
2. `audio_stop_recording()`:
   - Detener tarea de grabacion.
   - `recording_active = false`.
   - Retornar bytes grabados.
3. `audio_build_wav_header()`: escribir header WAV de 44 bytes al inicio del buffer:
   - RIFF header, fmt chunk (PCM, 16000 Hz, 16-bit, 2 canales), data chunk.
4. `audio_get_wav_buffer()` y `audio_get_wav_size()`: retornar puntero al buffer WAV completo (header + datos) y tamano total.
5. `audio_discard_recording()`: liberar buffer.

### Prueba de validacion
- Iniciar grabacion (por Serial o boton), hablar, detener. Serial muestra "Recording: X bytes captured".
- Verificar que el buffer WAV tiene header correcto (inspeccionar primeros 44 bytes: deben decir "RIFF", "WAVE", "fmt ", "data").
- Grabar 30s: se detiene automaticamente, Serial muestra "Recording timeout, discarded".
- Heap libre antes/despues: sin fugas de memoria.

---

## Historia de Usuario 11: Pantallas 2 y 2b — Grabar Mensaje y Escuchando

**Objetivo:** Implementar las pantallas de grabacion con interaccion completa (iniciar/detener/descartar por botones y touch), timeout de 30s, y transiciones a Pantalla 3 o 3b.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `src/ui/screens.cpp` (modificar) | `create_screen_2_record()`, `create_screen_2b_listening()` completas |
| `user_app.cpp` (modificar) | Logica de transiciones 2↔2b↔3↔3b, machine de estados parcial |

### Implementacion
1. `create_screen_2_record()`: texto "Grabar Mensaje" centrado + barra de estado.
2. `create_screen_2b_listening()`: texto "Escuchando..." centrado + barra de estado.
3. Logica en Pantalla 2:
   - BOOT click simple → `audio_start_recording()`, transitar a 2b.
   - PWR click simple → transitar a Pantalla 1.
   - Touch tap → igual que BOOT click.
4. Logica en Pantalla 2b:
   - BOOT click simple → `audio_stop_recording()`, construir WAV, transitar a Pantalla 3.
   - PWR click simple → `audio_discard_recording()`, transitar a 3b.
   - Touch tap → igual que BOOT click.
   - Timeout 30s → `audio_discard_recording()`, transitar a 3b.
5. Pausar contador de inactividad durante Pantalla 2b.

### Prueba de validacion
- Desde Pantalla 1, seleccionar opcion → Pantalla 2 ("Grabar Mensaje").
- BOOT click o tocar pantalla → Pantalla 2b ("Escuchando...").
- BOOT click durante grabacion → Pantalla 3 ("Enviando Mensaje...").
- PWR click durante grabacion → Pantalla 3b ("Mensaje descartado!!").
- Esperar 30s grabando → transicion automatica a Pantalla 3b.
- PWR en Pantalla 2 → vuelve a Pantalla 1.

---

## Historia de Usuario 12: Cliente API REST

**Objetivo:** Implementar el cliente HTTP para llamar a los 4 endpoints del backend (health, transcribe, chat/message, download MP3), con manejo de timeouts y errores.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `api_client.h` | Declaraciones: `api_health_check()`, `api_transcribe_audio()`, `api_send_message()`, `api_download_audio()` |
| `api_client.cpp` | Implementaciones con `HTTPClient` |
| `user_config.h` (modificar) | `API_BASE_URL`, `USER_ID` |

### Implementacion
1. `api_health_check()`:
   - `GET /api/health`
   - Timeout 5s.
   - Retorna `true` si 200 OK.
2. `api_transcribe_audio(uint8_t* wav_buffer, size_t wav_size, char* transcribed_text, size_t text_maxlen)`:
   - `POST /api/audio/transcribe`, Content-Type: `multipart/form-data`.
   - Adjuntar buffer WAV como campo "file".
   - Timeout 20s.
   - Parsear JSON respuesta, extraer campo `text`.
   - Retorna `true` si 200 OK, copia `text` a `transcribed_text`.
3. `api_send_message(const char* thread_id, const char* message, char* agent_text, size_t text_maxlen, char* audio_url, size_t url_maxlen)`:
   - `POST /api/chat/message`, Content-Type: `application/json`.
   - Body: `{"user_id":"...", "thread_id":"...", "message":"...", "response_audio":true}`.
   - Timeout 20s.
   - Parsear JSON respuesta, extraer `agent_text` y `audio_url`.
   - Retorna `true` si 200 OK.
4. `api_download_audio(const char* audio_url, uint8_t** mp3_buffer, size_t* mp3_size)`:
   - `GET {audio_url}` (URL relativa, completar con `API_BASE_URL`).
   - Timeout 20s.
   - Alocar buffer en PSRAM y copiar respuesta binaria.
   - Retorna `true` si 200 OK.
5. Todas las funciones manejan errores HTTP (timeout, 4xx, 5xx) retornando `false` e imprimiendo el error por Serial.

### Prueba de validacion
- Con backend de prueba corriendo:
  - `api_health_check()` retorna `true`.
  - Enviar un WAV valido → `api_transcribe_audio()` retorna texto transcrito.
  - Enviar texto → `api_send_message()` retorna `agent_text` y `audio_url`.
  - Con `audio_url`, `api_download_audio()` descarga MP3 a buffer.
- Con backend apagado: todas retornan `false` y Serial muestra codigos de error.

---

## Historia de Usuario 13: Pantallas 3 y 3b — Enviando y Mensaje Descartado

**Objetivo:** Implementar la pantalla de envio (llamada a API STT mientras se muestra "Enviando Mensaje..."), y la pantalla de descarte temporal (1.5s). Pausar contador de inactividad en Pantalla 3.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `src/ui/screens.cpp` (modificar) | `create_screen_3_sending()`, `create_screen_3b_discarded()` completas |
| `user_app.cpp` (modificar) | Logica asincrona de envio STT, timeout de 1.5s en 3b |

### Implementacion
1. `create_screen_3_sending()`: texto "Enviando Mensaje..." centrado + barra de estado.
2. `create_screen_3b_discarded()`: texto "Mensaje descartado!!" centrado + barra de estado.
3. Logica en Pantalla 3:
   - Al entrar: pausar contador de inactividad.
   - Llamar `api_transcribe_audio()` con el buffer WAV.
   - Si 200 OK: guardar texto transcrito, transitar a Pantalla 4.
   - Si error: mostrar mensaje de error 2s, transitar a Pantalla 2.
4. Logica en Pantalla 3b:
   - `vTaskDelay(1500 / portTICK_PERIOD_MS)`.
   - Transitar a Pantalla 2.
5. La llamada API debe ejecutarse en una tarea aparte para que la UI se renderice correctamente (la pantalla 3 se muestra inmediatamente, luego se espera la respuesta).

### Prueba de validacion
- Tras grabar y enviar: Pantalla 3 ("Enviando Mensaje...") se muestra durante la llamada HTTP. La pantalla no se congela.
- Respuesta exitosa: transita a Pantalla 4 con el texto transcrito.
- Error HTTP: mensaje de error 2s, vuelve a Pantalla 2.
- Descartar grabacion: Pantalla 3b se muestra exactamente 1.5s, luego vuelve a Pantalla 2.

---

## Historia de Usuario 14: Pantalla 4 — Confirmacion de Transcripcion

**Objetivo:** Mostrar el texto transcrito con botones "Enviar" y "Cancelar", navegacion por touch y botones, y envio del mensaje al chat.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `src/ui/screens.cpp` (modificar) | `create_screen_4_confirm(text, hasTouch)` completa |

### Implementacion
1. Mostrar texto transcrito (multilinea, centrado, con `lv_label` y `LV_LABEL_LONG_WRAP`).
2. Dos botones: "Enviar" (seleccionado por defecto) y "Cancelar".
3. Interaccion tactil: toque en boton activa la opcion.
4. Interaccion por botones fisicos:
   - BOOT doble click: toggle Enviar/Cancelar.
   - BOOT simple click: activa opcion seleccionada.
   - PWR simple click: equivalente a Cancelar.
5. "Enviar" → transitar a Pantalla 5.
6. "Cancelar" → transitar a Pantalla 2 (descartar transcripcion).

### Prueba de validacion
- Texto transcrito se muestra correctamente (acentos, caracteres especiales si los hay).
- BOOT doble click cambia seleccion visual entre Enviar y Cancelar.
- "Cancelar" o PWR → vuelve a Pantalla 2.
- "Enviar" → transita a Pantalla 5 ("Esperando Respuesta...").

---

## Historia de Usuario 15: Reproduccion de Audio MP3 via minimp3

**Objetivo:** Integrar minimp3 como decodificador MP3→PCM, implementar el pipeline de reproduccion desde buffer PSRAM, y control de reproduccion (play, stop, replay desde inicio sin re-descargar).

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `minimp3.h` | Libreria single-header (descargada de github.com/lieff/minimp3) |
| `audio_bsp.cpp` (modificar) | `audio_play_mp3_start()`, `audio_play_mp3_stop()`, `audio_play_mp3_replay()` |
| `audio_bsp.h` (modificar) | Declaraciones |

### Implementacion
1. `audio_play_mp3_start(uint8_t* mp3_buffer, size_t mp3_size)`:
   - Inicializar `mp3dec_t dec` con `mp3dec_init()`.
   - Abrir codec de playback con sample_rate del MP3 (leido del primer frame).
   - Iniciar tarea/bucle de decodificacion:
     a. `mp3dec_decode_frame(&dec, ptr, remaining, pcm_buf, &info)`.
     b. `esp_codec_dev_write(playback, pcm_buf, info.audio_bytes)`.
     c. Avanzar `ptr += info.frame_bytes`.
     d. Verificar flag de stop/replay entre frames.
2. `audio_play_mp3_stop()`:
   - Setear flag de stop. Esperar a que el bucle termine.
   - `esp_codec_dev_close(playback)`.
3. `audio_play_mp3_replay()`:
   - No re-descargar. Reiniciar puntero al inicio del buffer MP3.
   - Re-inicializar decodificador con `mp3dec_init()`.
   - Continuar bucle desde el principio.
4. Manejar cambio de sample rate: `esp_codec_dev_close()` y `esp_codec_dev_open()` con el nuevo sample rate si es necesario.

### Prueba de validacion
- Cargar un archivo MP3 de prueba en PSRAM (desde flash o Serial). Reproducirlo por el parlante.
- Durante reproduccion, llamar `audio_play_mp3_stop()`: se detiene inmediatamente.
- Durante reproduccion, llamar `audio_play_mp3_replay()`: reinicia desde el principio.
- Verificar que no hay glitches ni ruido entre frames.

---

## Historia de Usuario 16: Pantallas 5 y 6 — Esperando y Mostrando Respuesta

**Objetivo:** Completar el flujo conversacional: enviar mensaje al chat, esperar respuesta (Pantalla 5), descargar y reproducir audio TTS mientras se muestra la respuesta del agente (Pantalla 6), y gestionar la interaccion en Pantalla 6 (replay con BOOT, detener con PWR, fin automatico).

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `src/ui/screens.cpp` (modificar) | `create_screen_5_waiting()`, `create_screen_6_response(text)` completas |
| `user_app.cpp` (modificar) | Logica de envio chat, descarga MP3, reproduccion, transiciones |

### Implementacion
1. `create_screen_5_waiting()`: texto "Esperando Respuesta..." + barra de estado.
2. `create_screen_6_response(agentText)`: texto del agente centrado multilinea + barra de estado.
3. Logica Pantalla 5:
   - Pausar contador de inactividad.
   - Llamar `api_send_message()` con el texto transcrito y thread_id.
   - Si error: mostrar error 2s, volver a Pantalla 2.
   - Si OK: guardar `agent_text` y `audio_url`. Llamar `api_download_audio(audio_url)`.
   - Si descarga falla: mostrar error 2s, volver a Pantalla 2.
   - Si descarga OK: transitar a Pantalla 6.
4. Logica Pantalla 6:
   - Pausar contador de inactividad mientras se reproduce audio.
   - Iniciar `audio_play_mp3_start(mp3_buffer, mp3_size)`.
   - BOOT click / touch → `audio_play_mp3_replay()`.
   - PWR click → `audio_play_mp3_stop()`, liberar buffers, transitar a Pantalla 2.
   - Fin de reproduccion → liberar buffers MP3, texto STT, texto agente, audio_url. Reanudar contador de inactividad. Transitar a Pantalla 2.

### Prueba de validacion
- Enviar mensaje → Pantalla 5 se muestra durante la espera. Al recibir respuesta, se muestra el texto del agente y se reproduce el audio.
- BOOT durante reproduccion: el audio reinicia desde el principio.
- PWR durante reproduccion: el audio se detiene, se liberan buffers, vuelve a Pantalla 2.
- Fin de reproduccion: transicion automatica a Pantalla 2 lista para siguiente mensaje.
- Verificar con Serial que los buffers se liberan correctamente (heap libre vuelve al valor inicial).

---

## Historia de Usuario 17: Maquina de Estados Principal Completa

**Objetivo:** Integrar todos los estados en una maquina de estados central con cola de eventos FreeRTOS, gestionar transiciones, limpieza de buffers entre ciclos, y el contador de inactividad global.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `user_app.cpp` (modificar) | `main_state_task` con switch de estados, cola `state_queue` |
| `user_app.h` (modificar) | Enumeracion `AppState` (0 a 7 + 2b, 3b) |

### Implementacion
1. `AppState` enum: `DEEP_SLEEP(0), ACTIVE(1), RECORD(2), LISTENING(2b), SENDING(3), CONFIRM(4), WAITING(5), RESPONSE(6), DISCARDED(3b)`.
2. `main_state_task` (Core 1, prio 3, stack 8192):
   - Bucle infinito esperando en `state_queue` con `xQueueReceive()`.
   - Switch sobre `AppState`:
     - Cada case: `lv_scr_load(screen)`, `lv_obj_del(old_screen)`.
     - Configurar callbacks de botones/touch especificos del estado.
     - Gestionar pausa/reanudacion del contador de inactividad.
3. `switch_state(AppState new_state)`: enviar a la cola y esperar procesamiento.
4. Logica de limpieza al salir de Pantalla 6:
   - `free(mp3_buffer)`, `free(stt_text)`, `free(agent_text)`, `free(audio_url)`.
5. `inactivity_timer_task` (Core 1, prio 1, stack 4096):
   - Contador de 60s. Recibe notificaciones para pausar/reanudar.
   - Al expirar: enviar `DEEP_SLEEP` a la cola de estados.

### Prueba de validacion
- Flujo completo: desde Pantalla 1, grabar, enviar, confirmar, recibir respuesta, escuchar audio, continuar conversacion.
- Inactividad 60s en Pantalla 1 o 2: transita a deep sleep.
- Inactividad en Pantalla 2b, 3, 5, 6: NO transita a deep sleep hasta que la operacion termine.
- Buffers liberados correctamente entre ciclos (sin degradacion de memoria tras 10+ iteraciones).

---

## Historia de Usuario 18: Internacionalizacion (i18n)

**Objetivo:** Extraer todos los textos de la UI a un diccionario `Messages` con estructura `LangMessages`, permitiendo cambiar de idioma en tiempo de compilacion. Implementar solo espanol inicialmente.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `messages.h` | Estructura `LangMessages`, declaraciones `MSG_ES`, `currentLang` |
| `messages.cpp` | Implementacion del diccionario en espanol |
| `src/ui/screens.cpp` (modificar) | Sustituir todos los strings hardcodeados por `currentLang->...` |

### Implementacion
1. Definir struct `LangMessages` con todos los campos requeridos (segun seccion 7 del spec).
2. Definir `MSG_ES` con todos los strings en espanol.
3. `LangMessages* currentLang = &MSG_ES;`
4. Reemplazar TODOS los literales de texto en `screens.cpp` por referencias a `currentLang`.

### Prueba de validacion
- Todas las pantallas muestran los textos correctos en espanol.
- Cambiar `currentLang` a un diccionario dummy en ingles (para prueba): los textos cambian.
- No hay strings hardcodeados restantes (buscar con grep).

---

## Historia de Usuario 19: Manejo de Errores y Pulido Final

**Objetivo:** Implementar todos los escenarios de error de la seccion 15 del spec, gestionar reconexion WiFi, indicador LED, mensajes de error en pantalla, proteccion contra PSRAM insuficiente, y limpieza general del codigo.

### Entregables
| Archivo | Proposito |
|---------|-----------|
| `user_app.cpp` (modificar) | `show_error_message(msg, duration_ms)`, chequeos de memoria |
| `wifi_bsp.cpp` (modificar) | Reconexion robusta, indicador LED |
| Varios | Comentarios removidos, codigo limpio |

### Implementacion
1. `show_error_message(msg, duration_ms)`:
   - Crear pantalla temporal con el mensaje de error centrado.
   - `vTaskDelay(duration_ms)`.
   - Transitar a Pantalla 2.
2. Errores a manejar:
   - WiFi timeout inicial → "WIFI: --" en barra, reintentar c/10s.
   - Error HTTP (timeout, 4xx, 5xx) → mensaje generico 2s.
   - Transcripcion falla → "Error al transcribir" 2s.
   - Chat falla → "Error del servidor" 2s.
   - Descarga MP3 falla → "Error al descargar audio" 2s.
   - PSRAM insuficiente → "Error de memoria", `ESP.restart()`.
   - Timeout API (20s) → igual que error HTTP.
3. LED (GPIO 3): parpadeo 200ms on/off durante WiFi connecting, apagado cuando idle.
4. En Pantalla 0 (Deep Sleep): full refresh antes de dormir en TODOS los casos.
5. Verificar que al salir de Pantalla 6 SIEMPRE se liberan: buffer MP3, texto STT, texto agente, URL audio.

### Prueba de validacion
- Apagar backend → mensajes de error aparecen y la app sigue funcional.
- Desconectar WiFi durante operacion → reconexion automatica.
- Llenar PSRAM (simular) → "Error de memoria" y restart.
- LED parpadea durante WiFi connecting, apagado en idle.
- Heap libre se mantiene estable tras multiples ciclos completos de conversacion.
- Pantalla completamente blanca antes de cada deep sleep.

---

## Resumen de Historias y Orden

| # | Historia | Depende de | Complejidad |
|---|----------|------------|-------------|
| 1 | Fundacion del Proyecto y Display E-Paper | — | Media |
| 2 | Integracion LVGL y Renderizado de Pantallas | HU1 | Media |
| 3 | Sistema de Botones Fisicos | HU1 | Baja |
| 4 | Deteccion de Touch (FT6336) | HU1 | Media |
| 5 | WiFi Management | HU1 | Baja |
| 6 | Medicion de Bateria y Barra de Estado | HU2, HU4, HU5 | Media |
| 7 | Pantalla 0 — Deep Sleep con RTC Memory | HU1, HU3 | Alta |
| 8 | Pantalla 1 — Activo (Seleccion) | HU2, HU3, HU4, HU7 | Media |
| 9 | Codec de Audio e Inicializacion I2S | HU1 | Alta |
| 10 | Captura de Audio y Codificacion WAV | HU9 | Alta |
| 11 | Pantallas 2 y 2b — Grabar Mensaje | HU2, HU3, HU4, HU8, HU10 | Media |
| 12 | Cliente API REST | HU5 | Media |
| 13 | Pantallas 3 y 3b — Enviando y Descartado | HU2, HU11, HU12 | Media |
| 14 | Pantalla 4 — Confirmacion de Transcripcion | HU2, HU3, HU4, HU13 | Baja |
| 15 | Reproduccion de Audio MP3 via minimp3 | HU9 | Alta |
| 16 | Pantallas 5 y 6 — Esperando y Respuesta | HU2, HU12, HU14, HU15 | Alta |
| 17 | Maquina de Estados Completa | HU7, HU8, HU11, HU13, HU14, HU16 | Alta |
| 18 | Internacionalizacion (i18n) | HU2 | Baja |
| 19 | Manejo de Errores y Pulido Final | HU17 | Media |

---

## Diagrama de Dependencias

```
HU1 ──┬── HU2 ──┬── HU6 (Status Bar)
      │         ├── HU8 ── HU11 ──┬── HU13 ── HU14 ──┐
      │         │                 │                    │
      ├── HU3 ──┤                 │                    ├── HU16 ──┐
      │         │                 │                    │          │
      ├── HU4 ──┤                 │                    │          ├── HU17 ── HU19
      │         │                 │                    │          │
      ├── HU5 ──┴── HU12 ────────┴────────────────────┘          │
      │                                                           │
      ├── HU7 ───────────────────────────────────────────────────┘
      │
      └── HU9 ──┬── HU10 ────────────────────────────────────────┘
                 └── HU15 ───────────────────────────────────────┘

HU18 (i18n) — se puede hacer en cualquier momento tras HU2
```

---

## Notas para el Desarrollador

1. **Arduino IDE como build system**: Usar `.ino` como punto de entrada. Los archivos `.cpp`/.`h` en `src/` se compilan automaticamente. Las librerias externas (lvgl, minimp3, multi_button, esp_codec_dev) deben estar en `lib/` o como submodulos.

2. **PSRAM es critica**: Todos los buffers grandes (LVGL 80KB x2, audio WAV ~2MB, MP3 ~200KB) deben alocarse con `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`. Verificar `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` antes de alocar.

3. **Mutex de LVGL**: Toda operacion sobre widgets LVGL debe estar protegida por `lvgl_mutex`. Usar `example_lvgl_lock()` / `example_lvgl_unlock()` consistente con los ejemplos.

4. **Operaciones de red asincronas**: Las llamadas HTTP se ejecutan en la tarea `main_state_task` o en tareas auxiliares, nunca en la tarea LVGL. Usar `vTaskDelay()` dentro de la tarea de red para no bloquear.

5. **Full refresh solo en deep sleep**: Durante la operacion normal, usar SIEMPRE `EPD_DisplayPart()`. El full refresh (`EPD_Display()` con `WF_Full_1IN54`) solo se usa justo antes de `esp_deep_sleep_start()`.

6. **Liberacion de buffers en Pantalla 6**: Es critico liberar TODOS los buffers al salir de Pantalla 6. Usar una funcion dedicada `cleanup_response_buffers()` llamada en todos los puntos de salida.

7. **Testing del deep sleep**: Para acelerar pruebas, usar defines como `TEST_SLEEP_DURATION_SEC` en lugar de 60 minutos. Cambiar a produccion al final.

---

Documento de planificacion v1.0. Listo para iniciar implementacion.
