#include "modbus_manager.hpp"

#include "eeprom_manager.hpp"
#include "log.hpp"
#include "network_manager.hpp"

// ── Instancias estáticas del módulo (patrón del ejemplo funcional) ──
static WiFiClient      s_tcpClient;
static ModbusClientTCP s_mbClient(s_tcpClient);

ModbusManager &ModbusManager::instance() {
  static ModbusManager inst;
  return inst;
}

void ModbusManager::begin() {
  mutex_ = xSemaphoreCreateMutex();
  if (!mutex_) {
    LOGE("Failed to create Modbus mutex\n");
  }

  // Configurar timeout y intervalo mínimo entre requests
  s_mbClient.setTimeout(kModbusTimeoutMs, kModbusIntervalMs);
  s_mbClient.begin(kModbusTaskCore);

  LOGI("eModbus initialized (syncRequest mode, timeout=%u ms, interval=%u ms)\n",
       kModbusTimeoutMs, kModbusIntervalMs);
}

void ModbusManager::loop() {
  // No-op: syncRequest maneja todo internamente
}

bool ModbusManager::readDevice(size_t deviceIndex, std::vector<float> &values, std::vector<uint16_t> *rawData) {
  if (deviceIndex >= EepromManager::instance().getModbusDeviceCount()) {
    return false;
  }

  if (!NetworkManager::instance().isConnected()) {
    return false;
  }

  // Serializar acceso al cliente compartido
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(15000)) != pdTRUE) {
    LOGE("Modbus[%u] mutex timeout\n", deviceIndex);
    return false;
  }

  const auto &config = EepromManager::instance().getModbusDevice(deviceIndex);

  values.clear();
  if (rawData) {
    rawData->clear();
  }

  // Apuntar al dispositivo destino
  s_mbClient.setTarget(config.ip, 502);

  // Function code según tipo de registro
  uint8_t fc = (config.regType == ModbusRegisterType::HOLDING_REGISTER)
                 ? READ_HOLD_REGISTER
                 : READ_INPUT_REGISTER;

  // Lectura síncrona bloqueante (bloquea esta tarea hasta respuesta o timeout)
  uint32_t tok = ++token_;
  ModbusMessage response = s_mbClient.syncRequest(
    tok,
    config.unitId,
    fc,
    config.startReg,
    config.totalRegs
  );

  // Verificar error
  Error err = response.getError();
  if (err != SUCCESS) {
    ModbusError me(err);
    LOGE("Modbus[%u] %s (%u.%u.%u.%u): error %02X - %s\n",
         deviceIndex, config.modbusModelName,
         config.ip[0], config.ip[1], config.ip[2], config.ip[3],
         (int)err, (const char *)me);
    xSemaphoreGive(mutex_);
    return false;
  }

  // Extraer byte count (posición 2 del mensaje)
  uint8_t byteCount = 0;
  response.get(2, byteCount);
  uint16_t regsReceived = byteCount / 2;

  // Extraer registros raw
  std::vector<uint16_t> allRegs;
  allRegs.reserve(regsReceived);

  for (uint16_t i = 0; i < regsReceived; i++) {
    uint16_t regVal = 0;
    response.get(3 + i * 2, regVal);
    allRegs.push_back(regVal);
  }

  // Liberar mutex después de la lectura
  xSemaphoreGive(mutex_);

  // Guardar datos raw si se solicitan
  if (rawData) {
    *rawData = allRegs;
  }

  // Convertir pares de registros a floats (respetando word order del dispositivo)
  values.reserve(allRegs.size() / 2);
  for (size_t i = 0; i + 1 < allRegs.size(); i += 2) {
    float val = config.swapWords
      ? regsToFloat(allRegs[i + 1], allRegs[i])    // CD AB (Little-Endian words)
      : regsToFloat(allRegs[i],     allRegs[i + 1]); // AB CD (Big-Endian words)
    values.push_back(val);
  }

  LOGD("Modbus[%u] read %u regs, %u floats from %s\n",
       deviceIndex, allRegs.size(), values.size(), config.modbusModelName);

  return true;
}

bool ModbusManager::readAllDevices(std::vector<ModbusDeviceData> &devicesData) {
  devicesData.clear();
  const size_t deviceCount = EepromManager::instance().getModbusDeviceCount();
  devicesData.reserve(deviceCount);
  bool allSuccess = true;

  for (size_t i = 0; i < deviceCount; i++) {
    const auto &config = EepromManager::instance().getModbusDevice(i);
    ModbusDeviceData data;
    data.modbusModelId = config.modbusModelId;
    data.modbusModelName = config.modbusModelName;
    data.ip = config.ip;
    data.success = readDevice(i, data.values, &data.rawData);

    if (!data.success) {
      allSuccess = false;
      LOGE("Failed to read %s (%u.%u.%u.%u)\n", config.modbusModelName,
           config.ip[0], config.ip[1], config.ip[2], config.ip[3]);
    }

    devicesData.push_back(data);

    // Pausa entre dispositivos para no saturar la red
    if (i < deviceCount - 1) {
      vTaskDelay(pdMS_TO_TICKS(kModbusInterDeviceDelayMs));
    }
  }

  return allSuccess;
}

float ModbusManager::regsToFloat(uint16_t reg1, uint16_t reg2) {
  union {
    uint32_t i;
    float f;
  } converter;
  converter.i = (static_cast<uint32_t>(reg1) << 16) | reg2;
  return converter.f;
}

bool ModbusManager::readBooleans(size_t deviceIndex, uint16_t startAddress, uint16_t count,
                                 std::vector<bool> &states, bool discreteInputs) {
  states.clear();

  if (deviceIndex >= EepromManager::instance().getModbusDeviceCount()) {
    LOGE("readBooleans: invalid device index %u\n", deviceIndex);
    return false;
  }

  if (count == 0 || count > 2000) {
    LOGE("readBooleans: count fuera de rango (1..2000): %u\n", count);
    return false;
  }

  if (!NetworkManager::instance().isConnected()) {
    return false;
  }

  // Serializar acceso al cliente compartido
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(15000)) != pdTRUE) {
    LOGE("readBooleans[%u] mutex timeout\n", deviceIndex);
    return false;
  }

  const auto &config = EepromManager::instance().getModbusDevice(deviceIndex);
  s_mbClient.setTarget(config.ip, 502);

  // Function code: Coils (FC01) o Discrete Inputs (FC02)
  uint8_t fc = discreteInputs ? READ_DISCR_INPUT : READ_COIL;

  // Lectura síncrona bloqueante (bloquea esta tarea hasta respuesta o timeout)
  uint32_t tok = ++token_;
  ModbusMessage response = s_mbClient.syncRequest(
    tok,
    config.unitId,
    fc,
    startAddress,
    count
  );

  // Verificar error
  Error err = response.getError();
  if (err != SUCCESS) {
    ModbusError me(err);
    LOGE("readBooleans[%u] %s addr=%u count=%u: error %02X - %s\n",
         deviceIndex, config.modbusModelName, startAddress, count,
         (int)err, (const char *)me);
    xSemaphoreGive(mutex_);
    return false;
  }

  // Respuesta FC01/FC02: serverID, FC, byteCount, data... (1 bit por coil, LSB primero)
  uint8_t byteCount = 0;
  response.get(2, byteCount);

  // Liberar mutex después de la lectura (response es copia local)
  xSemaphoreGive(mutex_);

  // Desempaquetar bits: el bit i está en el byte i/8, posición i%8
  states.reserve(count);
  for (uint16_t i = 0; i < count; i++) {
    uint8_t byteIndex = i / 8;
    if (byteIndex >= byteCount) {
      break;  // El esclavo devolvió menos datos de los solicitados
    }
    uint8_t dataByte = 0;
    response.get(3 + byteIndex, dataByte);
    states.push_back((dataByte >> (i % 8)) & 0x01);
  }

  LOGD("readBooleans[%u] read %u bits (%s) from %s\n",
       deviceIndex, states.size(),
       discreteInputs ? "discrete inputs" : "coils", config.modbusModelName);

  return true;
}

bool ModbusManager::writeRegister(size_t deviceIndex, uint16_t address, const char* value) {
  if (deviceIndex >= EepromManager::instance().getModbusDeviceCount()) {
    LOGE("writeRegister: invalid device index %u\n", deviceIndex);
    return false;
  }

  if (!value) {
    LOGE("writeRegister: null value\n");
    return false;
  }

  uint16_t regValue = (uint16_t)strtoul(value, nullptr, 16);

  if (!NetworkManager::instance().isConnected()) {
    return false;
  }

  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(15000)) != pdTRUE) {
    LOGE("writeRegister: mutex timeout\n");
    return false;
  }

  const auto &config = EepromManager::instance().getModbusDevice(deviceIndex);
  s_mbClient.setTarget(config.ip, 502);

  uint32_t tok = ++token_;
  ModbusMessage response = s_mbClient.syncRequest(
    tok,
    config.unitId,
    WRITE_HOLD_REGISTER,
    address,
    regValue
  );

  xSemaphoreGive(mutex_);

  Error err = response.getError();
  if (err != SUCCESS) {
    ModbusError me(err);
    LOGE("writeRegister[%u] %s addr=%u val=%s: error %02X - %s\n",
         deviceIndex, config.modbusModelName, address, value,
         (int)err, (const char *)me);
    return false;
  }

  LOGI("writeRegister[%u] %s addr=%u val=%s OK\n",
       deviceIndex, config.modbusModelName, address, value);
  return true;
}

bool ModbusManager::writeCoil(size_t deviceIndex, uint16_t address, const char* value) {
  if (deviceIndex >= EepromManager::instance().getModbusDeviceCount()) {
    LOGE("writeCoil: invalid device index %u\n", deviceIndex);
    return false;
  }

  if (!value) {
    LOGE("writeCoil: null value\n");
    return false;
  }

  // Interpretar valor: "1" o "FF00" = ON (0xFF00), cualquier otra cosa = OFF (0x0000)
  uint16_t coilValue;
  if (strcmp(value, "1") == 0 || strcasecmp(value, "FF00") == 0) {
    coilValue = 0xFF00;
  } else {
    coilValue = 0x0000;
  }

  if (!NetworkManager::instance().isConnected()) {
    return false;
  }

  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(15000)) != pdTRUE) {
    LOGE("writeCoil: mutex timeout\n");
    return false;
  }

  const auto &config = EepromManager::instance().getModbusDevice(deviceIndex);
  s_mbClient.setTarget(config.ip, 502);

  uint32_t tok = ++token_;
  ModbusMessage response = s_mbClient.syncRequest(
    tok,
    config.unitId,
    WRITE_COIL,
    address,
    coilValue
  );

  xSemaphoreGive(mutex_);

  Error err = response.getError();
  if (err != SUCCESS) {
    ModbusError me(err);
    LOGE("writeCoil[%u] %s addr=%u val=%s: error %02X - %s\n",
         deviceIndex, config.modbusModelName, address, value,
         (int)err, (const char *)me);
    return false;
  }

  LOGI("writeCoil[%u] %s addr=%u val=%s (0x%04X) OK\n",
       deviceIndex, config.modbusModelName, address, value, coilValue);
  return true;
}
