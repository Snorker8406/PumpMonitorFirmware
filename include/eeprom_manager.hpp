#pragma once

#include <Arduino.h>
#include <Preferences.h>

class EepromManager {
 public:
  static EepromManager &instance();

  void begin();
  
  // Master Web Service URL
  String getMasterWebServiceURL();
  bool setMasterWebServiceURL(const char* url);
  
  // Client Web Service URL
  String getClientWebServiceURL();
  bool setClientWebServiceURL(const char* url);
  
  // Firmware Version
  String getFirmwareVersion();
  bool setFirmwareVersion(const char* version);
  
  // Real Time Interval (seconds)
  uint16_t getRealTimeIntervalSec();
  bool setRealTimeIntervalSec(uint16_t seconds);
  
  // Resetear a valores por defecto
  void resetToDefaults();
  
 private:
  EepromManager() = default;
  
  Preferences prefs_;
  bool initialized_ = false;
  
  static constexpr const char* kNamespace = "pumpmon";
  static constexpr const char* kKeyMasterUrl = "masterUrl";
  static constexpr const char* kKeyClientUrl = "clientUrl";
  static constexpr const char* kKeyFirmwareVer = "fwVersion";
  static constexpr const char* kKeyRTInterval = "rtInterval";
  static constexpr const char* kDefaultMasterUrl = "https://pumpmonitor.agrotecsa.com.mx/";
  static constexpr const char* kDefaultClientUrl = "http://castaindigo-001-site4.itempurl.com/api/";
  static constexpr const char* kDefaultFirmwareVer = "DEV.00";
  static constexpr uint16_t kDefaultRTInterval = 3;
  static constexpr size_t kMaxUrlLength = 128;
  static constexpr size_t kMaxVersionLength = 32;
};
