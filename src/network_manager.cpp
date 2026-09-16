#include "network_manager.hpp"

#include <time.h>

#include "eeprom_manager.hpp"
#include "log.hpp"

namespace {
NetworkManager *g_instance = nullptr;

const char *formatMac() {
  static char macStr[18];
  uint8_t mac[6] = {};
  ETH.macAddress(mac);
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return macStr;
}
}

NetworkManager &NetworkManager::instance() {
  static NetworkManager inst;
  g_instance = &inst;
  return inst;
}

bool NetworkManager::begin(uint32_t timeoutMs) {
  WiFi.onEvent(NetworkManager::handleEvent);

  if (!ETH.begin(kEthAddr, kEthPowerPin, kEthMdcPin, kEthMdioPin, kEthPhy, kEthClockMode)) {
    LOGE("ETH.begin() failed - check hardware configuration\n");
    return false;
  }

  // Configuración de red desde EEPROM: DHCP por defecto (EEPROM vacía),
  // IP fija solo si el flag y la IP están guardados.
  // ETH.config() debe llamarse DESPUÉS de ETH.begin(): antes la interfaz de
  // red no existe y el stop del cliente DHCP falla ("dhcp client stop called
  // with NULL api"), dejando activo el DHCP.
  auto &eeprom = EepromManager::instance();
  if (!eeprom.getNetworkUseDhcp()) {
    IPAddress ip = eeprom.getNetworkStaticIp();
    IPAddress gateway = eeprom.getNetworkGateway();
    IPAddress subnet = eeprom.getNetworkSubnet();
    IPAddress dns = eeprom.getNetworkDns();
    if (ETH.config(ip, gateway, subnet, dns)) {
      LOGI("ETH static IP config: %s gw=%s sn=%s dns=%s\n",
           ip.toString().c_str(), gateway.toString().c_str(),
           subnet.toString().c_str(), dns.toString().c_str());
    } else {
      LOGE("ETH static IP config failed, falling back to DHCP\n");
    }
  } else {
    LOGI("ETH using DHCP\n");
  }

  const uint32_t start = millis();
  while (!connected_ && (millis() - start) < timeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  
  if (!connected_) {
    LOGE("ETH connection timeout after %lu ms\n", timeoutMs);
  }

  if (connected_) {
    ETH.setHostname(kDeviceHostname);
  }

  return connected_;
}

bool NetworkManager::isConnected() const {
  return connected_;
}

bool NetworkManager::hasLink() const {
  return linkUp_;
}

IPAddress NetworkManager::localIP() const {
  return ETH.localIP();
}

uint32_t NetworkManager::linkSpeedMbps() const {
  return ETH.linkSpeed();
}

const char *NetworkManager::macString() const {
  return formatMac();
}

void NetworkManager::handleEvent(WiFiEvent_t event) {
  if (g_instance == nullptr) {
    return;
  }

  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      LOGD("ETH start\n");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      g_instance->linkUp_ = true;
      LOGI("ETH link up\n");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      g_instance->connected_ = true;
      g_instance->linkUp_ = true;
      LOGI("ETH got IP: %s | MAC: %s\n", ETH.localIP().toString().c_str(), formatMac());
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP:
      g_instance->connected_ = false;
      g_instance->linkUp_ = false;
      LOGW("ETH lost link\n");
      break;
    default:
      break;
  }
}

bool NetworkManager::systemClockIsValid() {
  return time(nullptr) > kMinValidEpoch;
}

void NetworkManager::beginTimeSync() {
  if (timeSyncStarted_) {
    return;
  }
  timeSyncStarted_ = true;

  // Offset 0 y sin horario de verano: el reloj queda en UTC, que es contra lo
  // que mbedTLS compara notBefore/notAfter del certificado del broker.
  //
  // IMPORTANTE: configTime() llama por dentro a sntp_stop()/sntp_init() de lwIP,
  // que NO son thread-safe. Debe invocarse una sola vez, desde setup(), antes de
  // crear las tareas; hacerlo desde una tarea con la red ya en uso corrompe el
  // estado de lwIP y cuelga el handshake TLS.
  configTime(0, 0, kNtpServer1, kNtpServer2);
  LOGI("NTP: cliente iniciado (%s, %s)\n", kNtpServer1, kNtpServer2);
}

bool NetworkManager::waitForSystemClock(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (systemClockIsValid()) {
      const time_t nowUtc = time(nullptr);
      struct tm utc = {};
      gmtime_r(&nowUtc, &utc);
      LOGI("NTP: reloj en hora, %04d-%02d-%02d %02d:%02d:%02d UTC\n",
           utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
           utc.tm_hour, utc.tm_min, utc.tm_sec);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }

  // Se continua sin hora: mbedTLS validara la cadena pero no las fechas, que es
  // el comportamiento anterior a DEV.05. Es preferible a dejar el equipo
  // incomunicado por no alcanzar un servidor NTP. El cliente SNTP sigue
  // reintentando por su cuenta, asi que puede ponerse en hora mas adelante.
  LOGW("NTP: sin hora tras %u ms; el certificado se validara sin fechas\n",
       (unsigned)timeoutMs);
  return false;
}
