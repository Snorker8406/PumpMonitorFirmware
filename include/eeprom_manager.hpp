#pragma once

#include <Arduino.h>
#include <Preferences.h>

class EepromManager {
 public:
  static EepromManager &instance();

  void begin();
  
  // Web Service URL
  String getWebServiceURL();
  bool setWebServiceURL(const char* url);
  
  // Real Time Interval (seconds)
  uint16_t getRealTimeIntervalSec();
  bool setRealTimeIntervalSec(uint16_t seconds);
  
  // Device ID
  int32_t getDeviceID();
  bool setDeviceID(int32_t id);
  
  // Resetear a valores por defecto
  void resetToDefaults();
  
 private:
  EepromManager() = default;
  
  Preferences prefs_;
  bool initialized_ = false;
  
  static constexpr const char* kNamespace = "pumpmon";
  static constexpr const char* kKeyUrl = "webUrl";
  static constexpr const char* kKeyRTInterval = "rtInterval";
  static constexpr const char* kKeyDeviceID = "deviceId";
  static constexpr const char* kDefaultUrl = "https://pumpmonitor.agrotecsa.com.mx/";
  static constexpr uint16_t kDefaultRTInterval = 3;
  static constexpr int32_t kDefaultDeviceID = 1;
  static constexpr size_t kMaxUrlLength = 128;
};
