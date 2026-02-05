#include "modbus_manager.hpp"

#include "log.hpp"
#include "network_manager.hpp"

// Puntero global para callbacks (eModbus usa callbacks estáticos)
static ModbusManager* gModbusInstance = nullptr;

ModbusManager &ModbusManager::instance() {
  static ModbusManager inst;
  gModbusInstance = &inst;
  return inst;
}

void ModbusManager::begin() {
  // Crear mutex interno para serializar lecturas
  internalMutex_ = xSemaphoreCreateMutex();
  if (!internalMutex_) {
    LOGE("Failed to create Modbus internal mutex\n");
  }
  
  // Crear e inicializar un cliente Modbus TCP por cada dispositivo
  for (size_t i = 0; i < kModbusDeviceCount; i++) {
    modbusClients_[i] = new ModbusClientTCP(tcpClients_[i]);
    
    // Configurar timeouts (en ms) - 5s timeout, 200ms intervalo
    modbusClients_[i]->setTimeout(5000, 200);
    
    // Registrar callbacks usando lambdas que llaman a los métodos de instancia
    modbusClients_[i]->onDataHandler([](ModbusMessage response, uint32_t token) {
      if (gModbusInstance) {
        gModbusInstance->handleData(response, token);
      }
    });
    
    modbusClients_[i]->onErrorHandler([](Error error, uint32_t token) {
      if (gModbusInstance) {
        gModbusInstance->handleError(error, token);
      }
    });
    
    // Iniciar el cliente (crea la tarea interna)
    modbusClients_[i]->begin();
    
    initialized_[i] = false;
  }
  
  LOGI("eModbus initialized with %u device clients\n", kModbusDeviceCount);
}

void ModbusManager::loop() {
  // eModbus no requiere loop() - es completamente asíncrono
}

void ModbusManager::handleData(ModbusMessage response, uint32_t token) {
  // Token contiene el índice del dispositivo
  size_t deviceIndex = token;
  
  if (response.getError() == SUCCESS) {
    // Extraer datos de la respuesta
    // El primer byte es la longitud en bytes, los datos empiezan en índice 3
    size_t numBytes = response[2];  // Byte count
    size_t numRegs = numBytes / 2;
    
    // Copiar registros al buffer
    for (size_t i = 0; i < numRegs && i < kMaxRegisters; i++) {
      // Los registros están en big-endian en la respuesta Modbus
      readBuffer_[i] = (response[3 + i*2] << 8) | response[3 + i*2 + 1];
    }
    
    readLength_ = numRegs;
    readSuccess_ = true;
  } else {
    LOGE("Modbus[%u] response error: %02X\n", deviceIndex, response.getError());
    readSuccess_ = false;
  }
  
  readComplete_ = true;
}

void ModbusManager::handleError(Error error, uint32_t token) {
  size_t deviceIndex = token;
  LOGE("Modbus[%u] error: %02X\n", deviceIndex, static_cast<int>(error));
  readSuccess_ = false;
  readComplete_ = true;
}

bool ModbusManager::readDevice(size_t deviceIndex, std::vector<float> &values, std::vector<uint16_t> *rawData) {
  if (deviceIndex >= kModbusDeviceCount) {
    return false;
  }
  
  if (!NetworkManager::instance().isConnected()) {
    return false;
  }
  
  // Adquirir mutex interno para serializar acceso a estado compartido
  if (xSemaphoreTake(internalMutex_, pdMS_TO_TICKS(10000)) != pdTRUE) {
    LOGE("Modbus[%u] internal mutex timeout\n", deviceIndex);
    return false;
  }
  
  const auto &config = kModbusDevices[deviceIndex];
  ModbusClientTCP* client = modbusClients_[deviceIndex];
  
  values.clear();
  values.reserve(config.totalRegs / 2);
  
  if (rawData) {
    rawData->clear();
    rawData->reserve(config.totalRegs);
  }
  
  // Conectar si no está conectado
  if (!initialized_[deviceIndex]) {
    LOGI("Modbus[%u] connecting to %s (%u.%u.%u.%u)...\n", 
         deviceIndex, config.modbusModelName,
         config.ip[0], config.ip[1], config.ip[2], config.ip[3]);
    
    // Conectar el cliente TCP primero
    if (!tcpClients_[deviceIndex].connect(config.ip, 502)) {
      LOGE("Modbus[%u] TCP connection failed\n", deviceIndex);
      xSemaphoreGive(internalMutex_);
      return false;
    }
    
    // Configurar el target del cliente Modbus
    client->setTarget(config.ip, 502);
    initialized_[deviceIndex] = true;
    
    vTaskDelay(pdMS_TO_TICKS(100));  // Dar tiempo a estabilizar conexión
  }
  
  // Verificar que el TCP sigue conectado
  if (!tcpClients_[deviceIndex].connected()) {
    LOGW("Modbus[%u] TCP disconnected, reconnecting...\n", deviceIndex);
    initialized_[deviceIndex] = false;
    
    if (!tcpClients_[deviceIndex].connect(config.ip, 502)) {
      LOGE("Modbus[%u] TCP reconnection failed\n", deviceIndex);
      xSemaphoreGive(internalMutex_);
      return false;
    }
    
    client->setTarget(config.ip, 502);
    initialized_[deviceIndex] = true;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  
  // Buffer temporal para acumular todos los registros
  std::vector<uint16_t> allRegs;
  allRegs.reserve(config.totalRegs);
  
  uint16_t offset = 0;
  bool success = true;
  
  // Leer en chunks
  while (offset < config.totalRegs) {
    const uint16_t regsToRead = std::min(static_cast<uint16_t>(kModbusChunkSize), 
                                          static_cast<uint16_t>(config.totalRegs - offset));
    
    // Resetear estado
    readComplete_ = false;
    readSuccess_ = false;
    readLength_ = 0;
    memset(readBuffer_, 0, sizeof(readBuffer_));
    
    // Enviar request según tipo de registro
    Error err;
    if (config.regType == ModbusRegisterType::HOLDING_REGISTER) {
      err = client->addRequest(deviceIndex, config.unitId, READ_HOLD_REGISTER, 
                               config.startReg + offset, regsToRead);
    } else {
      err = client->addRequest(deviceIndex, config.unitId, READ_INPUT_REGISTER, 
                               config.startReg + offset, regsToRead);
    }
    
    if (err != SUCCESS) {
      LOGE("Modbus[%u] request error at offset %u: %02X\n", deviceIndex, offset, static_cast<int>(err));
      success = false;
      break;
    }
    
    // Esperar respuesta con timeout (5 segundos)
    unsigned long startMs = millis();
    while (!readComplete_ && (millis() - startMs) < 5000) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    if (!readComplete_) {
      LOGE("Modbus[%u] timeout at offset %u\n", deviceIndex, offset);
      // Forzar reconexión para limpiar estado
      tcpClients_[deviceIndex].stop();
      initialized_[deviceIndex] = false;
      success = false;
      break;
    }
    
    if (!readSuccess_) {
      LOGE("Modbus[%u] read failed at offset %u\n", deviceIndex, offset);
      // Forzar reconexión para limpiar estado
      tcpClients_[deviceIndex].stop();
      initialized_[deviceIndex] = false;
      success = false;
      break;
    }
    
    // Agregar registros leídos al buffer total
    for (size_t i = 0; i < readLength_; i++) {
      allRegs.push_back(readBuffer_[i]);
    }
    
    offset += regsToRead;
    
    // Delay entre chunks
    if (offset < config.totalRegs) {
      vTaskDelay(pdMS_TO_TICKS(kModbusChunkDelayMs));
    }
  }
  
  // Liberar mutex antes de retornar
  xSemaphoreGive(internalMutex_);
  
  if (!success) {
    return false;
  }
  
  // Guardar datos raw si se solicitan
  if (rawData) {
    *rawData = allRegs;
  }
  
  // Convertir pares de registros a floats
  for (size_t i = 0; i + 1 < allRegs.size(); i += 2) {
    float value = regsToFloat(allRegs[i], allRegs[i + 1]);
    values.push_back(value);
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
    
    // Delay entre dispositivos
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
