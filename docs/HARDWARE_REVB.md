# Placa RevB — hardware propio sin módulos

La RevB sustituye el conjunto de pruebas (WT32-ETH01 + módulo RTC + módulo
microSD) por una sola placa con los componentes soldados directamente: el ESP32,
el PHY Ethernet, el RTC y el zócalo de la SD.

**El firmware es el mismo.** La RevB conserva la asignación de pines del
WT32-ETH01, así que el binario del entorno `wt32-eth01` corre sin cambios y no
hay un entorno de PlatformIO aparte.

> **Estado (2026-11-19): diseñada y verificada en papel, sin fabricar ni probar.**
> El netlist se cotejó contra `app_config.hpp` y contra las hojas de datos, pero
> nada de esto sustituye a encender la placa. Ver §8.

Archivos de fabricación (Gerber, BOM, posiciones): carpeta `hardware/RevB` del
contenedor del proyecto, **fuera de este repo**. Fuente: proyecto EasyEDA Pro
`PumpMonitorSystem`, board `pumpmonitor_revb` (esquemático `PumpMonitor_RevB`,
PCB `PCB_RevB`).

---

## 1. Resumen

| Qué | Valor |
|---|---|
| Tamaño | 60 × 80 mm, 4 capas (planos internos de GND y 3V3) |
| MCU | ESP32-WROOM-32E-N4 (`U2`), 4 MB de flash, sin PSRAM |
| Ethernet | LAN8720A (`U5`) + oscilador de 50 MHz (`X1`) + RJ45 HR911105A (`J4`) |
| RTC | DS3231SN (`U3`) + portapilas CR2032 (`BT1`) |
| Almacenamiento | zócalo microSD TF-01A (`J2`), en SPI |
| Alimentación | 24 V → B2405S-2WR3 aislado (`U1`) → 5 V → AMS1117-3.3 (`U4`) |
| Testigo | LED rojo de 5 mm (`D3`) en GPIO4 · `STATUS_LED.md` |
| Serigrafía | logo de AgroTECSA en la cara inferior |

---

## 2. Asignación de pines

Idéntica a la de `include/app_config.hpp`. Si cambia un pin ahí, la RevB deja de
ser compatible.

| GPIO | Red | Destino | Constante |
|---|---|---|---|
| 0 | `BOOT_IO0` | reloj de 50 MHz (por `R12`, 33 Ω), botón BOOT, `J3` | `kEthClockMode` |
| 2 | `SD_CS` | microSD CS · pull-down `R7` | `kSdCsPin` |
| 4 | `LED_STATUS` | `R1` 220 Ω → `D3` | `kStatusLedPin` |
| 5 | `I2C_SCL` | DS3231 · pull-up `R3` 4.7k | `kRtcSclPin` |
| 14 | `SD_SCK` | microSD CLK | `kSdSckPin` |
| 16 | `OSC_EN` | habilitación de `X1` · pull-down `R11` | `kEthPowerPin` |
| 17 | `SD_MOSI` | microSD CMD | `kSdMosiPin` |
| 18 | `ETH_MDIO` | LAN8720 · pull-up `R20` 1.5k | `kEthMdioPin` |
| 19, 22, 21 | `ETH_TXD0`, `ETH_TXD1`, `ETH_TX_EN` | LAN8720 (RMII, fijos del ESP32) | — |
| 25, 26, 27 | `ETH_RXD0`, `ETH_RXD1`, `ETH_CRS_DV` | LAN8720 (RMII, fijos del ESP32) | — |
| 23 | `ETH_MDC` | LAN8720 | `kEthMdcPin` |
| 33 | `I2C_SDA` | DS3231 · pull-up `R2` 4.7k | `kRtcSdaPin` |
| 35 | `SD_MISO` | microSD DAT0 · pull-up `R4` 10k | `kSdMisoPin` |
| 1, 3 | `UART0_TX`, `UART0_RX` | `J3` | monitor serie |

GPIO12, 13, 15, 32, 34, 36 y 39 quedan sin conectar.

### Pines de arranque (strapping)

| Pin | Estado en el reset | Por qué |
|---|---|---|
| GPIO0 | alto (`R9` 10k) | arranque normal; el oscilador está apagado en el reset, así que el reloj no lo perturba |
| GPIO2 | bajo (`R7` 10k) | necesario para entrar a modo de programación junto con GPIO0 |
| GPIO5 | alto (pull-up de I²C) | valor por defecto |
| GPIO12 | bajo (sin conectar, pull-down interno) | flash a 3.3 V |
| GPIO15 | alto (sin conectar, pull-up interno) | log de arranque activo |

---

## 3. Ethernet

Misma topología que el WT32-ETH01: un oscilador externo alimenta al PHY y al
MAC del ESP32 a la vez, y el firmware lo enciende con `kEthPowerPin`.

- **Reloj:** `X1` → `XTAL1/CLKIN` del LAN8720 y → `R12` (33 Ω) → GPIO0. Por eso
  el modo es `ETH_CLOCK_GPIO0_IN`.
- **Habilitación:** el pin 1 de `X1` enciende la salida en alto y la apaga en
  bajo. `R11` lo mantiene apagado hasta que el firmware sube GPIO16.
- **Dirección del PHY:** 1 (`R19` sube `PHYAD0`), igual que `kEthAddr`.
- **Configuración por resistencias del LAN8720:** los LEDs del RJ45 dejan
  `LED1/REGOFF` en bajo (regulador interno encendido) y `LED2/nINTSEL` en alto
  (reloj de entrada). Los pines de modo quedan en sus pull-ups internos:
  autonegociación completa.
- **Reset del PHY:** `nRST` comparte la red `EN` con el ESP32; se reinician
  juntos.
- **RJ45:** tomas centrales a 3V3, terminaciones de 49.9 Ω (`R15`–`R18`), blindaje
  a GND por `C20` (1 nF, 2 kV) en paralelo con `R23` (1 MΩ).

---

## 4. RTC y microSD

- **RTC:** el DS3231SN es el mismo chip del módulo que se usó en pruebas
  (dirección I²C 0x68), así que `RtcManager` no cambia. El módulo traía además
  una EEPROM que el firmware nunca usó.
- **Pila:** sin la CR2032 el RTC pierde la hora en cada corte y el arranque
  registra `RTC lost power`.
- **microSD:** conexión directa a 3.3 V, sin el regulador ni el adaptador de
  niveles del módulo. `DAT1` y `DAT2` llevan pull-up (`R5`, `R6`). El detector de
  tarjeta del zócalo no se usa.

---

## 5. Alimentación

```
24 V ─ F1 (PTC 0.5 A) ─ D1 (SS34) ─┬─ U1 B2405S-2WR3 ─ 5 V ─ U4 AMS1117 ─ 3V3
                                   └─ D2 (SMAJ28A) a retorno
```

- **Entrada:** el convertidor acepta de **21.6 a 26.4 V**. Una fuente de tablero
  ajustada por encima queda fuera de rango.
- **Aislamiento:** la tierra de entrada (`VIN_GND`) está aislada de la tierra
  lógica (`GND`).
- **Capacidad:** 400 mA a 5 V, sin regular. El consumo estimado es de unos
  210 mA típicos y 375 mA de pico, **con el WiFi apagado**. Encender el radio no
  cabe en este presupuesto.

Bornera `J1`: 1 = 24 V, 2 = retorno de 24 V, 3 = reset externo (a `EN` por `R8`,
1k), 4 = GND lógica.

---

## 6. Programación por serie

No hay convertidor USB en la placa. Se usa un adaptador USB-serie de 3.3 V en el
header `J3`:

| Pin de `J3` | Señal |
|---|---|
| 1 | +5 V |
| 2 | GND |
| 3 | TX del ESP32 (va al RX del adaptador) |
| 4 | RX del ESP32 (va al TX del adaptador) |
| 5 | GPIO0 |
| 6 | EN |

Para entrar a modo de programación: mantener **BOOT** (`SW2`), pulsar y soltar
**RST** (`SW1`), soltar BOOT. Después, `pio run -t upload` como siempre.

El pin 1 de `J3` es la misma red de 5 V que la salida de `U1`: **no alimentes por
`J3` y por la bornera de 24 V a la vez.**

En campo la actualización sigue siendo por OTA.

---

## 7. Qué no hacer

- **No sustituir `U2` por una variante con PSRAM** (ESP32-WROOM-32E-N4R2, N8R2):
  en esas GPIO16 está ocupado dentro del módulo, y aquí habilita el oscilador.
  Sin oscilador no hay Ethernet.
- **No mover el reloj Ethernet a `ETH_CLOCK_GPIO17_OUT`:** GPIO17 es el MOSI de
  la SD y el PHY quedaría con un reloj distinto al del MAC.
- **No encender el WiFi** sin rehacer la fuente (§5).
- **No colgar carga de GPIO0:** lleva el reloj de 50 MHz mientras Ethernet está
  activo.

---

## 8. Qué está verificado y qué no

Verificado sobre el diseño (2026-11-19):

- Netlist extraído de `PCB_RevB` y cotejado pin por pin contra `app_config.hpp`
  y contra el pinout del ESP32-WROOM-32E, LAN8720A, DS3231SN y TF-01A.
- Polaridad de `D1`, `D2`, `D3`, `U1` y `U4` contra los nombres de pin de sus
  símbolos; positivo de `BT1` contra su serigrafía.
- Hoja de datos del oscilador (serie YXC YSO110TR, la que LCSC publica para
  C717684): pinout, habilitación en alto y arranque de 3 ms máximo.
- Hoja de datos del HR911105A: par de transmisión, par de recepción, tomas
  centrales y sentido de los dos LEDs.
- DRC de EasyEDA sin errores; Gerber, BOM y posiciones aceptados por JLCPCB con
  las 33 partidas emparejadas. El ensamblaje requiere **Standard PCBA**: en
  Economic no se colocan el ESP32, el LAN8720A ni el portapilas.

Pendiente, solo comprobable con la placa:

- Calidad del reloj de 50 MHz. La red de GPIO0 sigue hasta el botón BOOT y `J3`,
  que quedan como ramales de la línea de reloj.
- Consumo real y temperatura de `U1` y `U4`.
- Enlace Ethernet estable durante horas y descarga OTA completa.
- Orientación con que JLCPCB coloca las partes polarizadas (se revisó a la vista
  en su visor, no pad por pad).
- Ajuste mecánico en el gabinete.
