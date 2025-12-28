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
  bool publish(const char* topic, const char* payload);

 private:
  MqttManager();
  bool connectInternal();
  static void messageCallback(char* topic, byte* payload, unsigned int length);

  WiFiClientSecure secureClient_;
  PubSubClient client_;
  unsigned long lastReconnectAttemptMs_ = 0;
};
