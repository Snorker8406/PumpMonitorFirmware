#pragma once

#include <ModbusClientTCP.h>
#include <vector>
#include <array>
#include <functional>

#include "app_config.hpp"

struct ModbusDeviceData {
  uint8_t modbusModelId;          // ID numérico del modelo Modbus
  const char* modbusModelName;    // Nombre descriptivo del modelo Modbus
  IPAddress ip;
  std::vector<float> values;
  std::vector<uint16_t> rawData;  // Datos crudos en hexadecimal
  bool success;
};

class ModbusManager {
 public:
  static ModbusManager &instance();

  void begin();
  void loop();
  
  // Lectura síncrona bloqueante (espera respuesta con timeout)
  // NOTA: Thread-safe - usa mutex interno
  bool readDevice(size_t deviceIndex, std::vector<float> &values, std::vector<uint16_t> *rawData = nullptr);
  bool readAllDevices(std::vector<ModbusDeviceData> &devicesData);

 private:
  ModbusManager() = default;
  static float regsToFloat(uint16_t reg1, uint16_t reg2);
  
  // Un cliente TCP por dispositivo para conexiones persistentes
  std::array<WiFiClient, kModbusDeviceCount> tcpClients_;
  std::array<ModbusClientTCP*, kModbusDeviceCount> modbusClients_;
  std::array<bool, kModbusDeviceCount> initialized_{};
  
  // Mutex interno para serializar acceso
  SemaphoreHandle_t internalMutex_{nullptr};
  
  // Buffer estático compartido para lecturas (máximo 120 registros)
  static constexpr size_t kMaxRegisters = 120;
  uint16_t readBuffer_[kMaxRegisters];
  
  // Estado de última lectura asíncrona (protegido por mutex)
  volatile bool readComplete_{false};
  volatile bool readSuccess_{false};
  volatile size_t readLength_{0};
  
  // Callback para respuestas
  void handleData(ModbusMessage response, uint32_t token);
  void handleError(Error error, uint32_t token);
};
