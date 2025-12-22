#include "mqtt_manager.hpp"

#include <Arduino.h>
#include <cstring>

#include "log.hpp"
#include "network_manager.hpp"

namespace {
constexpr char kMqttClientIdPrefix[] = "pump-monitor";
}

MqttManager &MqttManager::instance() {
  static MqttManager inst;
  return inst;
}

MqttManager::MqttManager() : client_(secureClient_) {}

void MqttManager::begin() {
  secureClient_.setCACert(kMqttCaCert);
  client_.setServer(kMqttBrokerHost, kMqttBrokerPort);
  client_.setKeepAlive(kMqttKeepAliveSec);
}

bool MqttManager::connectInternal() {
  if (!NetworkManager::instance().isConnected()) {
    return false;
  }

  // Build username as PM_ + MAC without colons, e.g., PM_AABBCCDDEEFF
  const char *macColoned = NetworkManager::instance().macString();
  char macNoColon[13] = {0};
  int idx = 0;
  for (const char *p = macColoned; *p && idx < 12; ++p) {
    if (*p != ':') {
      macNoColon[idx++] = *p;
    }
  }

  char username[20];
  snprintf(username, sizeof(username), "PM_%s", macNoColon);

  // Build a client ID with a suffix to avoid collisions
  char clientId[64];
  snprintf(clientId, sizeof(clientId), "%s-%lu", kMqttClientIdPrefix, millis());

  LOGI("MQTT connecting to %s:%u...\n", kMqttBrokerHost, kMqttBrokerPort);
  const bool ok = client_.connect(clientId, username, kMqttPassword);
  if (ok) {
    LOGI("MQTT connected\n");
  } else {
    LOGW("MQTT connect failed rc=%d\n", client_.state());
  }
  return ok;
}

void MqttManager::ensureConnected() {
  const unsigned long now = millis();
  if (client_.connected()) {
    return;
  }

  if (now - lastReconnectAttemptMs_ < kMqttReconnectDelayMs) {
    return;
  }

  lastReconnectAttemptMs_ = now;
  connectInternal();
}

void MqttManager::loop() {
  if (client_.connected()) {
    client_.loop();
  }
}

void MqttManager::disconnect() {
  if (client_.connected()) {
    client_.disconnect();
    LOGW("MQTT disconnected\n");
  }
}

bool MqttManager::isConnected() {
  return client_.connected();
}
