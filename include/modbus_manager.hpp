#pragma once

#include <ModbusIP_ESP8266.h>
#include <vector>

#include "app_config.hpp"

class ModbusManager {
 public:
  static ModbusManager &instance();

  void begin();
  void loop();
  bool readDevice(IPAddress deviceIp, uint16_t startReg, uint16_t totalRegs, std::vector<float> &values);

 private:
  ModbusManager() = default;
  bool ensureConnected(IPAddress deviceIp);
  static float regsToFloat(uint16_t reg1, uint16_t reg2);

  ModbusIP client_;
  unsigned long lastReadMs_ = 0;
};
