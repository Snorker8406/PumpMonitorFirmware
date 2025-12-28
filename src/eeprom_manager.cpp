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

  // Verificar si existe Master URL en EEPROM
  String currentMasterUrl = prefs_.getString(kKeyMasterUrl, "");
  
  if (currentMasterUrl.isEmpty()) {
    // Primera vez o EEPROM vacía - guardar URL por defecto
    if (setMasterWebServiceURL(kDefaultMasterUrl)) {
      LOGI("EEPROM: Master URL set to default: %s\n", kDefaultMasterUrl);
    } else {
      LOGE("EEPROM: Failed to set default Master URL\n");
    }
  } else {
    LOGI("EEPROM: Master URL loaded: %s\n", currentMasterUrl.c_str());
  }
  
  // Verificar si existe Client URL en EEPROM
  String currentClientUrl = prefs_.getString(kKeyClientUrl, "");
  
  if (currentClientUrl.isEmpty()) {
    // Primera vez o EEPROM vacía - guardar URL por defecto
    if (setClientWebServiceURL(kDefaultClientUrl)) {
      LOGI("EEPROM: Client URL set to default: %s\n", kDefaultClientUrl);
    } else {
      LOGE("EEPROM: Failed to set default Client URL\n");
    }
  } else {
    LOGI("EEPROM: Client URL loaded: %s\n", currentClientUrl.c_str());
  }
  
  // Verificar si existe Firmware Version en EEPROM
  String currentFwVer = prefs_.getString(kKeyFirmwareVer, "");
  
  if (currentFwVer.isEmpty()) {
    // Primera vez o EEPROM vacía - guardar versión por defecto
    if (setFirmwareVersion(kDefaultFirmwareVer)) {
      LOGI("EEPROM: Firmware Version set to default: %s\n", kDefaultFirmwareVer);
    } else {
      LOGE("EEPROM: Failed to set default Firmware Version\n");
    }
  } else {
    LOGI("EEPROM: Firmware Version loaded: %s\n", currentFwVer.c_str());
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
}

String EepromManager::getMasterWebServiceURL() {
  if (!initialized_) {
    LOGW("EEPROM: Not initialized, returning default URL\n");
    return String(kDefaultMasterUrl);
  }

  String url = prefs_.getString(kKeyMasterUrl, kDefaultMasterUrl);
  return url;
}

bool EepromManager::setMasterWebServiceURL(const char* url) {
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
  size_t written = prefs_.putString(kKeyMasterUrl, url);
  if (written == 0) {
    LOGE("EEPROM: Failed to write Master URL\n");
    return false;
  }

  LOGI("EEPROM: Master URL updated: %s\n", url);
  return true;
}

String EepromManager::getClientWebServiceURL() {
  if (!initialized_) {
    LOGW("EEPROM: Not initialized, returning default Client URL\n");
    return String(kDefaultClientUrl);
  }

  String url = prefs_.getString(kKeyClientUrl, kDefaultClientUrl);
  return url;
}

bool EepromManager::setClientWebServiceURL(const char* url) {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot set Client URL\n");
    return false;
  }

  if (!url || strlen(url) == 0) {
    LOGE("EEPROM: Invalid Client URL (empty)\n");
    return false;
  }

  if (strlen(url) >= kMaxUrlLength) {
    LOGE("EEPROM: Client URL too long (max %u chars)\n", kMaxUrlLength);
    return false;
  }

  // Guardar en EEPROM
  size_t written = prefs_.putString(kKeyClientUrl, url);
  if (written == 0) {
    LOGE("EEPROM: Failed to write Client URL\n");
    return false;
  }

  LOGI("EEPROM: Client URL updated: %s\n", url);
  return true;
}

String EepromManager::getFirmwareVersion() {
  if (!initialized_) {
    LOGW("EEPROM: Not initialized, returning default Firmware Version\n");
    return String(kDefaultFirmwareVer);
  }

  String version = prefs_.getString(kKeyFirmwareVer, kDefaultFirmwareVer);
  return version;
}

bool EepromManager::setFirmwareVersion(const char* version) {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot set Firmware Version\n");
    return false;
  }

  if (!version || strlen(version) == 0) {
    LOGE("EEPROM: Invalid Firmware Version (empty)\n");
    return false;
  }

  if (strlen(version) >= kMaxVersionLength) {
    LOGE("EEPROM: Firmware Version too long (max %u chars)\n", kMaxVersionLength);
    return false;
  }

  // Guardar en EEPROM
  size_t written = prefs_.putString(kKeyFirmwareVer, version);
  if (written == 0) {
    LOGE("EEPROM: Failed to write Firmware Version\n");
    return false;
  }

  LOGI("EEPROM: Firmware Version updated: %s\n", version);
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

void EepromManager::resetToDefaults() {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot reset\n");
    return;
  }

  prefs_.clear();
  LOGW("EEPROM: All settings cleared\n");
  
  // Restablecer valores por defecto
  setMasterWebServiceURL(kDefaultMasterUrl);
  setClientWebServiceURL(kDefaultClientUrl);
  setFirmwareVersion(kDefaultFirmwareVer);
  setRealTimeIntervalSec(kDefaultRTInterval);
}
