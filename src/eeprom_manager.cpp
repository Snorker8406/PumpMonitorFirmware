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

  // Cargar lista de dispositivos Modbus (o sembrar con defaults si está vacía)
  loadModbusDevices();
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

String EepromManager::getPendingBackupResult() {
  if (!initialized_) {
    return "";
  }
  return prefs_.getString(kKeyBackupResult, "");
}

bool EepromManager::setPendingBackupResult(const char* result) {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot set backup result\n");
    return false;
  }
  
  if (!result || strlen(result) == 0) {
    return false;
  }
  
  size_t written = prefs_.putString(kKeyBackupResult, result);
  if (written == 0) {
    LOGE("EEPROM: Failed to write backup result\n");
    return false;
  }
  
  LOGI("EEPROM: Backup result saved: %s\n", result);
  return true;
}

void EepromManager::clearPendingBackupResult() {
  if (!initialized_) {
    return;
  }
  prefs_.remove(kKeyBackupResult);
  LOGI("EEPROM: Backup result cleared\n");
}

bool EepromManager::hasPendingBackupResult() {
  if (!initialized_) {
    return false;
  }
  return prefs_.isKey(kKeyBackupResult);
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

  // Restablecer dispositivos Modbus desde defaults y persistir
  seedModbusDevicesFromDefaults();
  saveModbusDevices();
}

// ── Dispositivos Modbus ──

size_t EepromManager::getModbusDeviceCount() const {
  return modbusDeviceCount_;
}

const ModbusDeviceConfig& EepromManager::getModbusDevice(size_t index) const {
  if (index >= modbusDeviceCount_) {
    index = 0;  // Clamp para evitar acceso fuera de rango
  }
  return modbusDevices_[index];
}

void EepromManager::seedModbusDevicesFromDefaults() {
  modbusDeviceCount_ = kDefaultModbusDeviceCount;
  if (modbusDeviceCount_ > kMaxModbusDevices) {
    modbusDeviceCount_ = kMaxModbusDevices;
  }
  for (size_t i = 0; i < modbusDeviceCount_; ++i) {
    modbusDevices_[i] = kDefaultModbusDevices[i];
    // Copiar el nombre a un buffer estable y apuntar a él
    strncpy(modbusModelNames_[i], kDefaultModbusDevices[i].modbusModelName, kModbusModelNameLen - 1);
    modbusModelNames_[i][kModbusModelNameLen - 1] = '\0';
    modbusDevices_[i].modbusModelName = modbusModelNames_[i];
  }
}

void EepromManager::loadModbusDevices() {
  uint8_t storedCount = prefs_.getUChar(kKeyModbusCount, 0);
  size_t blobLen = prefs_.getBytesLength(kKeyModbusDevs);

  if (storedCount == 0 || blobLen == 0) {
    // Primera vez o EEPROM vacía: sembrar con defaults y persistir
    seedModbusDevicesFromDefaults();
    saveModbusDevices();
    LOGI("EEPROM: Modbus devices seeded from defaults (%u)\n", (unsigned)modbusDeviceCount_);
    return;
  }

  if (storedCount > kMaxModbusDevices) {
    storedCount = kMaxModbusDevices;
  }

  ModbusDeviceConfigStored buf[kMaxModbusDevices];
  size_t toRead = sizeof(ModbusDeviceConfigStored) * storedCount;
  size_t read = prefs_.getBytes(kKeyModbusDevs, buf, toRead);
  if (read != toRead) {
    LOGE("EEPROM: Modbus blob size mismatch (%u != %u), using defaults\n",
         (unsigned)read, (unsigned)toRead);
    seedModbusDevicesFromDefaults();
    saveModbusDevices();
    return;
  }

  modbusDeviceCount_ = storedCount;
  for (size_t i = 0; i < modbusDeviceCount_; ++i) {
    const ModbusDeviceConfigStored& s = buf[i];
    modbusDevices_[i].ip = IPAddress(s.ip[0], s.ip[1], s.ip[2], s.ip[3]);
    modbusDevices_[i].unitId = s.unitId;
    modbusDevices_[i].startReg = s.startReg;
    modbusDevices_[i].totalRegs = s.totalRegs;
    modbusDevices_[i].regType = static_cast<ModbusRegisterType>(s.regType);
    modbusDevices_[i].swapWords = (s.swapWords != 0);
    modbusDevices_[i].modbusModelId = s.modbusModelId;
    strncpy(modbusModelNames_[i], s.modbusModelName, kModbusModelNameLen - 1);
    modbusModelNames_[i][kModbusModelNameLen - 1] = '\0';
    modbusDevices_[i].modbusModelName = modbusModelNames_[i];
  }
  LOGI("EEPROM: Modbus devices loaded (%u)\n", (unsigned)modbusDeviceCount_);
}

bool EepromManager::saveModbusDevices() {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot save Modbus devices\n");
    return false;
  }

  ModbusDeviceConfigStored buf[kMaxModbusDevices];
  for (size_t i = 0; i < modbusDeviceCount_; ++i) {
    const ModbusDeviceConfig& d = modbusDevices_[i];
    buf[i].ip[0] = d.ip[0];
    buf[i].ip[1] = d.ip[1];
    buf[i].ip[2] = d.ip[2];
    buf[i].ip[3] = d.ip[3];
    buf[i].unitId = d.unitId;
    buf[i].startReg = d.startReg;
    buf[i].totalRegs = d.totalRegs;
    buf[i].regType = static_cast<uint8_t>(d.regType);
    buf[i].swapWords = d.swapWords ? 1 : 0;
    buf[i].modbusModelId = d.modbusModelId;
    strncpy(buf[i].modbusModelName, d.modbusModelName ? d.modbusModelName : "", kModbusModelNameLen - 1);
    buf[i].modbusModelName[kModbusModelNameLen - 1] = '\0';
  }

  prefs_.putUChar(kKeyModbusCount, static_cast<uint8_t>(modbusDeviceCount_));
  size_t written = prefs_.putBytes(kKeyModbusDevs, buf, sizeof(ModbusDeviceConfigStored) * modbusDeviceCount_);
  if (written == 0 && modbusDeviceCount_ > 0) {
    LOGE("EEPROM: Failed to write Modbus devices\n");
    return false;
  }
  LOGI("EEPROM: Modbus devices saved (%u)\n", (unsigned)modbusDeviceCount_);
  return true;
}

bool EepromManager::setModbusDevices(const ModbusDeviceConfig* devices, size_t count) {
  if (!devices || count == 0) {
    LOGE("EEPROM: Invalid Modbus device list\n");
    return false;
  }
  if (count > kMaxModbusDevices) {
    LOGW("EEPROM: Modbus device count %u truncated to %u\n", (unsigned)count, (unsigned)kMaxModbusDevices);
    count = kMaxModbusDevices;
  }

  modbusDeviceCount_ = count;
  for (size_t i = 0; i < count; ++i) {
    modbusDevices_[i] = devices[i];
    strncpy(modbusModelNames_[i], devices[i].modbusModelName ? devices[i].modbusModelName : "", kModbusModelNameLen - 1);
    modbusModelNames_[i][kModbusModelNameLen - 1] = '\0';
    modbusDevices_[i].modbusModelName = modbusModelNames_[i];
  }
  return saveModbusDevices();
}
