#include "eeprom_manager.hpp"
#include "log.hpp"

EepromManager &EepromManager::instance() {
  static EepromManager inst;
  return inst;
}

void EepromManager::begin() {
  if (initialized_) {
    return;
  }

  // Abrir namespace en modo lectura-escritura
  if (!prefs_.begin(kNamespace, false)) {
    LOGE("EEPROM: Failed to initialize Preferences\n");
    return;
  }

  initialized_ = true;
  LOGI("EEPROM: Initialized\n");

  // Verificar si existe URL en EEPROM
  String currentUrl = prefs_.getString(kKeyUrl, "");
  
  if (currentUrl.isEmpty()) {
    // Primera vez o EEPROM vacía - guardar URL por defecto
    if (setWebServiceURL(kDefaultUrl)) {
      LOGI("EEPROM: URL set to default: %s\n", kDefaultUrl);
    } else {
      LOGE("EEPROM: Failed to set default URL\n");
    }
  } else {
    LOGI("EEPROM: URL loaded: %s\n", currentUrl.c_str());
  }
  
  // Verificar si existe Real Time Interval en EEPROM
  uint16_t currentRTInterval = prefs_.getUShort(kKeyRTInterval, 0);
  
  if (currentRTInterval == 0) {
    // Primera vez o EEPROM vacía - guardar intervalo por defecto
    if (setRealTimeIntervalSec(kDefaultRTInterval)) {
      LOGI("EEPROM: Real Time Interval set to default: %u sec\n", kDefaultRTInterval);
    } else {
      LOGE("EEPROM: Failed to set default Real Time Interval\n");
    }
  } else {
    LOGI("EEPROM: Real Time Interval loaded: %u sec\n", currentRTInterval);
  }
  
  // Verificar si existe Instant Values Interval en EEPROM
  uint16_t currentIVInterval = prefs_.getUShort(kKeyIVInterval, 0);
  
  if (currentIVInterval == 0) {
    // Primera vez o EEPROM vacía - guardar intervalo por defecto
    if (setInstantValuesIntervalSec(kDefaultIVInterval)) {
      LOGI("EEPROM: Instant Values Interval set to default: %u sec\n", kDefaultIVInterval);
    } else {
      LOGE("EEPROM: Failed to set default Instant Values Interval\n");
    }
  } else {
    LOGI("EEPROM: Instant Values Interval loaded: %u sec\n", currentIVInterval);
  }
  
  // Verificar si existe Device ID en EEPROM
  int32_t currentDeviceID = prefs_.getInt(kKeyDeviceID, -1);
  
  if (currentDeviceID == -1) {
    // Primera vez o EEPROM vacía - guardar valor por defecto
    if (setDeviceID(kDefaultDeviceID)) {
      LOGI("EEPROM: Device ID set to default: %d\n", kDefaultDeviceID);
    } else {
      LOGE("EEPROM: Failed to set default Device ID\n");
    }
  } else {
    LOGI("EEPROM: Device ID loaded: %d\n", currentDeviceID);
  }
}

String EepromManager::getWebServiceURL() {
  if (!initialized_) {
    LOGW("EEPROM: Not initialized, returning default URL\n");
    return String(kDefaultUrl);
  }

  String url = prefs_.getString(kKeyUrl, kDefaultUrl);
  return url;
}

bool EepromManager::setWebServiceURL(const char* url) {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot set URL\n");
    return false;
  }

  if (!url || strlen(url) == 0) {
    LOGE("EEPROM: Invalid URL (empty)\n");
    return false;
  }

  if (strlen(url) >= kMaxUrlLength) {
    LOGE("EEPROM: URL too long (max %u chars)\n", kMaxUrlLength);
    return false;
  }

  // Guardar en EEPROM
  size_t written = prefs_.putString(kKeyUrl, url);
  if (written == 0) {
    LOGE("EEPROM: Failed to write URL\n");
    return false;
  }

  LOGI("EEPROM: URL updated: %s\n", url);
  return true;
}

uint16_t EepromManager::getRealTimeIntervalSec() {
  if (!initialized_) {
    LOGW("EEPROM: Not initialized, returning default Real Time Interval\n");
    return kDefaultRTInterval;
  }

  uint16_t interval = prefs_.getUShort(kKeyRTInterval, kDefaultRTInterval);
  return interval;
}

bool EepromManager::setRealTimeIntervalSec(uint16_t seconds) {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot set Real Time Interval\n");
    return false;
  }

  if (seconds == 0 || seconds > 3600) {
    LOGE("EEPROM: Invalid Real Time Interval (must be 1-3600 seconds)\n");
    return false;
  }

  // Guardar en EEPROM
  size_t written = prefs_.putUShort(kKeyRTInterval, seconds);
  if (written == 0) {
    LOGE("EEPROM: Failed to write Real Time Interval\n");
    return false;
  }

  LOGI("EEPROM: Real Time Interval updated: %u sec\n", seconds);
  return true;
}

uint16_t EepromManager::getInstantValuesIntervalSec() {
  if (!initialized_) {
    LOGW("EEPROM: Not initialized, returning default Instant Values Interval\n");
    return kDefaultIVInterval;
  }

  uint16_t interval = prefs_.getUShort(kKeyIVInterval, kDefaultIVInterval);
  return interval;
}

bool EepromManager::setInstantValuesIntervalSec(uint16_t seconds) {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot set Instant Values Interval\n");
    return false;
  }

  if (seconds == 0 || seconds > 3600) {
    LOGE("EEPROM: Invalid Instant Values Interval (must be 1-3600 seconds)\n");
    return false;
  }

  // Guardar en EEPROM
  size_t written = prefs_.putUShort(kKeyIVInterval, seconds);
  if (written == 0) {
    LOGE("EEPROM: Failed to write Instant Values Interval\n");
    return false;
  }

  LOGI("EEPROM: Instant Values Interval updated: %u sec\n", seconds);
  return true;
}

int32_t EepromManager::getDeviceID() {
  if (!initialized_) {
    LOGW("EEPROM: Not initialized, returning default Device ID\n");
    return kDefaultDeviceID;
  }

  int32_t id = prefs_.getInt(kKeyDeviceID, kDefaultDeviceID);
  return id;
}

bool EepromManager::setDeviceID(int32_t id) {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot set Device ID\n");
    return false;
  }

  // Guardar en EEPROM
  size_t written = prefs_.putInt(kKeyDeviceID, id);
  if (written == 0) {
    LOGE("EEPROM: Failed to write Device ID\n");
    return false;
  }

  LOGI("EEPROM: Device ID updated: %d\n", id);
  return true;
}

void EepromManager::resetToDefaults() {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot reset\n");
    return;
  }

  prefs_.clear();
  LOGW("EEPROM: All settings cleared\n");
  
  // Restablecer valores por defecto
  setWebServiceURL(kDefaultUrl);
  setRealTimeIntervalSec(kDefaultRTInterval);
  setInstantValuesIntervalSec(kDefaultIVInterval);
  setDeviceID(kDefaultDeviceID);
}
