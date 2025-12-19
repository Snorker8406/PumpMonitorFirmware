#pragma once

#include <ETH.h>
#include <WiFi.h>
#include "app_config.hpp"

class NetworkManager {
 public:
  static NetworkManager &instance();

  bool begin(uint32_t timeoutMs = 10000);
  bool isConnected() const;
  bool hasLink() const;
  IPAddress localIP() const;
  uint32_t linkSpeedMbps() const;
  const char *macString() const;

 private:
  NetworkManager() = default;
  static void handleEvent(WiFiEvent_t event);

  volatile bool connected_ = false;
  volatile bool linkUp_ = false;
};
