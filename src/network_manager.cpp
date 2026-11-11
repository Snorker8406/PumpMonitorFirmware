#include "network_manager.hpp"

#include <esp_attr.h>
#include <esp_system.h>
#include <time.h>

#include "eeprom_manager.hpp"
#include "log.hpp"

namespace {
NetworkManager *g_instance = nullptr;

// Estado del respaldo DHCP que sobrevive a ESP.restart() (memoria RTC sin
// inicializar). En un arranque en frío su contenido es basura: se valida con
// la firma y además se descarta si el reset fue por encendido.
enum class StaticFallbackStage : uint8_t {
  Armed = 0,         // IP fija normal; si no llega al broker, probar DHCP
  BootDhcp = 1,      // el próximo arranque (o el actual) usa DHCP de respaldo
  Exhausted = 2,     // el DHCP de respaldo tampoco sirvió: IP fija sin más reintentos
};
constexpr uint32_t kFallbackMagic = 0x4E455446;  // "NETF"

struct FallbackState {
  uint32_t magic;
  StaticFallbackStage stage;
};
RTC_NOINIT_ATTR FallbackState s_fallback;

StaticFallbackStage loadFallbackStage() {
  if (esp_reset_reason() == ESP_RST_POWERON || s_fallback.magic != kFallbackMagic ||
      static_cast<uint8_t>(s_fallback.stage) > static_cast<uint8_t>(StaticFallbackStage::Exhausted)) {
    s_fallback.magic = kFallbackMagic;
    s_fallback.stage = StaticFallbackStage::Armed;
  }
  return s_fallback.stage;
}

void storeFallbackStage(StaticFallbackStage stage) {
  s_fallback.magic = kFallbackMagic;
  s_fallback.stage = stage;
}

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
  const StaticFallbackStage stage = loadFallbackStage();
  staticConfigured_ = !eeprom.getNetworkUseDhcp();

  if (!staticConfigured_) {
    storeFallbackStage(StaticFallbackStage::Armed);
  } else if (stage == StaticFallbackStage::BootDhcp) {
    // La IP fija no alcanzó el broker en el arranque anterior. La EEPROM no se
    // toca: el siguiente reinicio vuelve a intentar la IP fija.
    dhcpFallbackActive_ = true;
  }

  if (dhcpFallbackActive_) {
    LOGW("ETH: IP fija %s sin broker en el arranque anterior; usando DHCP de respaldo\n",
         eeprom.getNetworkStaticIp().toString().c_str());
  } else if (staticConfigured_) {
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

void NetworkManager::resetStaticFallback() {
  storeFallbackStage(StaticFallbackStage::Armed);
}

void NetworkManager::superviseStaticIp(bool brokerConnected) {
  const TickType_t now = xTaskGetTickCount();
  const TickType_t delta = (lastSuperviseTick_ == 0) ? 0 : (now - lastSuperviseTick_);
  lastSuperviseTick_ = now;

  if (!staticConfigured_ || brokerReached_) {
    return;
  }

  if (brokerConnected) {
    brokerReached_ = true;
    // Con IP fija: rearmar para futuros reinicios. Con el DHCP de respaldo
    // también: el próximo reinicio vuelve a probar la IP fija desde cero.
    storeFallbackStage(StaticFallbackStage::Armed);
    if (dhcpFallbackActive_) {
      LOGW("ETH: broker alcanzado con DHCP de respaldo (IP %s). Corrige la IP fija por saveNetworkConfig\n",
           localIP().toString().c_str());
    }
    return;
  }

  // Sin enlace no hay nada que diagnosticar (cable, switch apagado): no cuenta.
  if (!linkUp_) {
    return;
  }
  linkUpTicks_ += delta;
  if (linkUpTicks_ < pdMS_TO_TICKS(kNetStaticFallbackTimeoutMs)) {
    return;
  }

  if (dhcpFallbackActive_) {
    LOGE("ETH: DHCP de respaldo tampoco alcanzó el broker; volviendo a IP fija sin más reintentos\n");
    storeFallbackStage(StaticFallbackStage::Exhausted);
  } else if (s_fallback.stage == StaticFallbackStage::Armed) {
    LOGE("ETH: IP fija sin broker tras %u s de enlace; reiniciando en DHCP de respaldo\n",
         (unsigned)(kNetStaticFallbackTimeoutMs / 1000));
    storeFallbackStage(StaticFallbackStage::BootDhcp);
  } else {
    // Exhausted: ya se probó el respaldo en este arranque en frío.
    brokerReached_ = true;  // dejar de supervisar
    return;
  }

  // Margen para que el LOGE llegue a la SD antes del reinicio.
  vTaskDelay(pdMS_TO_TICKS(kMqttPreRestartDelayMs));
  ESP.restart();
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
