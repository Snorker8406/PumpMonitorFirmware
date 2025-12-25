#include "modbus_manager.hpp"

#include "log.hpp"
#include "network_manager.hpp"

ModbusManager &ModbusManager::instance() {
  static ModbusManager inst;
  return inst;
}

void ModbusManager::begin() {
  client_.client();
  LOGI("Modbus client initialized\n");
}

void ModbusManager::loop() {
  client_.task();
}

bool ModbusManager::ensureConnected(IPAddress deviceIp) {
  if (!NetworkManager::instance().isConnected()) {
    return false;
  }

  if (!client_.isConnected(deviceIp)) {
    LOGI("Modbus connecting to %u.%u.%u.%u...\n", deviceIp[0], deviceIp[1], deviceIp[2], deviceIp[3]);
    client_.connect(deviceIp);
    vTaskDelay(pdMS_TO_TICKS(100));
    return false;
  }
  return true;
}

bool ModbusManager::readDevice(IPAddress deviceIp, uint8_t unitId, uint16_t startReg, uint16_t totalRegs, 
                               ModbusRegisterType regType, std::vector<float> &values) {
  values.clear();
  values.reserve(totalRegs / 2);  // Pre-reserve para evitar realocaciones

  if (!ensureConnected(deviceIp)) {
    return false;
  }

  std::vector<uint16_t> regsBuffer(totalRegs);
  uint16_t offset = 0;

  // Leer en chunks para evitar timeouts
  while (offset < totalRegs) {
    const uint16_t regsToRead = std::min(static_cast<uint16_t>(kModbusChunkSize), static_cast<uint16_t>(totalRegs - offset));
    
    bool success = false;
    if (regType == ModbusRegisterType::HOLDING_REGISTER) {
      success = client_.readHreg(deviceIp, startReg + offset, &regsBuffer[offset], regsToRead, nullptr, unitId);
    } else {
      success = client_.readIreg(deviceIp, startReg + offset, &regsBuffer[offset], regsToRead, nullptr, unitId);
    }
    
    if (!success) {
      LOGW("Modbus read failed at offset %u (unitId=%u, regType=%s)\n", 
           offset, unitId, regType == ModbusRegisterType::HOLDING_REGISTER ? "HOLD" : "INPUT");
      return false;
    }

    vTaskDelay(pdMS_TO_TICKS(kModbusChunkDelayMs));
    client_.task();
    offset += regsToRead;
  }

  // Convertir pares de registros a floats
  for (uint16_t i = 0; i < totalRegs; i += 2) {
    if (i + 1 < totalRegs) {
      float value = regsToFloat(regsBuffer[i], regsBuffer[i + 1]);
      values.push_back(value);
    }
  }

//   LOGI("Modbus read %u floats from %s\n", values.size(), deviceIp.toString().c_str());
  return true;
}

bool ModbusManager::readAllDevices(std::vector<ModbusDeviceData> &devicesData) {
  devicesData.clear();
  devicesData.reserve(kModbusDeviceCount);  // Pre-reserve para evitar realocaciones
  bool allSuccess = true;

  for (size_t i = 0; i < kModbusDeviceCount; i++) {
    const auto &config = kModbusDevices[i];
    ModbusDeviceData data;
    data.name = config.name;
    data.ip = config.ip;
    data.success = readDevice(config.ip, config.unitId, config.startReg, config.totalRegs, 
                              config.regType, data.values);
    
    if (!data.success) {
      allSuccess = false;
      LOGW("Failed to read %s (%u.%u.%u.%u)\n", config.name, config.ip[0], config.ip[1], config.ip[2], config.ip[3]);
    }
    
    devicesData.push_back(data);
    
    // Delay entre dispositivos para no saturar la red
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
