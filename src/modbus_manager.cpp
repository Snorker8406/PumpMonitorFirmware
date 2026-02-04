#include "modbus_manager.hpp"

#include "log.hpp"
#include "network_manager.hpp"

namespace {
  // Contador de reintentos por IP para evitar reconexiones muy frecuentes
  constexpr uint32_t kReconnectCooldownMs = 5000;
  constexpr uint8_t kMaxRetries = 3;
}

ModbusManager &ModbusManager::instance() {
  static ModbusManager inst;
  return inst;
}

void ModbusManager::begin() {
  client_.client();
  LOGI("Modbus client initialized (legacy-style persistent connection)\n");
}

void ModbusManager::loop() {
  client_.task();
}

bool ModbusManager::ensureConnected(IPAddress deviceIp) {
  if (!NetworkManager::instance().isConnected()) {
    return false;
  }

  if (client_.isConnected(deviceIp)) {
    return true;
  }
  
  // Verificar cooldown para evitar reconexiones muy frecuentes
  uint32_t ipKey = (uint32_t)deviceIp;
  uint32_t now = millis();
  
  auto it = lastConnectAttempt_.find(ipKey);
  if (it != lastConnectAttempt_.end()) {
    if ((now - it->second) < kReconnectCooldownMs) {
      return false;  // Muy pronto para reintentar
    }
  }
  
  lastConnectAttempt_[ipKey] = now;
  LOGI("Modbus connecting to %u.%u.%u.%u...\n", deviceIp[0], deviceIp[1], deviceIp[2], deviceIp[3]);
  
  if (!client_.connect(deviceIp)) {
    LOGE("Modbus connection failed to %u.%u.%u.%u\n", deviceIp[0], deviceIp[1], deviceIp[2], deviceIp[3]);
    return false;
  }
  
  // Esperar a que se establezca la conexión
  vTaskDelay(pdMS_TO_TICKS(200));
  client_.task();
  
  if (client_.isConnected(deviceIp)) {
    LOGI("Modbus connected to %u.%u.%u.%u\n", deviceIp[0], deviceIp[1], deviceIp[2], deviceIp[3]);
    return true;
  }
  
  return false;
}

bool ModbusManager::readDevice(IPAddress deviceIp, uint8_t unitId, uint16_t startReg, uint16_t totalRegs, 
                               ModbusRegisterType regType, std::vector<float> &values, std::vector<uint16_t> *rawData) {
  values.clear();
  values.reserve(totalRegs / 2);
  
  if (rawData) {
    rawData->clear();
    rawData->reserve(totalRegs);
  }

  if (!ensureConnected(deviceIp)) {
    return false;
  }

  // Buffer para registros - inicializado a 0xFFFF para detectar si se leyeron
  std::vector<uint16_t> regsBuffer(totalRegs, 0xFFFF);
  
  // Estilo Legacy: encolar todas las lecturas primero
  uint16_t offset = 0;
  while (offset < totalRegs) {
    const uint16_t regsToRead = std::min(static_cast<uint16_t>(kModbusChunkSize), static_cast<uint16_t>(totalRegs - offset));
    
    if (regType == ModbusRegisterType::HOLDING_REGISTER) {
      client_.readHreg(deviceIp, startReg + offset, &regsBuffer[offset], regsToRead, nullptr, unitId);
    } else {
      client_.readIreg(deviceIp, startReg + offset, &regsBuffer[offset], regsToRead, nullptr, unitId);
    }
    
    offset += regsToRead;
  }
  
  // Estilo Legacy: procesar transacciones pendientes con múltiples llamadas a task()
  for (int i = 0; i < 10; i++) {  // Dar tiempo suficiente para recibir respuestas
    client_.task();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  
  // Verificar si se recibieron datos válidos (no todos 0 o 0xFFFF)
  bool hasValidData = false;
  bool allZeros = true;
  
  for (const auto& val : regsBuffer) {
    if (val != 0xFFFF) {
      hasValidData = true;
    }
    if (val != 0) {
      allZeros = false;
    }
  }
  
  if (!hasValidData) {
    LOGW("Modbus: No response from %u.%u.%u.%u (all 0xFFFF)\n", 
         deviceIp[0], deviceIp[1], deviceIp[2], deviceIp[3]);
    // Forzar reconexión en el próximo ciclo
    lastConnectAttempt_.erase((uint32_t)deviceIp);
    return false;
  }
  
  if (allZeros) {
    LOGW("Modbus: All zeros from %u.%u.%u.%u - may be stale connection\n", 
         deviceIp[0], deviceIp[1], deviceIp[2], deviceIp[3]);
    // Incrementar contador de ceros consecutivos
    uint32_t ipKey = (uint32_t)deviceIp;
    consecutiveZeros_[ipKey]++;
    
    if (consecutiveZeros_[ipKey] >= kMaxRetries) {
      LOGW("Modbus: Too many zero readings, forcing reconnect\n");
      client_.disconnect(deviceIp);
      lastConnectAttempt_.erase(ipKey);
      consecutiveZeros_[ipKey] = 0;
      return false;
    }
  } else {
    // Reset contador si hay datos válidos
    consecutiveZeros_[(uint32_t)deviceIp] = 0;
  }

  // Guardar datos raw si se solicitan
  if (rawData) {
    for (uint16_t i = 0; i < totalRegs; i++) {
      rawData->push_back(regsBuffer[i]);
    }
  }
  
  // Convertir pares de registros a floats
  for (uint16_t i = 0; i < totalRegs; i += 2) {
    if (i + 1 < totalRegs) {
      float value = regsToFloat(regsBuffer[i], regsBuffer[i + 1]);
      values.push_back(value);
    }
  }

  return true;
}

bool ModbusManager::readAllDevices(std::vector<ModbusDeviceData> &devicesData) {
  devicesData.clear();
  devicesData.reserve(kModbusDeviceCount);  // Pre-reserve para evitar realocaciones
  bool allSuccess = true;

  for (size_t i = 0; i < kModbusDeviceCount; i++) {
    const auto &config = kModbusDevices[i];
    ModbusDeviceData data;
    data.modbusModelId = config.modbusModelId;
    data.modbusModelName = config.modbusModelName;
    data.ip = config.ip;
    data.success = readDevice(config.ip, config.unitId, config.startReg, config.totalRegs, 
                              config.regType, data.values, &data.rawData);
    
    if (!data.success) {
      allSuccess = false;
      LOGE("Failed to read %s (%u.%u.%u.%u)\n", config.modbusModelName, config.ip[0], config.ip[1], config.ip[2], config.ip[3]);
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
