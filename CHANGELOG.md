# Changelog — PumpMonitorFirmware

Formato basado en [Keep a Changelog](https://keepachangelog.com/es/1.1.0/).
Rama de trabajo: `main`.

## [DEV.11] — 2026-11-05

### Fixed

- **`111` / `000` ya no se alcanzan sin la confirmación del PLC.** `setConfirmation`
  movía el último switch antes de escribir el coil, así que el device reportaba
  arranque o paro aunque la escritura fallara o el PLC nunca confirmara. Además, como
  no se publicaba `coilStatus`, la UI seguía en `110` y un segundo clic entraba como
  idempotente y publicaba `1,111` sin escribir nada.

  - El último paso (`110` → `111`, `001` → `000`) encola la escritura y deja el estado
    en `110` / `001`. Solo `handleConfirmationValue` lleva a `111` / `000`.
  - Mientras espera, se rechaza cualquier `confirmCoil` de ese coil: ni doble
    escritura ni retroceso a media operación. La espera se marca al encolar para que
    un FC06 inmediato no se adelante.
  - Si `writeCoil` falla o vence `kActuatorConfirmTimeoutMs` (30 s), el coil se
    desbloquea sin cambiar de estado y se republica `coilStatus`. Una confirmación
    tardía se aplica igual.

  Topics y payloads no cambian; cambia la semántica de `111` / `000`
  (`../docs/CONTRATO_MQTT.md` §3.4). SPA y app móvil funcionan sin cambios: el spinner
  se libera a los 5 s, y si la confirmación tarda más el firmware rechaza el reintento.

### Changed

- **`reg[0] = 0` ya no confirma ningún coil** (`kActuatorConfirmValueUnset`): el backend
  manda `0` en los slots vacíos, así que un PLC que limpiara el registro habría
  confirmado ese coil.
- **Aviso de valores de confirmación repetidos.** `reloadConfig` registra un `WARN` si
  un valor se repite: `handleConfirmationValue` resuelve el coil por valor y gana el
  primero que coincide. Solo avisa; no rechaza la configuración.

## [DEV.10] — 2026-11-03

### Added

- **Comando MQTT `reset`.** El botón "Reiniciar dispositivo" de `devices.tsx`
  publicaba `device/{MAC}/reset` desde siempre, pero el firmware no tenía
  handler: el mensaje se registraba y se perdía en silencio al final de la
  cadena `if / else if` de `MqttManager::messageCallback`. El dispositivo nunca
  reiniciaba y el navegador se quedaba esperando hasta su timeout de 2 minutos.

  - Rama nueva al final de la cadena (`strstr(topic, "/reset")`), colocada
    última a propósito: ningún otro comando contiene `/reset` como subcadena ni
    es contenido por él, así que no captura mensajes ajenos.
  - El payload se ignora (el navegador publica vacío).
  - **No publica ACK propio.** Al reconectar, `connectInternal()` ya publica
    `PumpMonitorSystem/starting` con `mac` y `deviceTime`, que es exactamente lo
    que `devices.tsx` empareja contra `expectedResetRef` para cerrar la espera.
    Agregar un ACK antes del reinicio habría sido un segundo contrato para la
    misma confirmación.

### Changed

- **Rastro de comandos sin handler.** La cadena de despacho termina ahora en un
  `else` con `LOGW("MQTT: Unhandled command topic [%s]")`, que por el nivel WARN
  queda también en `/error/err_YYYYMMDD.txt` de la SD. El canal MQTT no tiene
  esquema ni validación, así que un topic mal escrito era indistinguible de un
  handler que rechazó el payload. La suscripción es `device/{MAC}/#` y todas las
  publicaciones del device usan `device/{MAC}_...`, nunca `device/{MAC}/...`, de
  modo que el `else` no se dispara con los propios mensajes del firmware.
- **`kMqttPreRestartDelayMs` (500 ms) en `app_config.hpp`.** Los tres
  `delay(500)` que precedían a un `ESP.restart()` en `mqtt_manager.cpp`
  (`saveNetworkConfig` DHCP, `saveNetworkConfig` IP fija y el cierre de
  `backupUpload`) pasan a `vTaskDelay(pdMS_TO_TICKS(kMqttPreRestartDelayMs))`:
  deja de ser un número mágico repetido y de bloquear con `delay()`. Los otros
  dos `delay(500)` del archivo esperan liberación de memoria antes de OTA y
  respaldo — otra semántica, se dejaron como estaban.

## [DEV.09] — 2026-11-02

### Added

- **LED testigo de estado y error en GPIO4.** Un solo LED revela en qué estado
  está el equipo con secuencias de parpadeo, legibles desde la puerta del
  tablero sin laptop y sin red — que es justo cuando hace falta, porque lo que
  suele fallar es la comunicación.

  - `StatusLedManager` nuevo (`include/status_led_manager.hpp`,
    `src/status_led_manager.cpp`): dueño único de `kStatusLedPin`, máscara
    atómica de condiciones y tabla de patrones. `setStatus()` es segura desde
    cualquier tarea.
  - Tarea `status-led-task` (core 1, prioridad 1, stack 2048 words, tick 25 ms).
  - Códigos: fijo = arrancando · parpadeo rápido = OTA/respaldo en curso ·
    2 = sin Ethernet · 3 = sin broker MQTT · 4 = SD/RTC · 5 = Modbus ·
    destello corto cada 3 s = operación normal. Arrancan en 2 destellos a
    propósito: un grupo de 1 se confunde con el latido.
  - Si hay varias condiciones activas se muestra **solo la más grave** (el orden
    de `DeviceStatus` es la prioridad). El log de la SD sigue mostrando todas.
  - Las condiciones son **estado, no eventos**: un fallo recuperado deja de
    mostrarse solo. Cada una la reporta el dueño del recurso — `MqttDown` desde
    `mqttTask` porque `PubSubClient` no es thread-safe, `ModbusFault` desde
    `instantValuesTask` (la telemetría siempre activa, no Real Time), y
    `BusyMaintenance` desde `OtaManager::suspendAllTasks()`/`resumeAllTasks()`,
    que es el único punto por el que pasan tanto el OTA como la subida de
    respaldo.
  - La tarea del LED **no** se registra en `OtaManager::registerTasks()`:
    durante una actualización es cuando el testigo tiene que seguir vivo.

  **Por qué GPIO4:** el WT32-ETH01 solo saca 15 GPIO al header, 3 de ellos
  solo-entrada, y el firmware ya ocupa 9. GPIO4 es el único libre sin rol de
  strapping, flash, JTAG ni UART, y al reset queda flotante, así que el LED
  arranca apagado por sí solo. Pines a evitar, alternativas y el cableado
  recomendado (incluido el MOSFET/opto para un testigo en la puerta del
  tablero): `docs/STATUS_LED.md`.

  **Pendiente de hardware:** el firmware ya maneja el pin, pero el LED todavía
  no está soldado en ningún equipo. Antes de hacerlo hay que verificar que
  GPIO4 no esté cableado al `DI` de un MAX485 en el tablero (en el legacy era
  `TXD2`).

## [DEV.08] — 2026-09-16

### Added

- **Reloj del sistema por SNTP**, para que mbedTLS valide también las **fechas**
  del certificado del broker. Hasta ahora el ESP32 arrancaba en 1970 y, sin
  hora, mbedTLS **omitía** esa comprobación: aceptaba un certificado vencido o
  suplantado mientras la cadena encajara.

  - `NetworkManager::beginTimeSync()` arranca el cliente SNTP y no bloquea.
  - `NetworkManager::waitForSystemClock()` espera hasta 10 s a que llegue la
    hora; si no llega se continúa igual y SNTP sigue reintentando por su cuenta.
  - Se llaman desde `setup()`, con la red lista y antes de crear las tareas.
  - `configTime(0, 0, …)`, o sea UTC sin horario de verano, que es contra lo que
    mbedTLS compara `notBefore` y `notAfter`.
  - `kEnableNtpTimeSync` en `app_config.hpp` permite desactivarlo y recompilar.

  **Por qué no se usa el DS3231:** guarda hora **local sin zona** (la SPA envía
  `adjustDeviceTime` con la hora del navegador tal cual), así que tomarlo como
  UTC dejaría el reloj corrido varias horas y el equipo podría rechazar un
  certificado recién emitido por verlo "en el futuro". El RTC sigue siendo la
  fuente de los timestamps de la aplicación y no se toca.

  **Es el tercer intento.** `DEV.05` y `DEV.06` llevaban este mismo cambio y
  dejaban el equipo en bucle de arranque, pero la causa no era el reloj: era el
  coste de verificar la cadena, arreglado en `DEV.07`. Ahora se reintenta sobre
  una base que ya arranca de forma estable.

### Changed

- Efecto secundario deseado: el respaldo de `SdManager` que usa el reloj del
  sistema cuando el RTC no está disponible (`generateFilename`) pasa de generar
  nombres con fecha de 1970 a fechas reales en UTC.

### Nota de operación

A partir de esta versión el dispositivo **rechaza** un certificado vencido en
lugar de aceptarlo. Es lo correcto, pero implica que si la renovación del broker
vuelve a fallar, los equipos se quedan sin MQTT. La renovación automática quedó
arreglada el 2026-09-16; ver `../docs/RENOVACION_TLS.md`.

## [DEV.07] — 2026-09-16

### Fixed

- **Bucle de arranque al conectar con el certificado nuevo del broker.** El
  watchdog de tareas abortaba con `IDLE0` sin refrescar y `mqtt-task` colgada en
  CPU 0, siempre justo después de `MQTT connecting to …`.

  **Causa: coste de la verificación de la cadena.** Desde que el broker rotó a
  `YE1`, la cadena que sirve es toda ECDSA P-384 y tiene cuatro certificados:

  ```
  hoja(P-256) -> YE1(P-384) -> Root YE(P-384) -> ISRG Root X2(P-384) -> ISRG Root X1(RSA-4096)
  ```

  Con solo las **raíces** como ancla (`DEV.04`), mbedTLS debe verificar **tres**
  firmas P-384. El ESP32 no tiene acelerador de curva elíptica, así que eso son
  más de 10 s de CPU seguidos dentro de una única llamada a
  `mbedtls_ssl_handshake()`, que no cede el procesador. `IDLE0` se quedaba sin
  tiempo y el watchdog (5 s por defecto) reiniciaba el equipo.

  **Arreglo, en dos partes:**

  1. `kMqttCaCert` vuelve a incluir el **intermedio** (`YE1`), y primero en la
     lista. Así el caso normal es **una sola** verificación P-384, el mismo
     coste que tenía el firmware original con `E7` y que funcionó durante meses.
     Las raíces ISRG Root X1 y X2 se conservan detrás como respaldo por si Let's
     Encrypt rota el intermedio.
  2. `esp_task_wdt_init(kTaskWdtTimeoutSec, true)` al inicio de `setup()` sube
     el watchdog de 5 s a 30 s, para que el camino lento de respaldo tampoco
     pueda reiniciar el equipo.

  **Por qué `DEV.04` pareció funcionar tras rotar el certificado:**
  `emqx ctl listeners restart` reinicia el aceptador pero **no cortó la conexión
  TLS ya establecida**, que seguía usando el certificado anterior. El equipo no
  rehízo el handshake contra la cadena nueva hasta el siguiente corte de
  alimentación, y ahí apareció el fallo.

### Vigencias de las anclas

| Certificado | Vence |
|---|---|
| Let's Encrypt `YE1` (intermedio) | 02/09/2028 |
| ISRG Root X1 | 04/06/2035 |
| ISRG Root X2 | 17/09/2040 |

## [DEV.04] — 2026-09-16

### Changed

- **Ancla de confianza TLS del broker MQTT** (`include/app_config.hpp`):
  `kMqttCaCert` dejó de embeber la hoja de `mqtt.agrotecsa.com.mx` y pasó a
  embeber las **raíces** ISRG Root X1 y X2.

  **Por qué:** fijar la hoja ataba el firmware a un certificado que caduca cada
  90 días. El 16/09/2026 el broker pasó de `E7` a `YE1`, y con la configuración
  anterior el dispositivo habría dejado de conectar sin posibilidad de
  recuperarlo en remoto, porque `installFirmware` viaja por MQTT.

  > ⚠️ Esta versión **no arranca** contra el certificado nuevo: la verificación
  > de la cadena desde la raíz tarda demasiado y dispara el watchdog. Corregida
  > en `DEV.07`, que añade el intermedio. No la uses.

### Descartado

- `DEV.05` y `DEV.06` intentaron ajustar el reloj del sistema por SNTP para que
  mbedTLS validara también las **fechas** del certificado. Ambas fallaron con el
  mismo bucle de arranque, pero **la causa no era el reloj**: era el coste de la
  cadena, que ya estaba presente en `DEV.04`. El ajuste de reloj sigue
  pendiente y debería reintentarse **sobre `DEV.07`**, ahora que el arranque es
  estable. Ver `../docs/RENOVACION_TLS.md` §9.3.

### Pendiente en su momento

- Quedaba sin ajustar el reloj del sistema, así que mbedTLS validaba la cadena
  pero no las fechas. **Resuelto en `DEV.08`.**
