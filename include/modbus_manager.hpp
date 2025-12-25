#pragma once

#include <ModbusIP_ESP8266.h>
#include <vector>

#include "app_config.hpp"

struct ModbusDeviceData {
  const char* name;
  IPAddress ip;
  std::vector<float> values;
  bool success;
};

class ModbusManager {
 public:
  static ModbusManager &instance();

  void begin();
  void loop();
  bool readDevice(IPAddress deviceIp, uint8_t unitId, uint16_t startReg, uint16_t totalRegs, 
                  ModbusRegisterType regType, std::vector<float> &values);
  bool readAllDevices(std::vector<ModbusDeviceData> &devicesData);

 private:
  ModbusManager() = default;
  bool ensureConnected(IPAddress deviceIp);
  static float regsToFloat(uint16_t reg1, uint16_t reg2);

  ModbusIP client_;
  unsigned long lastReadMs_ = 0;
};
