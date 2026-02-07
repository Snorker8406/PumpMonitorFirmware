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
  
  // Firmware update
  void requestFirmwareUpdate(const char* version);
  void processPendingFirmwareUpdate();
  bool hasPendingFirmwareUpdate() const { return firmwareUpdatePending_; }
  
  // Backup upload
  void requestBackupUpload(int year, int month, int day);
  void processPendingBackupUpload();
  bool hasPendingBackupUpload() const { return backupUploadPending_; }

 private:
  MqttManager();
  bool connectInternal();
  void publishStartingMessage(const char* macNoColon);
  void publishPendingBackupResult();
  static void messageCallback(char* topic, byte* payload, unsigned int length);

  WiFiClientSecure secureClient_;
  PubSubClient client_;
  unsigned long lastReconnectAttemptMs_ = 0;
  
  // Firmware update state
  volatile bool firmwareUpdatePending_ = false;
  char pendingFirmwareVersion_[32] = {0};
  
  // Backup upload state
  volatile bool backupUploadPending_ = false;
  int pendingBackupYear_ = 0;
  int pendingBackupMonth_ = 0;
  int pendingBackupDay_ = 0;
};
