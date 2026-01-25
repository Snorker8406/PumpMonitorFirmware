#pragma once

#include <Arduino.h>
#include <ETH.h>

#ifndef ENABLE_DEBUG_LOG
#define ENABLE_DEBUG_LOG 1
#endif

constexpr char kDeviceHostname[] = "pump-monitor";

// Firmware Version (fija en código)
constexpr const char* kFirmwareVersion = "DEV.00";

// Firmware OTA Configuration
constexpr const char* kFirmwareBaseUrl = "https://pumpmonitor.agrotecsa.com.mx";
constexpr const char* kFirmwareEndpointPath = "/api/Firmware/InstallFirmware/";
constexpr uint32_t kOtaTimeoutMs = 30000;  // Timeout para conexión y respuesta

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

// Modbus TCP
enum class ModbusRegisterType : uint8_t {
  HOLDING_REGISTER = 0,
  INPUT_REGISTER = 1
};

struct ModbusDeviceConfig {
  IPAddress ip;
  uint8_t unitId;           // Modbus Unit ID (slave ID)
  uint16_t startReg;
  uint16_t totalRegs;       // Debe ser par (2 regs por float)
  ModbusRegisterType regType;
  const char* modbusModelId;  // Identificador del modelo Modbus
};

// Lista de dispositivos Modbus a leer (hasta 120 registros por dispositivo)
static const ModbusDeviceConfig kModbusDevices[] = {
  {IPAddress(192, 168, 1, 11), 1, 0, 100, ModbusRegisterType::INPUT_REGISTER, "Device_1"},
  {IPAddress(192, 168, 1, 10), 1, 0, 20, ModbusRegisterType::HOLDING_REGISTER, "Device_2"},
  // Ejemplo con más registros: {IPAddress(192, 168, 1, 12), 1, 0, 120, ModbusRegisterType::HOLDING_REGISTER, "Device_3"},
};
constexpr size_t kModbusDeviceCount = sizeof(kModbusDevices) / sizeof(kModbusDevices[0]);

constexpr uint16_t kModbusChunkSize = 60;  // Leer hasta 60 registros por chunk (max 120 regs totales)
constexpr uint32_t kModbusChunkDelayMs = 300;  // Delay aumentado para chunks más grandes
constexpr uint32_t kModbusReadPeriodMs = 10000;
constexpr uint32_t kModbusInterDeviceDelayMs = 500;  // Delay entre dispositivos
constexpr uint32_t kModbusTaskStackWords = 10240;  // Stack para manejar hasta 120 registros por dispositivo
constexpr UBaseType_t kModbusTaskPriority = 1;
constexpr BaseType_t kModbusTaskCore = APP_CPU_NUM;  // Core 1 para lógica

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

// Certificado raíz/servidor para TLS MQTT
static const char kMqttCaCert[] PROGMEM = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDnzCCAyagAwIBAgISBoeKYFASZUFjdFjLN3UjDeFDMAoGCCqGSM49BAMDMDIx
CzAJBgNVBAYTAlVTMRYwFAYDVQQKEw1MZXQncyBFbmNyeXB0MQswCQYDVQQDEwJF
NzAeFw0yNTEyMTcyMDAwNDVaFw0yNjAzMTcyMDAwNDRaMCAxHjAcBgNVBAMTFW1x
dHQuYWdyb3RlY3NhLmNvbS5teDBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABI3A
hfpXmGE3YbA1MZgYVnGN2sqSoHzVBLEWrXXzn2vX2WpFITTwKtwl49jhSW+QUh0p
dFxkHTyhf4lpaG9ldIWjggIsMIICKDAOBgNVHQ8BAf8EBAMCB4AwHQYDVR0lBBYw
FAYIKwYBBQUHAwEGCCsGAQUFBwMCMAwGA1UdEwEB/wQCMAAwHQYDVR0OBBYEFGN5
Mz/s2HoEDYG46VGL7rVkuxp9MB8GA1UdIwQYMBaAFK5IntyHHUSgb9qi5WB0BHjC
nACAMDIGCCsGAQUFBwEBBCYwJDAiBggrBgEFBQcwAoYWaHR0cDovL2U3LmkubGVu
Y3Iub3JnLzAgBgNVHREEGTAXghVtcXR0LmFncm90ZWNzYS5jb20ubXgwEwYDVR0g
BAwwCjAIBgZngQwBAgEwLQYDVR0fBCYwJDAioCCgHoYcaHR0cDovL2U3LmMubGVu
Y3Iub3JnLzY4LmNybDCCAQ0GCisGAQQB1nkCBAIEgf4EgfsA+QB/ABqLnWlKV5jI
maDKiL30j8C0VmDMw2ANH3H0af/H0ayjAAABmy4cKIMACAAABQAmHRU+BAMASDBG
AiEAsUUWNkt3MleqnKpJUWbur5XXTuks2t7yZ51xFtmQd0ECIQDGTqGhPremdWci
zJa8Yr0bF/U8r87Do8jYExjCtjqTPgB2ANFuqaVoB35mNaA/N6XdvAOlPEESFNSI
GPXpMbMjy5UEAAABmy4cMIsAAAQDAEcwRQIgXgHXRoSQ9wZSe74dF893CWz/9iiW
bjM67QDTmR68lWYCIQDY7k1dYJ1DcPXslalfyf7T9wRZ0Wq4esRpex8Qt0zU+zAK
BggqhkjOPQQDAwNnADBkAjAC2ceA7XdEgvKPvTrNO8AdI8l4xw8vHCgLoG8AU7xI
CjvrVUBXLimI2OpuNNwwHBMCMEh4boZXoO/D8tq2s8Qk+n7xAGaX0aHHTKEzWfdj
a2XmU0b8SfNDuapegWbSGY3u4A==
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIEVzCCAj+gAwIBAgIRAKp18eYrjwoiCWbTi7/UuqEwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMjQwMzEzMDAwMDAw
WhcNMjcwMzEyMjM1OTU5WjAyMQswCQYDVQQGEwJVUzEWMBQGA1UEChMNTGV0J3Mg
RW5jcnlwdDELMAkGA1UEAxMCRTcwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAARB6AST
CFh/vjcwDMCgQer+VtqEkz7JANurZxLP+U9TCeioL6sp5Z8VRvRbYk4P1INBmbef
QHJFHCxcSjKmwtvGBWpl/9ra8HW0QDsUaJW2qOJqceJ0ZVFT3hbUHifBM/2jgfgw
gfUwDgYDVR0PAQH/BAQDAgGGMB0GA1UdJQQWMBQGCCsGAQUFBwMCBggrBgEFBQcD
ATASBgNVHRMBAf8ECDAGAQH/AgEAMB0GA1UdDgQWBBSuSJ7chx1EoG/aouVgdAR4
wpwAgDAfBgNVHSMEGDAWgBR5tFnme7bl5AFzgAiIyBpY9umbbjAyBggrBgEFBQcB
AQQmMCQwIgYIKwYBBQUHMAKGFmh0dHA6Ly94MS5pLmxlbmNyLm9yZy8wEwYDVR0g
BAwwCjAIBgZngQwBAgEwJwYDVR0fBCAwHjAcoBqgGIYWaHR0cDovL3gxLmMubGVu
Y3Iub3JnLzANBgkqhkiG9w0BAQsFAAOCAgEAjx66fDdLk5ywFn3CzA1w1qfylHUD
aEf0QZpXcJseddJGSfbUUOvbNR9N/QQ16K1lXl4VFyhmGXDT5Kdfcr0RvIIVrNxF
h4lqHtRRCP6RBRstqbZ2zURgqakn/Xip0iaQL0IdfHBZr396FgknniRYFckKORPG
yM3QKnd66gtMst8I5nkRQlAg/Jb+Gc3egIvuGKWboE1G89NTsN9LTDD3PLj0dUMr
OIuqVjLB8pEC6yk9enrlrqjXQgkLEYhXzq7dLafv5Vkig6Gl0nuuqjqfp0Q1bi1o
yVNAlXe6aUXw92CcghC9bNsKEO1+M52YY5+ofIXlS/SEQbvVYYBLZ5yeiglV6t3S
M6H+vTG0aP9YHzLn/KVOHzGQfXDP7qM5tkf+7diZe7o2fw6O7IvN6fsQXEQQj8TJ
UXJxv2/uJhcuy/tSDgXwHM8Uk34WNbRT7zGTGkQRX0gsbjAea/jYAoWv0ZvQRwpq
Pe79D/i7Cep8qWnA+7AE/3B3S/3dEEYmc0lpe1366A/6GEgk3ktr9PEoQrLChs6I
tu3wnNLB2euC8IKGLQFpGtOO/2/hiAKjyajaBP25w1jF0Wl8Bbqne3uZ2q1GyPFJ
YRmT7/OXpmOH/FVLtwS+8ng1cAmpCujPwteJZNcDG0sF2n/sc0+SQf49fdyUK0ty
+VUwFj9tmWxyR/M=
-----END CERTIFICATE-----
)PEM";
