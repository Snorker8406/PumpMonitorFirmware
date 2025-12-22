#pragma once

#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include "app_config.hpp"

class MqttManager {
 public:
  static MqttManager &instance();

  void begin();
  void loop();
  void ensureConnected();
  void disconnect();
  bool isConnected();

 private:
  MqttManager();
  bool connectInternal();

  WiFiClientSecure secureClient_;
  PubSubClient client_;
  unsigned long lastReconnectAttemptMs_ = 0;
};
