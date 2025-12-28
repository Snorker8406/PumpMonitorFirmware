#include "mqtt_manager.hpp"

#include <Arduino.h>
#include <cstring>

#include "log.hpp"
#include "network_manager.hpp"
#include "eeprom_manager.hpp"

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
  client_.setCallback(messageCallback);
}

void MqttManager::messageCallback(char* topic, byte* payload, unsigned int length) {
  // Log del mensaje recibido
  char msg[256];
  unsigned int len = (length < sizeof(msg) - 1) ? length : sizeof(msg) - 1;
  memcpy(msg, payload, len);
  msg[len] = '\0';
  LOGI("MQTT msg [%s]: %s\n", topic, msg);
  
  auto &eeprom = EepromManager::instance();
  
  // Procesar topic masterWebService (actualizar)
  if (strstr(topic, "/masterWebService") != nullptr) {
    if (eeprom.setMasterWebServiceURL(msg)) {
      LOGI("MQTT: Master URL updated via MQTT\n");
    } else {
      LOGE("MQTT: Failed to update Master URL\n");
    }
  }
  // Procesar topic clientWebService (actualizar)
  else if (strstr(topic, "/clientWebService") != nullptr) {
    if (eeprom.setClientWebServiceURL(msg)) {
      LOGI("MQTT: Client URL updated via MQTT\n");
    } else {
      LOGE("MQTT: Failed to update Client URL\n");
    }
  }
  // Procesar topic firmwareVersion (actualizar)
  else if (strstr(topic, "/firmwareVersion") != nullptr) {
    if (eeprom.setFirmwareVersion(msg)) {
      LOGI("MQTT: Firmware Version updated via MQTT\n");
    } else {
      LOGE("MQTT: Failed to update Firmware Version\n");
    }
  }
  // Procesar solicitudes de lectura de variables: device/{MAC}/getValue con variable en payload
  else if (strstr(topic, "/getValue") != nullptr && strstr(topic, "/getValues") == nullptr) {
    // El payload contiene el nombre de la variable
    const char* varName = msg;
    
    // Obtener MAC para construir topic de respuesta
    const char *macColoned = NetworkManager::instance().macString();
    char macNoColon[13] = {0};
    int idx = 0;
    for (const char *p = macColoned; *p && idx < 12; ++p) {
      if (*p != ':') {
        macNoColon[idx++] = *p;
      }
    }
    
    char responseTopic[64];
    String value;
    
    if (strcmp(varName, "masterWebService") == 0) {
      value = eeprom.getMasterWebServiceURL();
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/masterWebService", macNoColon);
    }
    else if (strcmp(varName, "clientWebService") == 0) {
      value = eeprom.getClientWebServiceURL();
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/clientWebService", macNoColon);
    }
    else if (strcmp(varName, "firmwareVersion") == 0) {
      value = eeprom.getFirmwareVersion();
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/firmwareVersion", macNoColon);
    }
    else {
      LOGW("MQTT: Unknown variable requested: %s\n", varName);
      return;
    }
    
    // Publicar el valor
    auto &mqtt = MqttManager::instance();
    if (mqtt.publish(responseTopic, value.c_str())) {
      LOGI("MQTT: Published %s = %s\n", varName, value.c_str());
    } else {
      LOGE("MQTT: Failed to publish %s\n", varName);
    }
  }
  // Procesar solicitud de todas las variables: device/{MAC}/getValues
  else if (strstr(topic, "/getValues") != nullptr) {
    // Obtener MAC para construir topic de respuesta
    const char *macColoned = NetworkManager::instance().macString();
    char macNoColon[13] = {0};
    int idx = 0;
    for (const char *p = macColoned; *p && idx < 12; ++p) {
      if (*p != ':') {
        macNoColon[idx++] = *p;
      }
    }
    
    // Construir JSON con todas las variables
    char jsonBuffer[512];
    snprintf(jsonBuffer, sizeof(jsonBuffer),
             "{\"masterWebService\":\"%s\",\"clientWebService\":\"%s\",\"firmwareVersion\":\"%s\"}",
             eeprom.getMasterWebServiceURL().c_str(),
             eeprom.getClientWebServiceURL().c_str(),
             eeprom.getFirmwareVersion().c_str());
    
    // Publicar todas las variables en un solo mensaje
    char responseTopic[64];
    snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/all", macNoColon);
    
    auto &mqtt = MqttManager::instance();
    if (mqtt.publish(responseTopic, jsonBuffer)) {
      LOGI("MQTT: Published all variables\n");
    } else {
      LOGE("MQTT: Failed to publish all variables\n");
    }
  }
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
    
    // Subscribirse automáticamente a device/{MAC}/# (wildcard para todos los subtopics)
    char deviceTopic[32];
    snprintf(deviceTopic, sizeof(deviceTopic), "device/%s/#", macNoColon);
    if (client_.subscribe(deviceTopic)) {
      LOGI("MQTT subscribed to %s\n", deviceTopic);
    } else {
      LOGE("MQTT subscribe to %s failed\n", deviceTopic);
    }
  } else {
    int state = client_.state();
    LOGE("MQTT connect failed rc=%d (broker=%s:%u, user=%s)\n", 
         state, kMqttBrokerHost, kMqttBrokerPort, username);
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

bool MqttManager::publish(const char* topic, const char* payload) {
  if (!client_.connected()) {
    LOGE("MQTT: Cannot publish, not connected\n");
    return false;
  }
  
  return client_.publish(topic, payload);
}
