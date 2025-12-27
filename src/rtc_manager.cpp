#include "rtc_manager.hpp"

#include "log.hpp"

RtcManager &RtcManager::instance() {
  static RtcManager inst;
  return inst;
}

bool RtcManager::begin() {
  if (initialized_) {
    LOGW("RTC already initialized\n");
    return true;
  }

  // Inicializar I2C con pines específicos
  Wire.begin(kRtcSdaPin, kRtcSclPin);
  
  LOGI("RTC initializing (SDA=%d, SCL=%d)...\n", kRtcSdaPin, kRtcSclPin);
  
  if (!rtc_.begin(&Wire)) {
    LOGE("RTC not found!\n");
    return false;
  }

  initialized_ = true;

  // Verificar si el RTC perdió alimentación
  if (rtc_.lostPower()) {
    LOGW("RTC lost power, time may be incorrect!\n");
    LOGW("Please set the correct time using setDateTime()\n");
  }

  // Información del RTC
  DateTime now = rtc_.now();
  LOGI("RTC initialized | Time: %04d-%02d-%02d %02d:%02d:%02d | Temp: %.2f°C\n",
       now.year(), now.month(), now.day(),
       now.hour(), now.minute(), now.second(),
       rtc_.getTemperature());

  return true;
}

bool RtcManager::isAvailable() const {
  return initialized_;
}

DateTime RtcManager::now() {
  if (!initialized_) {
    return DateTime((uint32_t)0);  // Epoch time si no está inicializado
  }
  return rtc_.now();
}

time_t RtcManager::getUnixTime() {
  if (!initialized_) {
    return 0;
  }
  return rtc_.now().unixtime();
}

bool RtcManager::setDateTime(uint16_t year, uint8_t month, uint8_t day,
                              uint8_t hour, uint8_t minute, uint8_t second) {
  if (!initialized_) {
    return false;
  }

  DateTime dt(year, month, day, hour, minute, second);
  rtc_.adjust(dt);
  
  LOGI("RTC time set to: %04d-%02d-%02d %02d:%02d:%02d\n",
       year, month, day, hour, minute, second);
  
  return true;
}

bool RtcManager::setDateTime(const DateTime &dt) {
  if (!initialized_) {
    return false;
  }

  rtc_.adjust(dt);
  
  LOGI("RTC time updated\n");
  
  return true;
}

float RtcManager::getTemperature() {
  if (!initialized_) {
    return 0.0f;
  }
  return rtc_.getTemperature();
}

bool RtcManager::lostPower() {
  if (!initialized_) {
    return true;
  }
  return rtc_.lostPower();
}

void RtcManager::getDateTimeString(char* buffer, size_t bufferSize) {
  if (!initialized_ || buffer == nullptr || bufferSize < 20) {
    if (buffer && bufferSize > 0) buffer[0] = '\0';
    return;
  }

  DateTime now = rtc_.now();
  snprintf(buffer, bufferSize, "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
}

void RtcManager::getDateString(char* buffer, size_t bufferSize) {
  if (!initialized_ || buffer == nullptr || bufferSize < 11) {
    if (buffer && bufferSize > 0) buffer[0] = '\0';
    return;
  }

  DateTime now = rtc_.now();
  snprintf(buffer, bufferSize, "%04d-%02d-%02d",
           now.year(), now.month(), now.day());
}

void RtcManager::getTimeString(char* buffer, size_t bufferSize) {
  if (!initialized_ || buffer == nullptr || bufferSize < 9) {
    if (buffer && bufferSize > 0) buffer[0] = '\0';
    return;
  }

  DateTime now = rtc_.now();
  snprintf(buffer, bufferSize, "%02d:%02d:%02d",
           now.hour(), now.minute(), now.second());
}
