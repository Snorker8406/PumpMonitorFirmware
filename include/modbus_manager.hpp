#pragma once

#include <ModbusClientTCP.h>
#include <vector>

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

  // Lectura síncrona bloqueante (syncRequest)
  // Thread-safe: múltiples tareas pueden llamar concurrentemente
  bool readDevice(size_t deviceIndex, std::vector<float> &values, std::vector<uint16_t> *rawData = nullptr);
  bool readAllDevices(std::vector<ModbusDeviceData> &devicesData);

  // Escritura síncrona de un solo Holding Register (FC06)
  bool writeRegister(size_t deviceIndex, uint16_t address, const char* value);

  // Escritura síncrona de un solo Coil (FC05)
  // value: "1"/"FF00" = ON, "0"/"0000" = OFF
  bool writeCoil(size_t deviceIndex, uint16_t address, const char* value);

 private:
  ModbusManager() = default;
  static float regsToFloat(uint16_t reg1, uint16_t reg2);

  // Mutex para serializar acceso al cliente compartido
  SemaphoreHandle_t mutex_{nullptr};
  uint32_t token_{0};
};
