#include "mqtt_manager.hpp"

#include <Arduino.h>
#include <cstring>

#include "log.hpp"
#include "network_manager.hpp"
#include "eeprom_manager.hpp"
#include "rtc_manager.hpp"
#include "app_config.hpp"
#include "ota_manager.hpp"

// Forward declarations de funciones de control de Real Time
extern void startRealTimeMode(uint32_t durationSeconds);

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
  client_.setBufferSize(kMqttMaxPacketSize);  // DEBE ser antes de setServer
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
  
  // Procesar topic webService (actualizar)
  if (strstr(topic, "/webService") != nullptr) {
    if (eeprom.setWebServiceURL(msg)) {
      LOGI("MQTT: URL updated via MQTT\n");
    } else {
      LOGE("MQTT: Failed to update URL\n");
    }
  }
  // Procesar topic realTimeIntervalSec (actualizar)
  else if (strstr(topic, "/realTimeIntervalSec") != nullptr) {
    uint16_t interval = atoi(msg);
    if (eeprom.setRealTimeIntervalSec(interval)) {
      LOGI("MQTT: Real Time Interval updated via MQTT\n");
    } else {
      LOGE("MQTT: Failed to update Real Time Interval\n");
    }
  }
  // Procesar topic instantValuesIntervalSec (actualizar)
  else if (strstr(topic, "/instantValuesIntervalSec") != nullptr) {
    uint16_t interval = atoi(msg);
    if (eeprom.setInstantValuesIntervalSec(interval)) {
      LOGI("MQTT: Instant Values Interval updated via MQTT\n");
    } else {
      LOGE("MQTT: Failed to update Instant Values Interval\n");
    }
  }
  // Procesar topic deviceId (actualizar)
  else if (strstr(topic, "/deviceId") != nullptr) {
    int32_t deviceId = atoi(msg);
    if (eeprom.setDeviceID(deviceId)) {
      LOGI("MQTT: Device ID updated via MQTT\n");
    } else {
      LOGE("MQTT: Failed to update Device ID\n");
    }
  }
  // Procesar startRealTime: activar modo Real Time por X segundos
  else if (strstr(topic, "/startRealTime") != nullptr) {
    uint32_t durationSeconds = atoi(msg);
    if (durationSeconds > 0 && durationSeconds <= 3600) {
      startRealTimeMode(durationSeconds);
      LOGI("MQTT: Real Time Mode activated for %lu seconds\n", durationSeconds);
    } else {
      LOGE("MQTT: Invalid Real Time duration (1-3600 seconds required)\n");
    }
  }
  // Procesar installFirmware: marcar para actualizar firmware (se procesa fuera del callback)
  else if (strstr(topic, "/installFirmware") != nullptr) {
    // El payload contiene la versión a instalar
    if (strlen(msg) == 0 || strlen(msg) >= 32) {
      LOGE("MQTT: Invalid firmware version\n");
      return;
    }
    
    LOGI("MQTT: Firmware install requested, version: %s\n", msg);
    MqttManager::instance().requestFirmwareUpdate(msg);
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
    
    if (strcmp(varName, "webService") == 0) {
      value = eeprom.getWebServiceURL();
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/webService", macNoColon);
    }
    else if (strcmp(varName, "firmwareVersion") == 0) {
      value = String(kFirmwareVersion);
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/firmwareVersion", macNoColon);
    }
    else if (strcmp(varName, "realTimeIntervalSec") == 0) {
      value = String(eeprom.getRealTimeIntervalSec());
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/realTimeIntervalSec", macNoColon);
    }
    else if (strcmp(varName, "instantValuesIntervalSec") == 0) {
      value = String(eeprom.getInstantValuesIntervalSec());
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/instantValuesIntervalSec", macNoColon);
    }
    else if (strcmp(varName, "deviceId") == 0) {
      value = String(eeprom.getDeviceID());
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/deviceId", macNoColon);
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
    
    // Obtener valores primero para evitar problemas con temporales
    String webServiceUrl = eeprom.getWebServiceURL();
    uint16_t rtInterval = eeprom.getRealTimeIntervalSec();
    uint16_t ivInterval = eeprom.getInstantValuesIntervalSec();
    int32_t deviceId = eeprom.getDeviceID();
    
    // Construir JSON con todas las variables
    char jsonBuffer[512];
    snprintf(jsonBuffer, sizeof(jsonBuffer),
             "{\"webService\":\"%s\",\"firmwareVersion\":\"%s\",\"realTimeIntervalSec\":%u,\"instantValuesIntervalSec\":%u,\"deviceId\":%d}",
             webServiceUrl.c_str(),
             kFirmwareVersion,
             rtInterval,
             ivInterval,
             deviceId);
    
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
  // Procesar ajuste de hora del RTC: device/{MAC}/adjustDeviceTime
  else if (strstr(topic, "/adjustDeviceTime") != nullptr) {
    // Parsear mensaje: año,mes,dia,hora,minuto,segundo
    int year, month, day, hour, minute, second;
    int parsed = sscanf(msg, "%d,%d,%d,%d,%d,%d", &year, &month, &day, &hour, &minute, &second);
    
    if (parsed != 6) {
      LOGE("MQTT: Invalid time format. Expected: year,month,day,hour,minute,second\n");
      return;
    }
    
    // Validar rangos básicos
    if (year < 2000 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
      LOGE("MQTT: Time values out of range\n");
      return;
    }
    
    // Ajustar el RTC
    auto &rtc = RtcManager::instance();
    if (rtc.setDateTime(year, month, day, hour, minute, second)) {
      LOGI("MQTT: RTC time updated to %04d-%02d-%02d %02d:%02d:%02d\n",
           year, month, day, hour, minute, second);
      
      // Obtener hora actualizada del RTC y publicar confirmación
      const char *macColoned = NetworkManager::instance().macString();
      char macNoColon[13] = {0};
      int idx = 0;
      for (const char *p = macColoned; *p && idx < 12; ++p) {
        if (*p != ':') {
          macNoColon[idx++] = *p;
        }
      }
      
      DateTime now = rtc.now();
      char timeBuffer[32];
      snprintf(timeBuffer, sizeof(timeBuffer), "%04d-%02d-%02d %02d:%02d:%02d",
               now.year(), now.month(), now.day(),
               now.hour(), now.minute(), now.second());
      
      char responseTopic[64];
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/deviceTime", macNoColon);
      
      auto &mqtt = MqttManager::instance();
      if (mqtt.publish(responseTopic, timeBuffer)) {
        LOGI("MQTT: Published device time confirmation\n");
      } else {
        LOGE("MQTT: Failed to publish device time\n");
      }
    } else {
      LOGE("MQTT: Failed to update RTC time\n");
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

void MqttManager::requestFirmwareUpdate(const char* version) {
  strncpy(pendingFirmwareVersion_, version, sizeof(pendingFirmwareVersion_) - 1);
  pendingFirmwareVersion_[sizeof(pendingFirmwareVersion_) - 1] = '\0';
  firmwareUpdatePending_ = true;
  LOGI("MQTT: Firmware update queued for version: %s\n", pendingFirmwareVersion_);
}

void MqttManager::processPendingFirmwareUpdate() {
  if (!firmwareUpdatePending_) {
    return;
  }
  
  firmwareUpdatePending_ = false;
  
  LOGI("MQTT: Processing firmware update for version: %s\n", pendingFirmwareVersion_);
  
  // Desconectar MQTT para liberar memoria
  disconnect();
  LOGI("MQTT: Disconnected to free memory for OTA\n");
  
  // Esperar un poco para que se libere memoria
  delay(500);
  
  // Delegar al OtaManager
  OtaManager::instance().performUpdate(pendingFirmwareVersion_);
}
