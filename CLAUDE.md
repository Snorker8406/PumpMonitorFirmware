# PumpMonitorFirmware — Guía para agentes (CLAUDE.md)

Firmware v2 del dispositivo PumpMonitor (ESP32 / WT32-ETH01). Contexto general
del proyecto y contrato con el backend: `../CLAUDE.md` y `../docs/CONTRATO_MQTT.md`.

> `docs/DECISIONS.md` es **normativo**: si algo aquí lo contradice, gana
> `DECISIONS.md`. `docs/CHECKLIST.md` es la lista de revisión que debe pasar
> todo cambio, incluido el generado por IA.

---

## 1. Hardware objetivo

Placa **WT32-ETH01**: ESP32 + PHY Ethernet LAN8720 (sin WiFi en uso).

| Periférico | Pines |
|---|---|
| Ethernet LAN8720 | PHY addr 1, power 16, MDC 23, MDIO 18, clock `GPIO0_IN` (oscilador externo de 50 MHz) |
| RTC DS3231 (I²C) | SDA 33, SCL 5 |
| Tarjeta SD (SPI) | CS 2, MOSI 17, SCK 14, MISO 35 · 4 MHz |
| LED testigo de estado | GPIO4, activo-alto · `docs/STATUS_LED.md` |

Todo esto vive en `include/app_config.hpp`. No disperses pines ni constantes por
el código.

La **placa RevB** (ESP32-WROOM-32E, LAN8720A, DS3231 y microSD soldados, sin
módulos) usa esta misma asignación de pines y el mismo binario. Antes de mover un
pin o cambiar el modo de reloj, lee `docs/HARDWARE_REVB.md`.

---

## 2. Compilar y cargar

PlatformIO, entorno único `wt32-eth01`:

```bash
pio run                      # compilar
pio run -t upload            # cargar por USB/serial
pio device monitor -b 115200 # monitor serie
```

Dependencias: `PubSubClient`, `eModbus` v1.7.4, `RTClib`. Flag de build
`-DENABLE_DEBUG_LOG=1`.

En producción el firmware se actualiza **por OTA**: se publica la versión en
`device/{MAC}/installFirmware` y el dispositivo descarga el binario del backend.
`kFirmwareVersion` en `app_config.hpp` es la versión que el device reporta —
súbela en el mismo commit en que generes un binario nuevo.

---

## 3. Estructura real del código

```
include/*.hpp     un header por manager + app_config.hpp + log.hpp
src/*.cpp         implementación; src/main.cpp crea las tareas FreeRTOS
legacy/           firmware anterior, SOLO referencia funcional
docs/             decisiones, arquitectura, checklist y guías por subsistema
```

**Patrón vigente:** cada subsistema es un *manager* singleton con
`Manager::instance()`, `begin()` y una API pública acotada; `main.cpp` los
arranca y les da una tarea FreeRTOS con núcleo, prioridad, stack y periodo
definidos en `app_config.hpp`.

> `docs/ARCHITECTURE.md` propone una estructura por capas
> (`app/` · `services/` · `drivers/`). Hoy **no** está implementada: el código es
> plano por manager. Es un objetivo, no el estado actual — no asumas que existen
> esas carpetas ni las introduzcas a medias en un cambio puntual.

### Managers

| Manager | Qué hace |
|---|---|
| `NetworkManager` | Ethernet, DHCP o IP fija, estado de enlace |
| `MqttManager` | TLS + despacho de comandos + publicación de respuestas |
| `ModbusManager` | Maestro TCP: lectura de registros y coils, FC05/FC06 |
| `ModbusServerManager` | Esclavo TCP: convierte escrituras externas en eventos |
| `ActuatorManager` | Coils de arranque/paro con secuencia de confirmaciones |
| `SdManager` | Archivos diarios de datos y errores, backups |
| `RtcManager` | DS3231, timestamps Unix |
| `EepromManager` | Configuración persistente en NVS (`Preferences`) |
| `OtaManager` | Descarga e instalación de firmware |
| `StatusLedManager` | LED testigo: estados de error en secuencias de parpadeo |

---

## 4. Reglas al escribir código aquí

- **Nada de `delay()` ni de lógica temporal con `millis()` dispersa.** El tiempo
  lo gobiernan las tareas FreeRTOS y sus periodos configurados.
- **Nada de números mágicos**: toda constante va a `app_config.hpp`.
- **Nada de `Serial.print` suelto**: usa `LOGE/LOGW/LOGI/LOGD` de `log.hpp`.
  ERROR y WARN se escriben además en `/error/err_YYYYMMDD.txt` de la SD.
- **No copies código de `legacy/`.** Se conserva como *ground truth* de
  comportamiento (protocolos, tiempos, formatos), nunca como referencia de
  diseño.
- **Configuración nueva → EEPROM, no hardcode.** Si un parámetro debe poder
  cambiarse en campo, agrégalo a `EepromManager` (clave corta, getter, setter con
  validación de rango) y expón un comando MQTT para leerlo y escribirlo.
- **Toda escritura a MQTT/SD desde una tarea nueva exige revisar el stack**: la
  publicación TLS consume mucho (por eso `kMqttTaskStackWords` = 12288 y el
  servidor Modbus usa lo mismo).
- El acceso a Modbus y a la SD es concurrente: respeta los mutex existentes, no
  llames a un manager desde una ISR.

---

## 5. Al tocar el protocolo

Cualquier cambio en topics o payloads **rompe el backend en silencio** (no hay
esquema ni validación en el canal MQTT). Por eso:

1. Actualiza `../docs/CONTRATO_MQTT.md` en el mismo commit.
2. Revisa si el consumidor equivalente ya existe en `PumpMonitorSystem` y
   menciónalo en el mensaje del commit.
3. Nombres de comando nuevos: que no sean subcadena de otro comando existente
   (el despacho es por `strstr`, ver el caveat en el contrato).

---

## 6. Formato de datos en SD

- `/data/YYYYMMDD.txt` → `{timestamp},{device},{registrosHex}` (4 dígitos hex por
  registro, sin separadores). Detalle en `docs/DATA_FORMAT.md`.
- `/error/err_YYYYMMDD.txt` → `[YYYY-MM-DD HH:MM:SS] [NIVEL] mensaje`. Detalle en
  `docs/ERROR_LOGGING.md`.

Los archivos rotan por día y **no se purgan automáticamente**.
