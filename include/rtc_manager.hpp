#pragma once

#include <Wire.h>
#include <RTClib.h>

#include "app_config.hpp"

class RtcManager {
 public:
  static RtcManager &instance();

  bool begin();
  bool isAvailable() const;
  
  // Obtener fecha y hora actual
  DateTime now();
  time_t getUnixTime();
  
  // Configurar fecha y hora (para sincronización inicial)
  bool setDateTime(uint16_t year, uint8_t month, uint8_t day, 
                   uint8_t hour, uint8_t minute, uint8_t second);
  bool setDateTime(const DateTime &dt);
  
  // Información del RTC
  float getTemperature();  // DS3231 tiene sensor de temperatura
  bool lostPower();        // Verifica si perdió alimentación
  
  // Formato de fecha/hora para logs (con buffer seguro)
  void getDateTimeString(char* buffer, size_t bufferSize);
  void getDateString(char* buffer, size_t bufferSize);
  void getTimeString(char* buffer, size_t bufferSize);

 private:
  RtcManager() = default;
  
  bool initialized_ = false;
  RTC_DS3231 rtc_;
};
