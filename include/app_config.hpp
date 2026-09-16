#pragma once

#include <Arduino.h>
#include <ETH.h>

#ifndef ENABLE_DEBUG_LOG
#define ENABLE_DEBUG_LOG 1
#endif

constexpr char kDeviceHostname[] = "pump-monitor";

// Firmware Version (fija en código)
constexpr const char* kFirmwareVersion = "DEV.08";

// Firmware OTA Configuration
constexpr const char* kFirmwareBaseUrl = "https://pumpmonitor.agrotecsa.com.mx";
constexpr const char* kFirmwareEndpointPath = "/api/Firmware/InstallFirmware/";
constexpr const char* kBackupUploadEndpointPath = "/api/BackupDeviceFiles/Upload";
constexpr uint32_t kOtaTimeoutMs = 30000;  // Timeout para conexión y respuesta

// API Security
constexpr const char* kApiKey = "a90f091431c4995062e041bdb7cdd3d6c3c26371e678b597a0e65eb7b6cdad4f";  // Cambiar por la API key real

// WT32-ETH01 / LAN8720 wiring
constexpr int kEthAddr = 1;
constexpr int kEthPowerPin = 16;
constexpr int kEthMdcPin = 23;
constexpr int kEthMdioPin = 18;
constexpr eth_phy_type_t kEthPhy = ETH_PHY_LAN8720;
constexpr eth_clock_mode_t kEthClockMode = ETH_CLOCK_GPIO17_OUT;

// Opcional: red estática. Pon kUseStaticIp = true y actualiza los valores.
constexpr bool kUseStaticIp = false;
static const IPAddress kStaticIp(192, 168, 1, 120);
static const IPAddress kStaticGateway(192, 168, 1, 254);
static const IPAddress kStaticSubnet(255, 255, 255, 0);
static const IPAddress kStaticDns(8, 8, 8, 8);

// Task sizing
constexpr uint32_t kNetworkTaskStackWords = 4096;
constexpr UBaseType_t kNetworkTaskPriority = 1;
constexpr BaseType_t kNetworkTaskCore = PRO_CPU_NUM;  // Core 0 para red
constexpr uint32_t kNetworkTaskPeriodMs = 60000;

// MQTT
constexpr const char *kMqttBrokerHost = "mqtt.agrotecsa.com.mx";
constexpr uint16_t kMqttBrokerPort = 8883;
constexpr uint16_t kMqttKeepAliveSec = 60;
constexpr uint32_t kMqttReconnectDelayMs = 3000;
constexpr const char *kMqttUsername = "";
constexpr const char *kMqttPassword = "";
constexpr uint32_t kMqttTaskStackWords = 12288;  // aumentado para buffer MQTT 1024 + TLS
constexpr UBaseType_t kMqttTaskPriority = 2;
constexpr BaseType_t kMqttTaskCore = PRO_CPU_NUM;  // Core 0
constexpr uint32_t kMqttLoopDelayMs = 200;
constexpr uint16_t kMqttMaxPacketSize = 1024;  // Aumentado de 256 default para Real Time mode
constexpr const char *kMqttSystemTopic = "PumpMonitorSystem";

// Modbus TCP
enum class ModbusRegisterType : uint8_t {
  HOLDING_REGISTER = 3,
  INPUT_REGISTER = 4
};

struct ModbusDeviceConfig {
  IPAddress ip;
  uint8_t unitId;           // Modbus Unit ID (slave ID)
  uint16_t startReg;
  uint16_t totalRegs;       // Debe ser par (2 regs por float)
  ModbusRegisterType regType;
  bool swapWords;               // true = Little-Endian words (CD AB), false = Big-Endian words (AB CD)
  uint8_t modbusSlaveId;        // ID numérico del esclavo Modbus
  const char* modbusSlaveName;  // Nombre descriptivo del esclavo Modbus
};

// Límites para el almacenamiento configurable en EEPROM
constexpr size_t kMaxModbusDevices = 8;          // Máximo de dispositivos almacenables en EEPROM
constexpr size_t kModbusSlaveNameLen = 24;       // Longitud máxima del nombre del esclavo (incl. '\0')

// Valores por defecto (semilla). Se escriben en EEPROM la primera vez que arranca
// el dispositivo o cuando la EEPROM está vacía. En tiempo de ejecución la lista
// activa se lee desde EEPROM mediante EepromManager (configurable, no hardcoded).
static const ModbusDeviceConfig kDefaultModbusDevices[] = {
  {IPAddress(192, 168, 1, 200), 1, 0, 100, ModbusRegisterType::INPUT_REGISTER, false, 1, "Device_1"},
  //                ip               unitId  startReg  totalRegs  regType                          swapWords  modbusSlaveId  slaveName
  {IPAddress(192, 168, 1, 101),      1,      0,        8,         ModbusRegisterType::HOLDING_REGISTER, true,  2,       "Device_2"},
};
constexpr size_t kDefaultModbusDeviceCount = sizeof(kDefaultModbusDevices) / sizeof(kDefaultModbusDevices[0]);

// ── Lista hardcoded original (DESACTIVADA: ahora la lista se lee desde EEPROM) ──
// static const ModbusDeviceConfig kModbusDevices[] = {
//   {IPAddress(192, 168, 1, 200), 1, 0, 100, ModbusRegisterType::INPUT_REGISTER, false, 1, "Device_1"},
//   //                ip               unitId  startReg  totalRegs  regType                          swapWords  modbusSlaveId  slaveName
//   {IPAddress(192, 168, 1, 101),      1,      0,        8,         ModbusRegisterType::HOLDING_REGISTER, true,  2,       "Device_2"},
//   // Ejemplo con más registros: {IPAddress(192, 168, 1, 12), 1, 0, 120, ModbusRegisterType::HOLDING_REGISTER, 3, "Device_3"},
// };
// constexpr size_t kModbusDeviceCount = sizeof(kModbusDevices) / sizeof(kModbusDevices[0]);

constexpr uint16_t kModbusChunkSize = 20;  // (legacy - no longer used with syncRequest)
constexpr uint32_t kModbusChunkDelayMs = 600;  // (legacy - no longer used with syncRequest)
constexpr uint32_t kModbusTimeoutMs = 8000;  // Timeout de respuesta por syncRequest (ms)
constexpr uint32_t kModbusIntervalMs = 100;  // Pausa mínima entre requests al mismo host (ms)
constexpr uint32_t kModbusReadPeriodMs = 10000;
constexpr uint32_t kModbusInterDeviceDelayMs = 500;  // Delay entre dispositivos
constexpr uint32_t kModbusTaskStackWords = 10240;  // Stack para manejar hasta 120 registros por dispositivo
constexpr UBaseType_t kModbusTaskPriority = 1;
constexpr BaseType_t kModbusTaskCore = APP_CPU_NUM;  // Core 1 para lógica

// ── Actuadores (control de Coils con confirmaciones) ──
constexpr size_t kActuatorCoilCount = 4;               // Número de coils controlables
constexpr uint8_t kActuatorConfirmCount = 3;           // Confirmaciones (booleanos) por coil
// Activar/desactivar el mecanismo de confirmaciones por cada coil (un valor por coil)
static constexpr bool kActuatorConfirmationsEnabled[kActuatorCoilCount] = {true, true, true, true};
constexpr size_t kActuatorModbusDeviceIndex = 1;       // Índice del dispositivo Modbus destino para los coils
// Índice en el array de alarmas que actúa como confirmación de estado para cada coil
static constexpr uint8_t kActuatorConfirmAlarmIndex[kActuatorCoilCount] = {0, 1, 2, 3};
// Valores de confirmación (enteros 0-99, únicos por coil) que llegan escritos en
// reg[0] del Modbus Server. Al recibir un valor que coincida con alguno, se
// fuerzan los switches de esa coil a 111 (ON) o 000 (OFF) y se publica el estado.
static constexpr uint8_t kActuatorConfirmManualOn[kActuatorCoilCount]  = {11, 12, 13, 14};
static constexpr uint8_t kActuatorConfirmManualOff[kActuatorCoilCount] = {21, 22, 23, 24};
static constexpr uint8_t kActuatorConfirmRemoteOn[kActuatorCoilCount]  = {31, 32, 33, 34};
static constexpr uint8_t kActuatorConfirmRemoteOff[kActuatorCoilCount] = {41, 42, 43, 44};
// Secuencia de ARRANQUE (llegando a 111): dirección y valor a escribir por coil
static constexpr uint16_t kActuatorCoilOnAddresses[kActuatorCoilCount] = {0, 1, 2, 3};
static constexpr bool     kActuatorCoilOnValues[kActuatorCoilCount]    = {true, true, true, true};
// Secuencia de PARO (llegando a 000): dirección y valor a escribir por coil
static constexpr uint16_t kActuatorCoilOffAddresses[kActuatorCoilCount] = {0, 1, 2, 3};
static constexpr bool     kActuatorCoilOffValues[kActuatorCoilCount]    = {false, false, false, false};
constexpr uint32_t kActuatorTaskStackWords = 4096;
constexpr UBaseType_t kActuatorTaskPriority = 1;
constexpr BaseType_t kActuatorTaskCore = APP_CPU_NUM;  // Core 1
constexpr uint32_t kActuatorTaskPeriodMs = 100;        // Periodo de evaluación de escrituras pendientes

// ── Alarmas (lectura de Coils/booleanos por Modbus) ──
// Lee un rango de coils (FC01) de un dispositivo Modbus y publica el estado por
// MQTT, igual que la task de Instant Values. Todos los valores son configurables
// vía EEPROM (semilla inicial con estos defaults).
// Notación Modbus: 000001..000006 equivale a startAddress=0, count=6.
constexpr size_t      kAlarmModbusDeviceIndex = 1;   // Dispositivo Modbus destino (índice en la lista; default 2)
constexpr uint16_t    kAlarmStartAddress      = 0;   // Dirección inicial (0 = coil 000001)
constexpr uint16_t    kAlarmCount             = 6;   // Número de coils a leer (000001..000006)
constexpr bool        kAlarmDiscreteInputs    = true; // true = FC02 Discrete Inputs; false = FC01 Coils
// Tipos de cada coil (1 carácter por coil, en el mismo orden que se leen):
//   A = Alarm, N = Notification, C = Confirmation (mayúsculas o minúsculas).
// Ej: "CNAA" = 1 confirmación, 1 notificación, 2 alarmas. Tamaño fijo en EEPROM.
constexpr const char* kAlarmCoilsTypes        = "AAAAAA";  // default: 6 alarmas
constexpr size_t      kAlarmCoilsTypesMaxLen  = 40;   // longitud máxima almacenable
constexpr uint32_t    kAlarmTaskStackWords    = 4096;
constexpr UBaseType_t kAlarmTaskPriority      = 1;
constexpr BaseType_t  kAlarmTaskCore          = APP_CPU_NUM;  // Core 1

// ── Modbus Server (esclavo TCP: dispositivos externos escriben en este device) ──
constexpr uint8_t kModbusServerUnitId = 2;             // Unit/Server ID que atiende este device
constexpr uint16_t kModbusServerPort = 502;            // Puerto TCP de escucha (estándar Modbus = 502)
constexpr uint8_t kModbusServerMaxClients = 4;         // Conexiones simultáneas permitidas
constexpr uint32_t kModbusServerTimeoutMs = 10000;     // Timeout de inactividad por cliente (ms)
constexpr uint16_t kModbusServerRegCount = 32;         // Holding registers expuestos (direcciones 0..N-1)
constexpr uint32_t kModbusServerTaskStackWords = 12288;  // process() publica por MQTT (TLS) y escribe en SD: necesita stack grande
constexpr UBaseType_t kModbusServerTaskPriority = 1;
constexpr BaseType_t kModbusServerTaskCore = APP_CPU_NUM;  // Core 1
constexpr uint32_t kModbusServerTaskPeriodMs = 50;     // Periodo de impresión de escrituras recibidas

// SD Card SPI
constexpr int kSdCsPin = 2;
constexpr int kSdMosiPin = 17;
constexpr int kSdSckPin = 14;
constexpr int kSdMisoPin = 35;
constexpr uint32_t kSdSpiFrequency = 4000000;  // 4 MHz
constexpr const char* kSdDataPath = "/data";
constexpr const char* kSdErrorPath = "/error";
constexpr uint32_t kSdTaskStackWords = 8192;
constexpr UBaseType_t kSdTaskPriority = 1;
constexpr BaseType_t kSdTaskCore = APP_CPU_NUM;  // Core 1
constexpr uint32_t kSdWritePeriodMs = 10000;  // Guardar cada 10 segundos

// RTC DS3231 I2C
constexpr int kRtcSdaPin = 33;
constexpr int kRtcSclPin = 5;

// Watchdog de tareas.
//
// El valor por defecto de Arduino son 5 s, y no alcanzan: el handshake TLS con
// el broker verifica firmas ECDSA P-384 por software (el ESP32 no tiene
// acelerador de curva elíptica) dentro de una sola llamada a
// mbedtls_ssl_handshake(), que no cede el procesador. Si esa verificación tarda
// más que el watchdog, IDLE0 se queda sin tiempo y el equipo se reinicia en
// bucle sin llegar nunca a conectar.
//
// Con el intermedio como ancla (ver kMqttCaCert) el caso normal es una sola
// verificación y va sobrado; este margen cubre el camino lento de respaldo.
constexpr uint32_t kTaskWdtTimeoutSec = 30;

// Reloj del sistema por SNTP (solo para TLS).
//
// mbedTLS únicamente valida las fechas del certificado del broker si el sistema
// tiene la hora puesta; al arrancar, el ESP32 está en 1970 y las omite, o sea
// aceptaría un certificado vencido o suplantado. SNTP da UTC real, que es justo
// lo que necesita la validación.
//
// **No se usa el DS3231 a propósito.** El RTC guarda hora *local sin zona* (la
// SPA envía `adjustDeviceTime` con la hora del navegador tal cual), así que
// tomarlo como UTC dejaría el reloj corrido varias horas y el equipo podría
// rechazar un certificado recién emitido por verlo "en el futuro". El DS3231
// sigue siendo la fuente de los timestamps de la aplicación; ver
// docs/RTC_USAGE.md.
//
// Interruptor de emergencia: ponlo en false y recompila para volver a validar
// solo la cadena, sin fechas.
constexpr bool kEnableNtpTimeSync = true;
constexpr const char *kNtpServer1 = "pool.ntp.org";
constexpr const char *kNtpServer2 = "time.google.com";
// Cuánto se espera en setup() a que SNTP conteste antes de seguir sin hora.
// Si no llega, el cliente SNTP sigue reintentando solo en segundo plano.
constexpr uint32_t kNtpSyncTimeoutMs = 10000;
// Umbral para considerar que el reloj ya está en hora (~2025-06-15).
// Sirve para distinguir "hora real" del 1970 con el que arranca el ESP32.
constexpr time_t kMinValidEpoch = 1750000000;

// Anclas de confianza TLS del broker MQTT.
//
// Orden deliberado: primero el intermedio que firma la hoja, luego las raíces.
//
// El coste importa más que la elegancia. El ESP32 no tiene acelerador de curva
// elíptica y la cadena del broker es toda ECDSA P-384:
//   hoja -> YE1 -> Root YE -> ISRG Root X2 -> ISRG Root X1
// Confiando solo en las raíces hay que verificar 3 firmas P-384, lo que toma
// más de 10 s de CPU seguidos dentro de mbedtls_ssl_handshake(). Como esa
// función no cede el procesador, IDLE0 se queda sin tiempo y el watchdog de
// tareas reinicia el equipo (así se perdió el arranque con DEV.04..DEV.06).
//
// Con YE1 como ancla solo hay UNA verificación: es el mismo coste que tenía el
// firmware original con E7, que funcionó durante meses.
//
// Las raíces se dejan como respaldo por si Let's Encrypt rota el intermedio.
// Ese camino es el lento, por eso además se amplía el watchdog en setup().
// Vigencias: YE1 hasta 2028, ISRG Root X1 hasta 2035, X2 hasta 2040.
// Contexto completo en docs/RENOVACION_TLS.md.
static const char kMqttCaCert[] PROGMEM = R"PEM(
-----BEGIN CERTIFICATE-----
MIICizCCAhGgAwIBAgIQXd1w3TH4AchcGGp6BLgK/jAKBggqhkjOPQQDAzAuMQsw
CQYDVQQGEwJVUzENMAsGA1UEChMESVNSRzEQMA4GA1UEAxMHUm9vdCBZRTAeFw0y
NTA5MDMwMDAwMDBaFw0yODA5MDIyMzU5NTlaMDMxCzAJBgNVBAYTAlVTMRYwFAYD
VQQKEw1MZXQncyBFbmNyeXB0MQwwCgYDVQQDEwNZRTEwdjAQBgcqhkjOPQIBBgUr
gQQAIgNiAAQHZVB1/mimla2hfSurylScjPMZaOJXLz/NnAc2sylm8WDyhU9Ccp+z
ASQi5vSwGGJjSGklkD9fdPR8GpyDIOIjCEfrnbt/v+ZSEPLLEGbaM6EccDbN7p9x
teIm2Avf+ryjge4wgeswDgYDVR0PAQH/BAQDAgGGMBMGA1UdJQQMMAoGCCsGAQUF
BwMBMBIGA1UdEwEB/wQIMAYBAf8CAQAwHQYDVR0OBBYEFLsgykcL/tflnPmPCSqj
jDdFsbzYMB8GA1UdIwQYMBaAFKPIJlqOoUzQNWP8myPIOq5W809WMDIGCCsGAQUF
BwEBBCYwJDAiBggrBgEFBQcwAoYWaHR0cDovL3llLmkubGVuY3Iub3JnLzATBgNV
HSAEDDAKMAgGBmeBDAECATAnBgNVHR8EIDAeMBygGqAYhhZodHRwOi8veWUuYy5s
ZW5jci5vcmcvMAoGCCqGSM49BAMDA2gAMGUCMQDgjUEahFT/h3DRakqiPZpLvPgf
Zwkt6K2EOMmh1nvEzl83eMLYcod4GCl3b0J1Nn0CMBNYmEQJb4CEG5WoOe7aRn/L
VKu6saHmHEynI7ysIPd8zQsK1HdmhlHKlw9Z5GpGvA==
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw
CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg
R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00
MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT
ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw
EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW
+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9
ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T
AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI
zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW
tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1
/q4AaOeMSQ+2b1tbFfLn
-----END CERTIFICATE-----
)PEM";
