#include "modbus_manager.hpp"

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
  if (deviceIndex >= kModbusDeviceCount) {
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

  const auto &config = kModbusDevices[deviceIndex];

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
  devicesData.reserve(kModbusDeviceCount);
  bool allSuccess = true;

  for (size_t i = 0; i < kModbusDeviceCount; i++) {
    const auto &config = kModbusDevices[i];
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
    if (i < kModbusDeviceCount - 1) {
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

bool ModbusManager::writeRegister(size_t deviceIndex, uint16_t address, const char* value) {
  if (deviceIndex >= kModbusDeviceCount) {
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

  const auto &config = kModbusDevices[deviceIndex];
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
  if (deviceIndex >= kModbusDeviceCount) {
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

  const auto &config = kModbusDevices[deviceIndex];
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
