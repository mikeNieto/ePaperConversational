# Especificacion Tecnica y Funcional

## Dispositivo: Waveshare ESP32-S3-Touch-ePaper-1.54

---

# PARTE 1: ESPECIFICACION FUNCIONAL

## 1. Descripcion General

Aplicacion de firmware para Arduino IDE sobre el dispositivo Waveshare ESP32-S3-Touch-ePaper-1.54. El dispositivo actua como interfaz conversacional por voz: graba mensajes de audio del usuario, los envia a un backend REST para transcripcion (STT), procesamiento por agente IA (Chat) y sintesis de voz (TTS), luego reproduce la respuesta de audio.

El firmware detecta automaticamente si el hardware posee controlador touch (FT6336). Si esta presente, habilita interaccion tactil en todas las pantallas que lo requieran. Si no esta presente, solo los botones fisicos PWR y BOOT estan disponibles para la navegacion.

---

## 2. Estados y Pantallas

El dispositivo tiene **8 pantallas/estados** numerados del 0 al 7:

### Pantalla 0 — Deep Sleep

```
+--------------------------------------+
|                                      |
|                                      |
|                                      |
|           Durmiendo...   N           |
|                                      |
|                                      |
+--------------------------------------+
```

- **N** = contador de ciclos de sleep sin intervencion del usuario. Cuando el usuario despierta el dispositivo manualmente con PWR o BOOT, este contador se reinicia a 0.
- Esta pantalla se muestra al entrar en deep sleep y cada vez que el dispositivo se auto-despierta (al expirar el timer de 60 minutos, sin intervencion del usuario).
- **Cada vez que se entra a deep sleep** (ya sea por inactividad o por auto-despertar), se ejecuta un full refresh de la pantalla antes de dormir para dejar el display completamente blanco y libre de ghosting.
- Al auto-despertarse: limpia completamente la pantalla en blanco con full refresh, incrementa N, redibuja esta misma pantalla, y vuelve a entrar en deep sleep por otros 60 minutos.
- NOTA: En esta pantalla NO se muestra la barra de estado (WiFi ni bateria), ya que el dispositivo esta en deep sleep con las radios y perifericos apagados.

### Pantalla 1 — Activo (Seleccion de Conversacion)

```
+---------------------------------------------+
| WIFI: OK                           [bat]XX% |
|---------------------------------------------|
|         +-------------------------+         |
|         | Continuar Conversacion  |         |
|         +-------------------------+         |
|                                             |
|         +-------------------------+         |
|         |   Nueva Conversacion    |         |
|         +-------------------------+         |
|                                             |
+---------------------------------------------+
```

- Muestra dos botones en pantalla: "Continuar Conversacion" y "Nueva Conversacion".
- Por defecto, la opcion "Continuar Conversacion" esta seleccionada (resaltada visualmente).
- Si el UUID de conversacion es nulo (primera instalacion del firmware), solo se muestra el boton "Nueva Conversacion".

**Interaccion tactil:**
- Toque simple en cualquiera de los dos botones → navega a la Pantalla 2 con la opcion elegida.

**Interaccion por botones fisicos:**
- **BOOT (doble click):** Cambia la seleccion entre los dos botones (toggle).
- **BOOT (click simple):** Activa la opcion actualmente seleccionada → navega a Pantalla 2.
- **PWR:** Sin efecto en esta pantalla.

### Pantalla 2 — Grabar Mensaje

```
+---------------------------------------------+
| WIFI: OK                           [bat]XX% |
|---------------------------------------------|
|                                             |
|                                             |
|             Grabar Mensaje                  |
|                                             |
|                                             |
+---------------------------------------------+
```

- Mensaje "Grabar Mensaje" centrado en pantalla.

**Interaccion por botones fisicos:**
- **BOOT (click simple):** Inicia grabacion → transita a Pantalla 2b ("Escuchando...").
- **PWR (click simple):** Vuelve a Pantalla 1 (Activo).

**Interaccion tactil:**
- Toque en cualquier parte de la pantalla → inicia grabacion → transita a Pantalla 2b.

### Pantalla 2b — Escuchando... (Grabacion en curso)

```
+---------------------------------------------+
| WIFI: OK                           [bat]XX% |
|---------------------------------------------|
|                                             |
|                                             |
|             Escuchando...                   |
|                                             |
|                                             |
+---------------------------------------------+
```

- Estado de grabacion activa. El dispositivo esta capturando audio del microfono.
- Duracion maxima de grabacion: **30 segundos**.
- Al alcanzar los 30 segundos sin intervencion del usuario, el mensaje se considera incorrecto y se descarta automaticamente → transita a Pantalla 3b ("Mensaje descartado!!") por 1.5 segundos → vuelve a Pantalla 2 ("Grabar Mensaje").

**Interaccion por botones fisicos:**
- **BOOT (click simple):** Detiene grabacion y envia el audio → transita a Pantalla 3 ("Enviando Mensaje...").
- **PWR (click simple):** Descarta la grabacion → transita a Pantalla 3b ("Mensaje descartado!!") por 1.5 segundos → vuelve a Pantalla 2 ("Grabar Mensaje").

**Interaccion tactil:**
- Toque en pantalla: equivalente a BOOT (detiene y envia) → transita a Pantalla 3.

### Pantalla 3 — Enviando Mensaje...

```
+---------------------------------------------+
| WIFI: OK                           [bat]XX% |
|---------------------------------------------|
|                                             |
|                                             |
|           Enviando Mensaje...               |
|                                             |
|                                             |
+---------------------------------------------+
```

- El audio grabado se esta enviando al endpoint `/api/audio/transcribe`.
- El dispositivo espera la respuesta del STT.
- **NOTA:** Durante esta pantalla el contador de inactividad para deep sleep (60s) esta **PAUSADO**.

### Pantalla 4 — Confirmacion de Transcripcion

```
+---------------------------------------------+
| WIFI: OK                           [bat]XX% |
|---------------------------------------------|
|                                             |
|   [texto transcrito en idioma original]     |
|   (posiblemente multilinea, centrado)       |
|                                             |
|         +-------------------------+         |
|         |         Enviar          |         |
|         +-------------------------+         |
|                                             |
|         +-------------------------+         |
|         |        Cancelar         |         |
|         +-------------------------+         |
|                                             |
+---------------------------------------------+
```

- Muestra el texto transcrito devuelto por el STT (campo `text` de la respuesta).
- Dos botones: "Enviar" y "Cancelar".
- "Enviar" seleccionado por defecto.

**Interaccion tactil:**
- Toque en "Enviar" → envia el texto al endpoint `/api/chat/message` → transita a Pantalla 5.
- Toque en "Cancelar" → descarta → vuelve a Pantalla 2 ("Grabar Mensaje").

**Interaccion por botones fisicos:**
- **BOOT (doble click):** Cambia la seleccion entre Enviar/Cancelar.
- **BOOT (click simple):** Activa la opcion seleccionada.
- **PWR (click simple):** Equivalente a Cancelar → vuelve a Pantalla 2.

### Pantalla 3b — Mensaje descartado!!

```
+---------------------------------------------+
| WIFI: OK                           [bat]XX% |
|---------------------------------------------|
|                                             |
|                                             |
|         Mensaje descartado!!                |
|                                             |
|                                             |
+---------------------------------------------+
```

- Se muestra por **exactamente 1.5 segundos**.
- Luego transita automaticamente a Pantalla 2 ("Grabar Mensaje").
- Sin interaccion del usuario durante esta pantalla.

### Pantalla 5 — Esperando Respuesta...

```
+---------------------------------------------+
| WIFI: OK                           [bat]XX% |
|---------------------------------------------|
|                                             |
|                                             |
|         Esperando Respuesta...              |
|                                             |
|                                             |
+---------------------------------------------+
```

- El mensaje se envio al agente (`/api/chat/message` con `response_audio: true`).
- Se espera la respuesta del backend.
- **NOTA:** Durante esta pantalla el contador de inactividad para deep sleep (60s) esta **PAUSADO**.

### Pantalla 6 — Mostrando Respuesta

```
+---------------------------------------------+
| WIFI: OK                           [bat]XX% |
|---------------------------------------------|
|                                             |
|   [texto de respuesta del agente]           |
|   (campo agent_text, centrado, multilinea)  |
|                                             |
|                                             |
+---------------------------------------------+
```

- Muestra el texto de respuesta del agente (`agent_text`) centrado en pantalla.
- Simultaneamente, reproduce el audio TTS descargado desde `audio_url`.
- Al terminar la reproduccion del audio → transita automaticamente a Pantalla 2 ("Grabar Mensaje") para permitir al usuario continuar la conversacion.
- **NOTA:** Mientras se reproduce el audio, el contador de inactividad para deep sleep (60s) esta **PAUSADO**. Una vez termina la reproduccion, el contador se reanuda.
- **NOTA:** Al abandonar esta pantalla (ya sea por fin de reproduccion, PWR o BOOT), se deben liberar inmediatamente de PSRAM tanto el buffer del archivo MP3 descargado como los textos de respuesta del STT y del agente, ya que no seran necesarios en la siguiente iteracion.

**Interaccion por botones fisicos:**
- **PWR (click simple):** Detiene la reproduccion de audio (si esta en curso) y transita a Pantalla 2 ("Grabar Mensaje"). Aplica la liberacion de buffers descrita arriba.
- **BOOT (click simple):** Reinicia la reproduccion del audio TTS desde el principio, sin necesidad de volver a descargarlo. El texto en pantalla permanece sin cambios.

**Interaccion tactil:**
- Toque en pantalla: equivalente a BOOT (reinicia la reproduccion del audio).

---

## 3. Maquina de Estados Completa

```
                       +------------------+
                       |  0. Deep Sleep   |
                       +--------+---------+
                                |
                    PWR o BOOT (usuario despierta)
                                |
                                v
                       +--------+---------+
               +------>|   1. Activo      |
               |       +--------+---------+
               |                |
               |   Seleccion + UUID no nulo: mostrar 2 opciones
               |   Seleccion + UUID nulo: solo "Nueva Conversacion"
               |                |
               |   BOOT click o touch en opcion seleccionada
               |                |
               |       +--------v---------+
               |       |  2. Grabar Msg   |<-----------------------------+
               |       +--------+---------+                              |
               |                |                                        |
               |         BOOT click o touch                              |
               |                |                                        |
               |       +--------v---------+                              |
               |       | 2b. Escuchando.. |                              |
               |       +--------+---------+                              |
               |           |            |                                |
               |    BOOT/touch     PWR o timeout 30s                     |
               |    (detener y     (descarta)                            |
               |     enviar)           |                                |
               |           |      +-----v-----------+                    |
               |           |      | 3b. Msj descart  |--(1.5s)----------+
               |           |      +-----------------+                    |
               |           |                                             |
               |   +-------v--------+                                    |
               |   | 3. Enviando... |                                    |
               |   +-------+--------+                                    |
               |           |                                             |
               |    Respuesta STT recibida                               |
               |           |                                             |
               |   +-------v--------+                                    |
               |   | 4. Confirmacion|                                    |
               |   +-------+--------+                                    |
               |       |           |                                     |
               |    Enviar     Cancelar ---------------------------------+
               |       |
               |   +---v------------+
               |   | 5. Esperando...|
               |   +-------+--------+
               |           |
               |    Respuesta Chat+TTS recibida
               |           |
                |   +-------v--------+
                |   | 6. Mostrar Rsp |
                |   +-------+--------+
                |       |       |        |
                |  BOOT/touch  PWR o   Fin repr.
                |   (replay) (detiene)  (auto)
                |       |       |        |
                +-------+-------+--------+  (vuelve a Grabar Msg + libera buffers)
```

---

## 4. Comportamiento del Deep Sleep

### Entrada en Deep Sleep
- Contador de inactividad: **60 segundos**.
- El contador corre en todas las pantallas EXCEPTO:
  - Pantalla 2b ("Escuchando...")
  - Pantalla 3 ("Enviando Mensaje...")
  - Pantalla 5 ("Esperando Respuesta...")
  - Pantalla 6 ("Mostrando Respuesta") mientras se reproduce audio
- Al agotarse los 60s, el dispositivo entra en deep sleep.
- **Inmediatamente antes de entrar en deep sleep**, el display debe ejecutar un **full refresh** (limpieza completa de la pantalla con `EPD_Display()` usando el LUT completo, no partial refresh). Esto garantiza que la pantalla quede completamente blanca antes de dormir y evita acumulacion de ghosting. Esta regla aplica **siempre** que se entra a deep sleep, incluyendo la primera vez.

### Durante el Deep Sleep
- Duracion: **60 minutos** (programado via RTC o timer interno).
- Variables que sobreviven: aquellas anotadas con `RTC_DATA_ATTR`.
- Pines que PERMANECEN activos (REGLA NO NEGOCIABLE):
  - VBAT_PWR_PIN (GPIO 17): alimentacion de bateria.
  - Carga por USB-C: controlado por hardware, no se desactiva.
  - PWR_BUTTON_PIN (GPIO 18) y BOOT_BUTTON_PIN (GPIO 0): wakeup sources.
- Los pines de wakeup se configuran via `esp_sleep_enable_ext1_wakeup_io()` con `ESP_EXT1_WAKEUP_ANY_LOW`.
- Se usa `rtc_gpio_hold_en()` en GPIO 17 para mantener el estado de VBAT durante el sleep.

### Salida del Deep Sleep

**Caso A — Usuario despierta con PWR o BOOT:**
1. Se evalua la causa de wakeup (`esp_sleep_get_wakeup_cause()`).
2. Si la causa es `ESP_SLEEP_WAKEUP_EXT1`, se identifica el pin que desperto.
3. Se reinicializan todos los perifericos (WiFi, display, audio, etc.).
4. **El contador de sleeps (`sleep_counter`) se reinicia a 0**, ya que el usuario ha intervenido activamente.
5. Se muestra la Pantalla 1 (Activo).

**Caso B — Auto-despertar por timer (60 minutos):**
1. Se evalua la causa de wakeup.
2. Si es `ESP_SLEEP_WAKEUP_TIMER`, se incrementa el contador `RTC_DATA_ATTR int sleepCounter`.
3. Se enciende el display.
4. Se limpia completamente la pantalla en blanco.
5. Se muestra "Durmiendo... N" con el valor actualizado de N.
6. Se vuelve a entrar en deep sleep por 60 minutos.
7. NOTA: En este caso NO se enciende WiFi ni audio.

---

## 5. Barra de Estado (WiFi + Bateria)

### Formato
```
WIFI: OK                           [bat]XX%
```

### Posicion
- Parte superior de la pantalla, en todas las pantallas excepto Pantalla 0 (Deep Sleep).
- "WIFI: OK" alineado a la izquierda.
- Icono de bateria + porcentaje alineado a la derecha.

### Estados WiFi
- **WIFI: OK** — Conectado, direccion IP asignada.
- **WIFI: --** — Desconectado o conectando.

### Estados Bateria
Se muestra un icono de bateria (grafico simple) y el porcentaje calculado.
- **100%:** Vbat >= 4.12V
- **0%:** Vbat <= 3.0V
- **Interpolacion lineal:** `(Vbat - 3.0) / (4.12 - 3.0) * 100`
- Antes de medir, se debe activar VBAT_PWR_PIN (GPIO 17). Despues de medir, se puede desactivar para ahorrar energia (excepto si se va a usar en breve).

### Actualizacion
- La bateria se mide y se actualiza cada vez que se renderiza una pantalla o al menos cada 30 segundos.

---

## 6. Flujo de Conversacion (Nueva vs Continuar)

### Nueva Conversacion
1. Usuario selecciona "Nueva Conversacion" en Pantalla 1.
2. El firmware genera un nuevo UUID (thread_id) usando `esp_random()` o similar, o lo solicita al backend.
3. Se almacena el UUID en `RTC_DATA_ATTR`.
4. Se procede a Pantalla 2 (Grabar Mensaje).

### Continuar Conversacion
1. Usuario selecciona "Continuar Conversacion" en Pantalla 1.
2. Se usa el UUID (thread_id) ya almacenado.
3. Se procede a Pantalla 2 (Grabar Mensaje).

### Primer inicio (UUID nulo)
1. En Pantalla 1 solo se muestra el boton "Nueva Conversacion".
2. Al seleccionarlo, transita directamente a Pantalla 2 con generacion de nuevo UUID.

---

## 7. Internacionalizacion (i18n)

Todos los mensajes visibles al usuario deben almacenarse en un diccionario/estructura de variables globales (ej. `Messages`), indexados por clave. Ejemplo:

```cpp
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
    // ...
};

// Por ahora solo espanol
const LangMessages MSG_ES = {
    .continuar_conversacion = "Continuar Conversacion",
    .nueva_conversacion = "Nueva Conversacion",
    .grabar_mensaje = "Grabar Mensaje",
    .escuchando = "Escuchando...",
    .enviando_mensaje = "Enviando Mensaje...",
    .mensaje_descartado = "Mensaje descartado!!",
    .esperando_respuesta = "Esperando Respuesta...",
    .durmiendo = "Durmiendo...",
    .enviar = "Enviar",
    .cancelar = "Cancelar",
    .wifi_ok = "WIFI: OK",
};

LangMessages* currentLang = &MSG_ES;
```

---

# PARTE 2: ESPECIFICACION TECNICA

## 8. Plataforma Hardware

| Componente | Especificacion |
|------------|---------------|
| Microcontrolador | ESP32-S3 (Xtensa LX7 dual-core, 240 MHz) |
| Memoria Flash | 16 MB (QIO mode) |
| PSRAM | Octal SPI RAM (SPIRAM mode OCT, 80 MHz) |
| Display | E-Paper 1.54", 200x200 pixeles, 1-bit (blanco/negro) |
| Controlador Display | SSD1681 (compatible GDEW0154M10) |
| Touch | FT6336 (I2C addr 0x38) — opcional, se detecta en runtime |
| Audio Codec | ES8311 (I2S + I2C), amplificador PA en GPIO 46 |
| RTC | PCF85063 (I2C addr 0x51) |
| Sensor Temp/Hum | SHTC3 (I2C addr 0x70) |
| Bateria | LiPo 1S, monitoreo por ADC1_CH3 (GPIO 4) con divisor 1:2 |
| Botones | BOOT (GPIO 0), PWR (GPIO 18) |
| LED | GPIO 3 (indicador de estado) |
| Carga | USB-C con control de carga integrado |

---

## 9. Mapa de Pines Completo

### Display E-Paper (SPI2)
| Señal | GPIO | Direccion | Notas |
|-------|------|-----------|-------|
| DC | 10 | Output | Data/Command |
| CS | 11 | Output | Chip Select (activo LOW) |
| SCK | 12 | Output | SPI Clock |
| MOSI | 13 | Output | SPI Data (no MISO, solo escritura) |
| RST | 9 | Output | Hardware Reset |
| BUSY | 8 | Input | HIGH = ocupado, LOW = listo |
| PWR | 6 | Output | Alimentacion display (activo LOW: 0=ON) |

### Audio (I2S + Control)
| Señal | GPIO | Notas |
|-------|------|-------|
| I2S BCLK | 15 | Bit Clock |
| I2S WS | 38 | Word Select / LRCLK |
| I2S DOUT | 45 | Data Output (DAC → codec) |
| I2S DIN | 16 | Data Input (codec ADC → ESP32) |
| I2S MCLK | 14 | Master Clock |
| Audio PWR | 42 | Alimentacion codec (activo LOW: 0=ON) |
| PA | 46 | Amplificador audio (activo HIGH) |

### I2C Bus (I2C_NUM_0, 400 kHz)
| Señal | GPIO |
|-------|------|
| SDA | 47 |
| SCL | 48 |

### Dispositivos I2C
| Dispositivo | Direccion | Funcion |
|-------------|-----------|---------|
| PCF85063 | 0x51 | RTC |
| SHTC3 | 0x70 | Temp/Humedad |
| FT6336 | 0x38 | Touch (opcional) |

### Touch (FT6336)
| Señal | GPIO | Notas |
|-------|------|-------|
| RST | 7 | Reset (HIGH-LOW-HIGH) |
| INT | 21 | Interrupcion (NEGEDGE, pull-up) |

### Botones
| Boton | GPIO | Activo | Wakeup |
|-------|------|--------|--------|
| BOOT | 0 | LOW | EXT1 |
| PWR | 18 | LOW | EXT1 |

### Potencia y Bateria
| Señal | GPIO | Activo | Notas |
|-------|------|--------|-------|
| VBAT PWR | 17 | HIGH = ON | Habilita circuito de medicion |
| VBAT ADC | 4 | ADC1_CH3 | Medicion con divisor 1:2 |

### LED
| Señal | GPIO | Notas |
|-------|------|-------|
| LED | 3 | HIGH = encendido |

### RTC
| Señal | GPIO | Notas |
|-------|------|-------|
| RTC INT | 5 | Interrupcion del PCF85063 |

---

## 10. Arquitectura de Software

### Librerias Externas Requeridas

| Libreria | Version | Uso | Fuente |
|----------|---------|-----|--------|
| **lvgl** | v8.x | UI y renderizado de pantallas | `01_Arduino_Libraries/lvgl8/` |
| **SensorLib** | — | Driver RTC (SensorPCF85063) y sensores | `01_Arduino_Libraries/SensorLib/` |
| **esp_codec_dev** | v1.3.5 | Control de codec ES8311 | `08_Audio_Test/src/esp_codec_dev/` |
| **multi_button** | — | Manejo de botones con multi-press | Incluido en ejemplos |
| **minimp3** | — | **Decodificacion MP3 a PCM** | Single-header C (ver seccion 11) |
| **WiFi** | Arduino built-in | Conexion WiFi STA | nativo |
| **HTTPClient** | Arduino built-in | Peticiones REST | nativo |

### Estructura de Archivos del Proyecto

```
ePaperConversational/
├── ePaperConversational.ino       # Punto de entrada (setup/loop vacios)
├── user_config.h                  # Pines, constantes, configuracion
├── user_app.h                     # Declaraciones de user_app
├── user_app.cpp                   # Logica principal de la aplicacion
├── messages.h                     # Diccionario de mensajes i18n
├── messages.cpp                   # Implementacion de idiomas
├── audio_bsp.h                    # API de audio (grabar/reproducir)
├── audio_bsp.cpp                  # Implementacion de audio
├── wifi_bsp.h                     # API de WiFi
├── wifi_bsp.cpp                   # Implementacion de WiFi
├── api_client.h                   # API REST client
├── api_client.cpp                 # Implementacion llamadas HTTP
├── minimp3.h                      # Decodificador MP3 (single-header)
├── src/
│   ├── display/
│   │   ├── epaper_driver_bsp.h    # Driver e-paper
│   │   └── epaper_driver_bsp.cpp
│   ├── power/
│   │   ├── board_power_bsp.h      # Control de alimentacion
│   │   └── board_power_bsp.cpp
│   ├── button_bsp/
│   │   ├── button_bsp.h           # Botones fisicos
│   │   ├── button_bsp.cpp
│   │   ├── multi_button.h         # Libreria multi-press
│   │   └── multi_button.c
│   ├── i2c_bsp/
│   │   ├── i2c_bsp.h              # Bus I2C
│   │   └── i2c_bsp.cpp
│   ├── touch_bsp/
│   │   ├── ft6336_bsp.h           # Driver touch FT6336
│   │   └── ft6336_bsp.cpp
│   ├── battery/
│   │   ├── battery_bsp.h          # Medicion de bateria
│   │   └── battery_bsp.cpp
│   ├── codec_board/
│   │   ├── board_cfg.h            # Configuracion de codec de audio
│   │   ├── board_cfg.c
│   │   ├── codec_board.h          # Inicializacion de codec
│   │   ├── codec_board.cpp
│   │   ├── codec_init.h
│   │   └── codec_init.c
│   ├── esp_codec_dev/             # Libreria de codec (misma de Audio_Test)
│   │   └── ...
│   └── ui/
│       ├── screens.h              # Declaraciones de pantallas
│       ├── screens.cpp            # Implementacion de cada pantalla
│       ├── status_bar.h           # Barra de estado WiFi+Bateria
│       └── status_bar.cpp
```

### Tareas FreeRTOS

| Tarea | Core | Prioridad | Stack | Funcion |
|-------|------|-----------|-------|---------|
| `lvgl_task` | 1 | 4 | 8192 | Maneja el tick de LVGL y renderizado |
| `button_task` | 1 | 3 | 4096 | Sondeo de botones (multi_button) |
| `touch_task` | 1 | 3 | 4096 | Procesamiento de eventos touch (solo si FT6336 presente) |
| `audio_task` | 1 | 5 | 8192 | Grabacion, codificacion WAV, reproduccion audio |
| `wifi_task` | 1 | 2 | 4096 | Conexion y reconexion WiFi |
| `main_state_task` | 1 | 3 | 8192 | Maquina de estados principal |
| `deep_sleep_timer_task` | 1 | 1 | 4096 | Contador de inactividad (60s) |

### Comunicacion entre Tareas

| Mecanismo | Uso |
|-----------|-----|
| `EventGroupHandle_t` | Eventos de botones (BOOT: click, double, long; PWR: click, long) |
| `EventGroupHandle_t` | Eventos de touch (tap detectado, coordenadas) |
| `EventGroupHandle_t` | Eventos de sistema (wifi_connected, api_response_ready, audio_done) |
| `QueueHandle_t` | Cola de estados para la maquina de estados principal |
| `SemaphoreHandle_t` | Mutex para acceso thread-safe a LVGL |
| `SemaphoreHandle_t` | Semaforo binario para notificacion de descarga HTTP completada |

---

## 11. Pipeline de Audio

### Grabacion (Microfono → API)

```
Microfono → ES8311 ADC → I2S DIN (16000 Hz, 16-bit, 2 canales)
    → buffer en PSRAM (max 30 segundos: 16000*2*2*30 = 1,920,000 bytes)
    → Codificar WAV (agregar header WAV de 44 bytes)
    → POST /api/audio/transcribe (multipart/form-data)
```

**Formato WAV resultante:**
- Sample rate: 16000 Hz
- Bits per sample: 16
- Canales: 2 (estereo) — se graba en estereo pero se podria convertir a mono
- Codec: PCM (sin compresion)
- Header WAV estandar (RIFF, fmt, data chunks)

### Reproduccion (API → Parlante)

```
GET /api/audio/files/<filename>.mp3
    → Buffer en PSRAM (archivo MP3 descargado, se conserva durante toda la estancia en Pantalla 6)
    → Decodificar con minimp3: MP3 → PCM 16-bit stereo
    → PCM → esp_codec_dev_write() → I2S DOUT → ES8311 DAC → PA (GPIO 46) → Parlante
    → [BOOT] → Reiniciar puntero MP3 y decodificador, reproducir de nuevo desde el buffer
    → [PWR o fin] → Detener reproduccion, liberar buffer MP3 y textos, volver a Pantalla 2
```

**Formato de salida PCM:**
- Sample rate: el que indique el stream MP3 (tipicamente 44100 o 48000 Hz)
- Bits per sample: 16
- Canales: 2 (estereo)
- La libreria `esp_codec_dev` debe configurarse dinamicamente al sample rate del MP3 decodificado.

### Pipeline de decodificacion MP3 (minimp3)

minimp3 es una libreria C de un solo archivo (`minimp3.h`) que provee:

```c
// API principal:
void mp3dec_init(mp3dec_t *dec);
int mp3dec_decode_frame(mp3dec_t *dec, const uint8_t *mp3, int mp3_bytes,
                         mp3d_sample_t *pcm, mp3dec_frame_info_t *info);
```

**Flujo de decodificacion:**

```
1. Descargar archivo MP3 completo a buffer en PSRAM via HTTP GET.
2. Inicializar decodificador: mp3dec_init(&dec).
3. Bucle:
   a. mp3dec_decode_frame(&dec, mp3_buf_ptr, bytes_remaining, pcm_buffer, &info);
   b. Si PWR fue presionado → interrumpir bucle, liberar buffers (paso 4), transitar a Pantalla 2.
   c. Si BOOT fue presionado → reiniciar puntero al inicio del buffer MP3 y decodificador (mp3dec_init de nuevo), continuar bucle.
   d. Escribir pcm_buffer (info.channels * info.audio_bytes samples) via esp_codec_dev_write().
   e. Avanzar mp3_buf_ptr += info.frame_bytes.
   f. Repetir hasta que no queden mas bytes o se interrumpa.
4. Liberar buffer MP3 de PSRAM y resetear punteros de datos de respuesta.
```

**Gestion de buffers de respuesta:**
- Al abandonar la Pantalla 6 por cualquier motivo (fin de reproduccion, PWR, BOOT, o timeout), se liberan todos los buffers asociados a la respuesta del ciclo conversacional:
  - Buffer del archivo MP3 descargado.
  - Texto de transcripcion STT (respuesta de `/api/audio/transcribe`).
  - Texto de respuesta del agente (`agent_text` de `/api/chat/message`).
  - URL de audio (`audio_url`).
- Esto asegura que cada nuevo ciclo de grabacion-envio-respuesta parte con la memoria limpia.
```

**Buffers necesarios en PSRAM:**
- Buffer MP3: ~200 KB tipico para unos 30 segundos de audio MP3 (a 64kbps).
- Buffer PCM de salida: `MP3_MAX_SAMPLES_PER_FRAME * 2 * 2 = 1152 * 4 = ~4.6 KB` por frame, reutilizable.

---

## 12. API REST — Protocolo Completo

### Endpoint Base
```
BASE_URL = "http://<IP>:<PUERTO>"   // Variable de configuracion en firmware
```

### 12.1 Health Check (al iniciar)
```
GET /api/health
Response 200: {"status": "ok", ...}
```
Verifica conectividad con el backend al iniciar el dispositivo.

### 12.2 Transcribir Audio
```
POST /api/audio/transcribe
Content-Type: multipart/form-data

Body:
  file: <archivo WAV>

Response 200:
{
  "text": "texto transcrito en idioma original",
  "original_text": "texto original",
  "translated_text": "texto traducido",
  "detected_language": "es",
  "duration_seconds": 2.5
}

Error: Cualquier codigo != 200 → mostrar error y volver a Pantalla 2.
```

### 12.3 Enviar Mensaje al Chat
```
POST /api/chat/message
Content-Type: application/json

Body:
{
  "user_id": "<user_id configurado>",
  "thread_id": "<uuid de conversacion>",
  "message": "<texto transcrito (campo 'text' del paso 12.2)>",
  "response_audio": true
}

Response 200:
{
  "thread_id": "...",
  "user_message": "texto enviado",
  "agent_text": "respuesta del agente en texto",
  "tts_text": "texto preparado para TTS",
  "audio_url": "/api/audio/files/<uuid>.mp3",
  "audio_mime_type": "audio/mpeg",
  "resolved_model": "..."
}

Error: Cualquier codigo != 200 → mostrar error y volver a Pantalla 2.
```

### 12.4 Descargar Audio TTS
```
GET {audio_url}
Ejemplo: GET /api/audio/files/abc123.mp3

Response 200:
  Content-Type: audio/mpeg
  Body: <datos binarios MP3>

Error: Cualquier codigo != 200 → mostrar error y volver a Pantalla 2.
```

### 12.5 Variables de Configuracion de API (hardcoded en firmware)

```cpp
// Configuracion WiFi
const char* WIFI_SSID = "TU_SSID";
const char* WIFI_PASSWORD = "TU_PASSWORD";

// Configuracion API
const char* API_BASE_URL = "http://192.168.1.100:8000";  // Cambiar segun despliegue
const char* USER_ID = "esp32-user-01";                     // ID de usuario fijo
```

**NOTA:** En esta version, estos valores van hardcodeados como variables globales. En versiones futuras seran configurables dinamicamente.

---

## 13. Secuencia de Inicializacion del Dispositivo

### Al encender / despertar de Deep Sleep:

```
1.  Leer causa de wakeup.
2.  Inicializar Serial (115200 baud).
3.  Inicializar GPIOs de potencia:
    a. board_div.POWEER_EPD_ON()     // GPIO 6 = LOW
    b. board_div.POWEER_Audio_ON()   // GPIO 42 = LOW
4.  Inicializar I2C (SDA=47, SCL=48, 400 kHz).
5.  Inicializar SPI2 para e-paper (40 MHz).
6.  Detectar FT6336 en I2C 0x38:
    a. Si responde → hasTouch = true. Inicializar driver FT6336.
    b. Si no responde → hasTouch = false.
7.  Inicializar e-paper:
    a. Crear epaper_driver_display.
    b. EPD_Init().
    c. EPD_Clear().
    d. EPD_DisplayPartBaseImage().
    e. EPD_Init_Partial().
8.  Inicializar LVGL:
    a. Asignar buffers en SPIRAM (80000 bytes RGB565).
    b. Configurar flush callback.
    c. Inicializar tick timer (5ms).
    d. Crear tarea LVGL (Core 1, prio 4, stack 8KB).
9.  Inicializar codec de audio ES8311:
    a. audio_bsp_init().
    b. audio_play_init().
10. Inicializar botones (multi_button, GPIO 0 y 18).
11. Inicializar WiFi:
    a. WiFi.mode(WIFI_STA).
    b. WiFi.begin(SSID, PASSWORD).
    c. Esperar conexion (timeout 15 segundos).
12. Si es wakeup por usuario → Pantalla 1 (Activo).
    Si es wakeup por timer → mostrar "Durmiendo... N", incrementar N, deep sleep.
    Si es primer boot (power-on reset) y UUID es nulo → Pantalla 1 con solo "Nueva Conversacion".
```

---

## 14. Variables Persistentes (RTC_DATA_ATTR)

```cpp
// Todas estas sobreviven al deep sleep
RTC_DATA_ATTR char conversation_uuid[37];  // UUID de la conversacion activa (36 chars + null)
RTC_DATA_ATTR bool uuid_is_null;            // true si no hay UUID asignado
RTC_DATA_ATTR int sleep_counter;            // Contador de ciclos de deep sleep sin intervencion
RTC_DATA_ATTR int boot_count;               // Contador de boots totales (para debug)
```

**Inicializacion:**
- En `setup()`, verificar si `boot_count == 0` (primer arranque absoluto) → inicializar `uuid_is_null = true`, `sleep_counter = 0`.
- Si `boot_count > 0` → los valores ya vienen de RTC memory, no reinicializar.
- Siempre incrementar `boot_count`.
- **Al despertar por intervencion del usuario** (PWR o BOOT desde deep sleep), `sleep_counter` se asigna explicitamente a 0, independientemente de su valor anterior. Esto refleja que el usuario esta activo y reinicia el conteo de ciclos sin intervencion.

---

## 15. Manejo de Errores

| Escenario | Accion |
|-----------|--------|
| WiFi no conecta (timeout 15s) | Mostrar "WIFI: --" en barra. Reintentar cada 10s. La app sigue funcionando sin WiFi (muestra mensaje de error si intenta usar API). |
| Error HTTP (timeout, 4xx, 5xx) | Mostrar mensaje generico de error en pantalla por 2 segundos. Volver a Pantalla 2 (Grabar Mensaje). |
| Transcripcion falla | Mostrar "Error al transcribir" por 2s. Volver a Pantalla 2. |
| Chat falla | Mostrar "Error del servidor" por 2s. Volver a Pantalla 2. |
| Descarga MP3 falla | Mostrar "Error al descargar audio" por 2s. Volver a Pantalla 2. |
| PSRAM insuficiente | Mostrar "Error de memoria" y reiniciar dispositivo. |
| Timeout de API (20s) | Tratar igual que error HTTP. |

---

## 16. Deteccion de Touch

### Algoritmo de Deteccion en setup():
```cpp
bool detectTouch() {
    // Intentar leer registro 0x00 del FT6336 en I2C 0x38
    uint8_t data;
    if (i2c_read(0x38, 0x00, &data, 1) == ESP_OK) {
        return true;  // FT6336 presente
    }
    return false;     // Sin touch
}
```

Si `detectTouch() == false`:
- Las pantallas con botones (Pantalla 1 y Pantalla 4) no muestran botones tactiles sino indicadores de cual opcion esta seleccionada, navegables solo con BOOT.
- En Pantalla 2b, no hay interaccion tactil, solo botones fisicos.

---

## 17. Decision de Libreria MP3: minimp3

**Libreria seleccionada: minimp3**

**Justificacion:**
- Single-header C (~1500 lineas), sin dependencias.
- Desarrollada especificamente para sistemas embebidos con recursos limitados.
- Decodifica MP3 a PCM 16-bit en punto flotante o entero.
- Probada extensivamente en ARM, ESP32 y otros microcontroladores.
- Peso minimo en flash (~30KB).
- No requiere sistema de archivos ni threading.
- Compatible con C++ (envuelta en `extern "C"`).
- La alternativa ESP8266Audio es mas pesada, orientada a streaming con buffer circular que no necesitamos (descargamos el archivo completo).

**Fuente:** https://github.com/lieff/minimp3 (archivo `minimp3.h` — dominio publico / CC0).

---

## 18. Renderizado de Pantallas con LVGL

### Configuracion LVGL

```cpp
// user_config.h
#define EPD_WIDTH  200
#define EPD_HEIGHT 200
#define EXAMPLE_LVGL_TICK_PERIOD_MS    5
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 100
#define LVGL_SPIRAM_BUFF_LEN (EPD_WIDTH * EPD_HEIGHT * 2)  // 80000 bytes
```

### Flush Callback (LVGL → E-Paper)

```cpp
void lvgl_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_map) {
    // 1. Limpiar buffer local del driver e-paper
    // 2. Para cada pixel en el area modificada:
    //    - Convertir RGB565 a 1-bit: threshold en 0x7FFF
    //    - Si < 0x7FFF → negro (0x00)
    //    - Si >= 0x7FFF → blanco (0xFF)
    //    - Llamar driver->EPD_DrawColorPixel(x, y, color)
    // 3. driver->EPD_DisplayPart()
    // 4. lv_disp_flush_ready(drv)
}
```

### Creacion de Pantallas

Cada pantalla es una funcion que construye la UI con widgets LVGL:

```cpp
lv_obj_t* create_screen_1_active(bool hasTouch, bool uuidIsNull);
lv_obj_t* create_screen_2_record();
lv_obj_t* create_screen_2b_listening();
lv_obj_t* create_screen_3_sending();
lv_obj_t* create_screen_3b_discarded();
lv_obj_t* create_screen_4_confirm(const char* transcribedText, bool hasTouch);
lv_obj_t* create_screen_5_waiting();
lv_obj_t* create_screen_6_response(const char* agentText);
```

Cada funcion:
1. Crea un `lv_obj_t* screen = lv_obj_create(NULL)`.
2. Si la pantalla lleva barra de estado, llama a `create_status_bar(screen)`.
3. Agrega labels, botones, etc. segun corresponda.
4. Retorna el objeto pantalla.

La maquina de estados principal carga la pantalla activa con `lv_scr_load(screen)` y destruye la anterior con `lv_obj_del(old_screen)`.

### Barra de Estado

```cpp
void create_status_bar(lv_obj_t* parent) {
    // Label "WIFI: OK" arriba-izquierda
    // Icono de bateria + label "XX%" arriba-derecha
    // Linea separadora horizontal
}
```

Se actualiza periodicamente actualizando el texto de los labels.

---

## 19. Flujo de Datos Completo (End-to-End)

```
[Usuario habla] → MIC → ES8311 ADC → I2S → Buffer PSRAM (PCM raw)
    → Codificar header WAV → Buffer WAV en PSRAM
    → POST /api/audio/transcribe (WAV)
    → Respuesta: {"text": "..."}
    → Mostrar Pantalla 4 (confirmacion)
    → [Usuario confirma] → POST /api/chat/message (JSON)
    → Respuesta: {"agent_text": "...", "audio_url": "/api/audio/files/x.mp3"}
    → Mostrar Pantalla 6 (texto respuesta)
    → GET /api/audio/files/x.mp3 → Buffer MP3 en PSRAM
    → minimp3: decodificar MP3 → PCM
    → esp_codec_dev_write(pcm) → I2S → ES8311 DAC → PA → Parlante
    → Fin reproduccion → Liberar buffer MP3 y textos de respuesta
    → Pantalla 2 (listo para siguiente mensaje)
```

---

## 20. Versionado del Firmware

El firmware debe incluir una constante de version:

```cpp
#define FIRMWARE_VERSION "1.0.0"
```

Se muestra en el monitor serial al iniciar.

---

## 21. Notas de Implementacion

1. **Todas las operaciones de red deben ser asincronas** respecto a la UI. Mientras se espera respuesta HTTP, la UI debe seguir respondiendo (al menos para mostrar el estado actual).

2. **El audio grabado se almacena en PSRAM**, no en flash ni SD. Esto implica un limite practico de ~30 segundos de grabacion. Si el buffer se llena antes, se trunca silenciosamente.

3. **El archivo WAV se construye en memoria** agregando un header de 44 bytes al inicio del buffer PCM. No se escribe en storage.

4. **El archivo MP3 descargado se almacena integramente en PSRAM** antes de iniciar la decodificacion. Esto simplifica el manejo de streaming y la integracion con minimp3.

5. **La conexion WiFi se establece una vez al despertar** y se mantiene durante toda la sesion activa. Si se pierde, se reconecta automaticamente.

6. **Las pantallas de e-paper se renderizan en modo partial refresh** para minimizar el parpadeo durante la operacion normal. El **full refresh** (limpieza completa usando `EPD_Display()` con el LUT completo y pantalla en blanco) se ejecuta exclusivamente al entrar en deep sleep, para dejar el display limpio y prevenir ghosting acumulativo.

7. **El LED (GPIO 3) parpadea durante operaciones de red** como indicador visual para debug (200ms on/off durante WiFi connecting, apagado cuando idle).

8. **Los widgets LVGL se destruyen al cambiar de pantalla** para liberar memoria. No se reciclan entre pantallas.

9. **No se usa SD card** en esta version del firmware. Todo el almacenamiento temporal es en PSRAM.

10. **Al abandonar la Pantalla 6** (por fin de reproduccion, PWR o cualquier otra transicion), se debe ejecutar una rutina de limpieza que libere: (a) el buffer del MP3 descargado en PSRAM, (b) el buffer que contiene la respuesta JSON del STT, y (c) el buffer que contiene la respuesta JSON del chat. Esto evita fugas de memoria entre ciclos de conversacion.

11. **El boton BOOT en Pantalla 6** reinicia la reproduccion desde el inicio sin volver a descargar el archivo MP3. Para lograrlo, se reinicia el puntero de lectura al inicio del buffer MP3 ya descargado y se reinicializa el decodificador `mp3dec_init()`. Si el buffer MP3 ya fue liberado (porque la reproduccion anterior termino), este boton no tiene efecto.

---
Documento listo para implementacion. Version 1.0.
