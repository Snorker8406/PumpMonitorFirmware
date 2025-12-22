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
    LOGI("Modbus connecting to %s...\n", deviceIp.toString().c_str());
    client_.connect(deviceIp);
    vTaskDelay(pdMS_TO_TICKS(100));
    return false;
  }
  return true;
}

bool ModbusManager::readDevice(IPAddress deviceIp, uint16_t startReg, uint16_t totalRegs, std::vector<float> &values) {
  values.clear();

  if (!ensureConnected(deviceIp)) {
    return false;
  }

  std::vector<uint16_t> regsBuffer(totalRegs);
  uint16_t offset = 0;

  // Leer en chunks para evitar timeouts
  while (offset < totalRegs) {
    const uint16_t regsToRead = std::min(static_cast<uint16_t>(kModbusChunkSize), static_cast<uint16_t>(totalRegs - offset));
    
    if (!client_.readHreg(deviceIp, startReg + offset, &regsBuffer[offset], regsToRead)) {
      LOGW("Modbus read failed at offset %u\n", offset);
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

  LOGI("Modbus read %u floats from %s\n", values.size(), deviceIp.toString().c_str());
  return true;
}

float ModbusManager::regsToFloat(uint16_t reg1, uint16_t reg2) {
  union {
    uint32_t i;
    float f;
  } converter;
  converter.i = (static_cast<uint32_t>(reg1) << 16) | reg2;
  return converter.f;
}
