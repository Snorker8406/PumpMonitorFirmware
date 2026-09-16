# Changelog — PumpMonitorFirmware

Formato basado en [Keep a Changelog](https://keepachangelog.com/es/1.1.0/).
Rama de trabajo: `main`.

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
