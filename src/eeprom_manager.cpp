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
  
  // Sembrar configuración de alarmas si no existe (start=0 es válido, usar isKey)
  if (!prefs_.isKey(kKeyAlarmDevIdx)) {
    setAlarmDeviceIndex((uint8_t)kAlarmModbusDeviceIndex);
  }
  if (!prefs_.isKey(kKeyAlarmStart)) {
    setAlarmStartAddress(kAlarmStartAddress);
  }
  if (!prefs_.isKey(kKeyAlarmCount)) {
    setAlarmCount(kAlarmCount);
  }
  if (!prefs_.isKey(kKeyAlarmFunc)) {
    setAlarmDiscreteInputs(kAlarmDiscreteInputs);
  }
  if (!prefs_.isKey(kKeyAlarmTypes)) {
    setAlarmCoilsTypes(kAlarmCoilsTypes);
  }
  LOGI("EEPROM: Alarm config | devIdx=%u start=%u count=%u\n",
       getAlarmDeviceIndex(), getAlarmStartAddress(), getAlarmCount());  

  // Sembrar configuración del Modbus Server si no existe
  if (!prefs_.isKey(kKeyMbSrvUnit)) {
    setServerUnitId(kModbusServerUnitId);
  }
  if (!prefs_.isKey(kKeyMbSrvPort)) {
    setServerPort(kModbusServerPort);
  }
  if (!prefs_.isKey(kKeyMbSrvMaxCli)) {
    setServerMaxClients(kModbusServerMaxClients);
  }
  if (!prefs_.isKey(kKeyMbSrvTout)) {
    setServerTimeoutMs(kModbusServerTimeoutMs);
  }
  LOGI("EEPROM: Modbus Server config | unitId=%u port=%u maxClients=%u timeout=%lu ms\n",
       getServerUnitId(), getServerPort(), getServerMaxClients(), getServerTimeoutMs());

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

  // Cargar configuración de actuadores (o sembrar con defaults si está vacía)
  loadActuators();
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

uint8_t EepromManager::getAlarmDeviceIndex() {
  if (!initialized_) {
    return (uint8_t)kAlarmModbusDeviceIndex;
  }
  return prefs_.getUChar(kKeyAlarmDevIdx, (uint8_t)kAlarmModbusDeviceIndex);
}

bool EepromManager::setAlarmDeviceIndex(uint8_t index) {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot set Alarm Device Index\n");
    return false;
  }
  if (index >= kMaxModbusDevices) {
    LOGE("EEPROM: Invalid Alarm Device Index (max %u)\n", (unsigned)kMaxModbusDevices);
    return false;
  }
  if (prefs_.putUChar(kKeyAlarmDevIdx, index) == 0) {
    LOGE("EEPROM: Failed to write Alarm Device Index\n");
    return false;
  }
  LOGI("EEPROM: Alarm Device Index updated: %u\n", index);
  return true;
}

uint16_t EepromManager::getAlarmStartAddress() {
  if (!initialized_) {
    return kAlarmStartAddress;
  }
  return prefs_.getUShort(kKeyAlarmStart, kAlarmStartAddress);
}

bool EepromManager::setAlarmStartAddress(uint16_t address) {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot set Alarm Start Address\n");
    return false;
  }
  // Cualquier dirección 0..65535 es válida.
  if (prefs_.putUShort(kKeyAlarmStart, address) == 0) {
    LOGE("EEPROM: Failed to write Alarm Start Address\n");
    return false;
  }
  LOGI("EEPROM: Alarm Start Address updated: %u\n", address);
  return true;
}

uint16_t EepromManager::getAlarmCount() {
  if (!initialized_) {
    return kAlarmCount;
  }
  return prefs_.getUShort(kKeyAlarmCount, kAlarmCount);
}

bool EepromManager::setAlarmCount(uint16_t count) {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot set Alarm Count\n");
    return false;
  }
  if (count == 0 || count > 2000) {
    LOGE("EEPROM: Invalid Alarm Count (must be 1-2000)\n");
    return false;
  }
  if (prefs_.putUShort(kKeyAlarmCount, count) == 0) {
    LOGE("EEPROM: Failed to write Alarm Count\n");
    return false;
  }
  LOGI("EEPROM: Alarm Count updated: %u\n", count);
  return true;
}

bool EepromManager::getAlarmDiscreteInputs() {
  if (!initialized_) {
    return kAlarmDiscreteInputs;
  }
  return prefs_.getBool(kKeyAlarmFunc, kAlarmDiscreteInputs);
}

bool EepromManager::setAlarmDiscreteInputs(bool discreteInputs) {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot set Alarm Function\n");
    return false;
  }
  if (!prefs_.putBool(kKeyAlarmFunc, discreteInputs)) {
    LOGE("EEPROM: Failed to write Alarm Function\n");
    return false;
  }
  LOGI("EEPROM: Alarm Function updated: %s\n",
       discreteInputs ? "FC02 Discrete Inputs" : "FC01 Coils");
  return true;
}

String EepromManager::getAlarmCoilsTypes() {
  if (!initialized_) {
    return String(kAlarmCoilsTypes);
  }
  return prefs_.getString(kKeyAlarmTypes, kAlarmCoilsTypes);
}

bool EepromManager::setAlarmCoilsTypes(const char* types) {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot set Alarm Coil Types\n");
    return false;
  }
  if (!types) {
    LOGE("EEPROM: Invalid Alarm Coil Types (null)\n");
    return false;
  }
  size_t len = strlen(types);
  if (len == 0 || len > kAlarmCoilsTypesMaxLen) {
    LOGE("EEPROM: Invalid Alarm Coil Types length (1..%u)\n", (unsigned)kAlarmCoilsTypesMaxLen);
    return false;
  }
  // Validar que cada carácter sea A/N/C (mayúsculas o minúsculas)
  for (size_t i = 0; i < len; i++) {
    char c = toupper((unsigned char)types[i]);
    if (c != 'A' && c != 'N' && c != 'C') {
      LOGE("EEPROM: Invalid Alarm Coil Type '%c' (use A/N/C)\n", types[i]);
      return false;
    }
  }
  if (prefs_.putString(kKeyAlarmTypes, types) == 0) {
    LOGE("EEPROM: Failed to write Alarm Coil Types\n");
    return false;
  }
  LOGI("EEPROM: Alarm Coil Types updated: %s\n", types);
  return true;
}

// ── Modbus Server (esclavo TCP) ──

uint8_t EepromManager::getServerUnitId() {
  if (!initialized_) {
    return kModbusServerUnitId;
  }
  return (uint8_t)prefs_.getUChar(kKeyMbSrvUnit, kModbusServerUnitId);
}

bool EepromManager::setServerUnitId(uint8_t unitId) {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot set Server Unit ID\n");
    return false;
  }
  if (unitId < 1 || unitId > 247) {
    LOGE("EEPROM: Invalid Server Unit ID (must be 1-247)\n");
    return false;
  }
  if (prefs_.putUChar(kKeyMbSrvUnit, unitId) == 0) {
    LOGE("EEPROM: Failed to write Server Unit ID\n");
    return false;
  }
  LOGI("EEPROM: Server Unit ID updated: %u\n", unitId);
  return true;
}

uint16_t EepromManager::getServerPort() {
  if (!initialized_) {
    return kModbusServerPort;
  }
  return prefs_.getUShort(kKeyMbSrvPort, kModbusServerPort);
}

bool EepromManager::setServerPort(uint16_t port) {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot set Server Port\n");
    return false;
  }
  if (port == 0) {
    LOGE("EEPROM: Invalid Server Port (must be 1-65535)\n");
    return false;
  }
  if (prefs_.putUShort(kKeyMbSrvPort, port) == 0) {
    LOGE("EEPROM: Failed to write Server Port\n");
    return false;
  }
  LOGI("EEPROM: Server Port updated: %u\n", port);
  return true;
}

uint8_t EepromManager::getServerMaxClients() {
  if (!initialized_) {
    return kModbusServerMaxClients;
  }
  return (uint8_t)prefs_.getUChar(kKeyMbSrvMaxCli, kModbusServerMaxClients);
}

bool EepromManager::setServerMaxClients(uint8_t maxClients) {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot set Server Max Clients\n");
    return false;
  }
  if (maxClients < 1 || maxClients > 8) {
    LOGE("EEPROM: Invalid Server Max Clients (must be 1-8)\n");
    return false;
  }
  if (prefs_.putUChar(kKeyMbSrvMaxCli, maxClients) == 0) {
    LOGE("EEPROM: Failed to write Server Max Clients\n");
    return false;
  }
  LOGI("EEPROM: Server Max Clients updated: %u\n", maxClients);
  return true;
}

uint32_t EepromManager::getServerTimeoutMs() {
  if (!initialized_) {
    return kModbusServerTimeoutMs;
  }
  return prefs_.getULong(kKeyMbSrvTout, kModbusServerTimeoutMs);
}

bool EepromManager::setServerTimeoutMs(uint32_t timeoutMs) {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot set Server Timeout\n");
    return false;
  }
  if (timeoutMs < 1000 || timeoutMs > 120000) {
    LOGE("EEPROM: Invalid Server Timeout (must be 1000-120000 ms)\n");
    return false;
  }
  if (prefs_.putULong(kKeyMbSrvTout, timeoutMs) == 0) {
    LOGE("EEPROM: Failed to write Server Timeout\n");
    return false;
  }
  LOGI("EEPROM: Server Timeout updated: %lu ms\n", timeoutMs);
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

  // Restablecer actuadores desde defaults y persistir
  seedActuatorsFromDefaults();
  saveActuators();
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

bool EepromManager::clearModbusDevices() {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot clear Modbus devices\n");
    return false;
  }
  modbusDeviceCount_ = 0;
  prefs_.putUChar(kKeyModbusCount, 0);
  prefs_.remove(kKeyModbusDevs);
  LOGI("EEPROM: Modbus devices cleared\n");
  return true;
}

// ── Actuadores ──

size_t EepromManager::getActuatorModbusDeviceIndex() const {
  return actuatorDeviceIndex_;
}

uint16_t EepromManager::getActuatorCoilOnAddress(size_t coilIndex) const {
  if (coilIndex >= kActuatorCoilCount) {
    return 0;
  }
  return actuatorCoilOnAddresses_[coilIndex];
}

bool EepromManager::getActuatorCoilOnValue(size_t coilIndex) const {
  if (coilIndex >= kActuatorCoilCount) {
    return true;
  }
  return actuatorCoilOnValues_[coilIndex];
}

uint16_t EepromManager::getActuatorCoilOffAddress(size_t coilIndex) const {
  if (coilIndex >= kActuatorCoilCount) {
    return 0;
  }
  return actuatorCoilOffAddresses_[coilIndex];
}

bool EepromManager::getActuatorCoilOffValue(size_t coilIndex) const {
  if (coilIndex >= kActuatorCoilCount) {
    return false;
  }
  return actuatorCoilOffValues_[coilIndex];
}

bool EepromManager::getActuatorCoilEnabled(size_t coilIndex) const {
  if (coilIndex >= kActuatorCoilCount) {
    return false;
  }
  return actuatorCoilEnabled_[coilIndex];
}

void EepromManager::seedActuatorsFromDefaults() {
  actuatorDeviceIndex_ = kActuatorModbusDeviceIndex;
  for (size_t i = 0; i < kActuatorCoilCount; ++i) {
    actuatorCoilOnAddresses_[i]  = kActuatorCoilOnAddresses[i];
    actuatorCoilOnValues_[i]     = kActuatorCoilOnValues[i];
    actuatorCoilOffAddresses_[i] = kActuatorCoilOffAddresses[i];
    actuatorCoilOffValues_[i]    = kActuatorCoilOffValues[i];
    actuatorCoilEnabled_[i]      = kActuatorConfirmationsEnabled[i];
  }
}

void EepromManager::loadActuators() {
  size_t blobLen = prefs_.getBytesLength(kKeyActCoils);

  if (blobLen == 0) {
    // Primera vez o EEPROM vacía: sembrar con defaults y persistir
    seedActuatorsFromDefaults();
    saveActuators();
    LOGI("EEPROM: Actuators seeded from defaults\n");
    return;
  }

  ActuatorCoilStored buf[kActuatorCoilCount];
  size_t toRead = sizeof(ActuatorCoilStored) * kActuatorCoilCount;
  size_t read = prefs_.getBytes(kKeyActCoils, buf, toRead);
  if (read != toRead) {
    LOGE("EEPROM: Actuator blob size mismatch (%u != %u), using defaults\n",
         (unsigned)read, (unsigned)toRead);
    seedActuatorsFromDefaults();
    saveActuators();
    return;
  }

  actuatorDeviceIndex_ = prefs_.getUChar(kKeyActDevIdx, (uint8_t)kActuatorModbusDeviceIndex);
  for (size_t i = 0; i < kActuatorCoilCount; ++i) {
    actuatorCoilOnAddresses_[i]  = buf[i].onAddress;
    actuatorCoilOnValues_[i]     = (buf[i].onValue != 0);
    actuatorCoilOffAddresses_[i] = buf[i].offAddress;
    actuatorCoilOffValues_[i]    = (buf[i].offValue != 0);
    actuatorCoilEnabled_[i]      = (buf[i].enabled != 0);
  }
  LOGI("EEPROM: Actuators loaded (deviceIndex=%u)\n", (unsigned)actuatorDeviceIndex_);
}

bool EepromManager::saveActuators() {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot save actuators\n");
    return false;
  }

  ActuatorCoilStored buf[kActuatorCoilCount];
  for (size_t i = 0; i < kActuatorCoilCount; ++i) {
    buf[i].onAddress  = actuatorCoilOnAddresses_[i];
    buf[i].onValue    = actuatorCoilOnValues_[i]  ? 1 : 0;
    buf[i].offAddress = actuatorCoilOffAddresses_[i];
    buf[i].offValue   = actuatorCoilOffValues_[i] ? 1 : 0;
    buf[i].enabled    = actuatorCoilEnabled_[i]   ? 1 : 0;
  }

  prefs_.putUChar(kKeyActDevIdx, (uint8_t)actuatorDeviceIndex_);
  size_t written = prefs_.putBytes(kKeyActCoils, buf, sizeof(buf));
  if (written != sizeof(buf)) {
    LOGE("EEPROM: Failed to write actuators\n");
    return false;
  }
  LOGI("EEPROM: Actuators saved (deviceIndex=%u)\n", (unsigned)actuatorDeviceIndex_);
  return true;
}

bool EepromManager::setActuatorConfig(size_t deviceIndex,
                                       const uint16_t* onAddresses, const bool* onValues,
                                       const uint16_t* offAddresses, const bool* offValues,
                                       const bool* enabled) {
  if (!onAddresses || !onValues || !offAddresses || !offValues || !enabled) {
    LOGE("EEPROM: Invalid actuator config\n");
    return false;
  }
  actuatorDeviceIndex_ = deviceIndex;
  for (size_t i = 0; i < kActuatorCoilCount; ++i) {
    actuatorCoilOnAddresses_[i]  = onAddresses[i];
    actuatorCoilOnValues_[i]     = onValues[i];
    actuatorCoilOffAddresses_[i] = offAddresses[i];
    actuatorCoilOffValues_[i]    = offValues[i];
    actuatorCoilEnabled_[i]      = enabled[i];
  }
  return saveActuators();
}

bool EepromManager::clearActuatorConfig() {
  if (!initialized_) {
    LOGE("EEPROM: Not initialized, cannot clear actuators\n");
    return false;
  }
  // Restablecer a defaults de app_config y persistir.
  seedActuatorsFromDefaults();
  bool ok = saveActuators();
  LOGI("EEPROM: Actuators reset to defaults\n");
  return ok;
}

