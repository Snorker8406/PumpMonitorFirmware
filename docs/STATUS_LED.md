# LED testigo de estado y error

Un solo LED revela en qué estado está el equipo mediante secuencias de parpadeo.
Pensado para leerse desde la puerta del tablero, sin laptop y sin red: es el
único diagnóstico disponible cuando justamente lo que falló es la comunicación.

Implementación: `StatusLedManager` (`include/status_led_manager.hpp`,
`src/status_led_manager.cpp`) y la tarea `status-led-task` de `src/main.cpp`.

---

## 1. Hardware

| Qué | Valor | Constante |
|---|---|---|
| Pin | **GPIO4** (header pin 24, serigrafía `IO4`) | `kStatusLedPin` |
| Polaridad | activo-alto | `kStatusLedActiveLow` |

Cableado por defecto:

```
GPIO4 ──[ R ]──▶|── GND
```

El ánodo del LED va hacia GPIO4 y el cátodo a GND; la resistencia en serie, de
cualquiera de los dos lados. `digitalWrite(HIGH)` enciende.

**Placa RevA** (`hardware/RevA`, proyecto EasyEDA `PumpMonitor_RevA`): `D3` es un
LED rojo difuso de **5 mm de inserción** (XL-502SURD, LCSC C2895492) con
`R1` = **220 Ω** en serie (0603, LCSC C22962), lo que da ~6 mA. La altura a la
que queda el LED sobre la placa se decide al soldarlo, según el case.

**Placa RevB** (`HARDWARE_REVB.md`): mismo LED y misma resistencia, también en
GPIO4.

El valor de R depende del color, porque solo hay 3.3 V disponibles:

| LED | Vf típica | R | Corriente |
|---|---|---|---|
| Rojo / ámbar | ~2.0 V | 220 Ω | ~6 mA (RevA) |
| Rojo / ámbar | ~2.0 V | 330 Ω | ~4 mA |
| Rojo / ámbar | ~2.0 V | 1 kΩ | ~1.3 mA (prototipo con módulos; se ve poco de día) |
| Verde / azul / blanco | ~3.0 V | 100 Ω | ~3 mA |

**Usa rojo o ámbar.** Con verde, azul o blanco la Vf casi iguala los 3.3 V: con
330 Ω apenas pasa 1 mA y el testigo queda demasiado opaco para verse de día.

Cualquiera de esos valores está muy por debajo del límite cómodo del ESP32
(12 mA por pin; 40 mA es el máximo absoluto), así que el GPIO maneja el LED
directo, sin transistor.

Para un testigo montado en la puerta del tablero, **no cuelgues un cable largo
del GPIO**: un metro de cable en un tablero con variadores es una antena de EMI.
Usa un MOSFET pequeño (BSS138 / 2N7002) o un optoacoplador y alimenta el LED de
la fuente del tablero. Si el driver invierte la señal, pon
`kStatusLedActiveLow = true`.

### Por qué GPIO4 y no otro

El WT32-ETH01 solo saca **15 GPIO** al header: 0, 1, 2, 3, 4, 5, 12, 14, 15, 17,
32, 33, 35, 36, 39. De esos, **35, 36 y 39 son solo-entrada** y no pueden manejar
un LED. El firmware ya ocupa 2, 5, 14, 16, 17, 18, 23, 33, 35 (ver §1 de
`../CLAUDE.md`), y 1/3 son el monitor serie.

Queda muy poco, y GPIO4 es el único libre **sin** rol de strapping, flash, JTAG
ni UART. Al reset queda como entrada flotante, así que el LED arranca apagado
por sí solo, antes de que corra `pinMode()`.

**No muevas el LED a estos pines sin leer esto:**

| Pin | Problema |
|---|---|
| GPIO0 | Strapping de modo flash (LOW al reset = modo programación). Varias fuentes de la comunidad apuntan a que en algunas revisiones de esta placa lleva el reloj de 50 MHz del PHY. |
| GPIO2 | Strapping; además es el CS de la SD. |
| GPIO12 | Strapping MTDI: HIGH al reset selecciona flash de 1.8 V → brownout, el equipo no arranca. |
| GPIO15 | Strapping MTDO (pull-up interno) y JTAG TDO. **Sirve como alternativa, pero solo con cableado activo-bajo** (3V3 → resistencia → ánodo; cátodo → GPIO15), que deja el pin alto en el boot. Con cableado activo-alto, la resistencia a GND lo puede leer LOW al reset: se silencia el log de arranque y puede cambiar el modo de boot. |
| GPIO32 | Eléctricamente perfecto, pero en el firmware legacy era `relay_start` (`legacy/src/PumpMonitorHardware.ino:43`). En un tablero retrofit puede ir al opto de un relé de arranque: un parpadeo de "error" arrancaría la bomba. **No lo uses sin verificar la placa física.** |
| GPIO17 | MOSI de la SD. (Hasta el cambio a `ETH_CLOCK_GPIO0_IN` también estaba declarado como reloj del PHY.) |

**Antes de soldar, revisa el tablero.** En el firmware legacy GPIO4 era `TXD2`
del camino serie RS-485 (`legacy/src/PumpMonitorHardware.ino:40`). El firmware
v2 solo usa Modbus **TCP**, así que el pin está libre a nivel software, pero si
la placa tiene un MAX485 con `DI` en GPIO4, el LED estaría moviendo la entrada de
datos del transceptor. Con `DE` desactivado (como en el legacy, donde
`MAX485_RE_NEG` está comentado) es inocuo, pero conviene confirmarlo.

---

## 2. Secuencias

Los códigos de falla arrancan en **2 destellos** a propósito: un grupo de un solo
destello se confunde con el latido de operación normal, y este LED se lee a
oscuras.

| Estado | Secuencia | Significado |
|---|---|---|
| `Booting` | **encendido fijo** | `setup()` en curso. Se apaga al arrancar las tareas. |
| `BusyMaintenance` | **parpadeo rápido** (5 Hz continuo) | OTA o subida de respaldo: las tareas están suspendidas. **No cortar la alimentación.** |
| `EthDown` | **2 destellos** + pausa | Sin enlace Ethernet o sin IP. Revisa cable, switch y DHCP. |
| `MqttDown` | **3 destellos** + pausa | Hay red pero no hay sesión con el broker. Revisa credenciales, puerto 8883, firewall, y la vigencia del certificado TLS (`../docs/RENOVACION_TLS.md`). |
| `StorageFault` | **4 destellos** + pausa | SD no disponible o RTC no encontrado. El equipo sigue publicando por MQTT pero **no está guardando histórico local**. |
| `ModbusFault` | **5 destellos** + pausa | Los esclavos Modbus no responden. Revisa IPs, el analizador/variador y el `unitId`. |
| `Ok` | **destello corto** cada 3 s | Operación normal. Distingue "todo bien" de "equipo sin alimentación". |

Tiempos: destello de 150 ms, 250 ms entre destellos del grupo, 1.5 s de pausa
entre grupos; latido de 50 ms cada 3 s. Todo en `app_config.hpp`
(`kStatusLedPulseOnMs`, `kStatusLedPulseOffMs`, `kStatusLedGroupPauseMs`,
`kStatusLedHeartbeatOnMs`, `kStatusLedHeartbeatPauseMs`, `kStatusLedOtaBlinkMs`).

### Prioridad

Si hay varias condiciones activas, **el LED muestra solo la más grave**: gana la
primera del enum `DeviceStatus`, que está ordenado de mayor a menor prioridad.
Es deliberado: sin red tampoco hay broker, y mostrar "no hay broker" cuando el
cable está desconectado manda al técnico a revisar lo que no es.

Para ver *todas* las condiciones activas hay que ir al log de errores de la SD
(`docs/ERROR_LOGGING.md`), que no filtra nada.

---

## 3. Cómo se alimentan los estados

Las condiciones son **estado, no eventos**: se marcan y se limpian por separado,
así que un fallo que ya se recuperó deja de mostrarse sin que nadie tenga que
acordarse de apagarlo. Un `LOGE` viejo en la SD no enciende el testigo.

Cada condición la reporta el dueño del recurso:

| Condición | Quién la reporta | Cada cuánto |
|---|---|---|
| `EthDown` | `statusLedTask` — `NetworkManager::isConnected()` es una lectura de bandera, segura de consultar | cada tick (25 ms) |
| `StorageFault` | `statusLedTask` — `SdManager::isAvailable()` / `RtcManager::isAvailable()`, también banderas | cada tick (25 ms) |
| `MqttDown` | `mqttTask` — **tiene que ser esa tarea**: `PubSubClient` no es thread-safe y no se puede consultar desde la tarea del LED | cada `kMqttLoopDelayMs` (200 ms) |
| `ModbusFault` | `instantValuesTask`, con el resultado de `readAllDevices()` | cada intervalo de Instant Values |
| `BusyMaintenance` | `OtaManager::suspendAllTasks()` / `resumeAllTasks()` | al entrar y salir |
| `Booting` | `StatusLedManager::begin()` lo marca; `setup()` lo limpia al final | una vez |

Notas de diseño:

- **`ModbusFault` se alimenta solo de Instant Values**, que es la telemetría
  siempre activa. La tarea de Real Time es un modo transitorio y a demanda; que
  falle una lectura ahí no es el estado del equipo.
- **El enganche del OTA está en `suspendAllTasks()` / `resumeAllTasks()`**, no en
  `performUpdate()`: es el único punto por el que pasan tanto el OTA como la
  subida de respaldo (`MqttManager::processPendingBackupUpload()` también
  suspende las tareas, y termina en `ESP.restart()`).
- **La tarea del LED no se registra en `OtaManager::registerTasks()`** a
  propósito: durante una actualización es justo cuando el testigo tiene que
  seguir vivo. Es seguro, porque no toca red ni SD.

---

## 4. Agregar una condición nueva

1. Pon el valor en `DeviceStatus` (`status_led_manager.hpp`) **en el lugar que le
   toca por prioridad**, nunca al final: el orden del enum *es* la prioridad.
   `Ok` y `Count` van siempre al final.
2. Agrega su patrón en `kStatusPatterns` y su nombre en `kStatusNames`
   (`status_led_manager.cpp`), en la misma posición. Dos `static_assert`
   fallan la compilación si se te olvida uno.
3. Elige un número de destellos que no choque con los existentes y documéntalo
   en la tabla de §2.
4. Llama a `setStatus(DeviceStatus::X, activo)` desde el dueño del recurso, con
   el valor **actual** de la condición en cada vuelta de su bucle — no solo
   cuando falla.

`setStatus()` es segura desde cualquier tarea (máscara atómica). **No la llames
desde una ISR.**
