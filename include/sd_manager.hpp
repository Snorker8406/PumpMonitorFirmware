#pragma once

#include <SD.h>
#include <SPI.h>
#include <FS.h>
#include <vector>

#include "app_config.hpp"

struct SensorDataRecord {
  unsigned long timestamp;
  const char* deviceName;
  IPAddress deviceIp;
  std::vector<float> values;
};

class SdManager {
 public:
  static SdManager &instance();

  bool begin();
  void end();
  bool isAvailable() const;
  
  // Operaciones de escritura
  bool writeDataRecord(const SensorDataRecord &record);
  bool writeDataBatch(const std::vector<SensorDataRecord> &records);
  
  // Información de la tarjeta
  uint64_t getTotalBytes() const;
  uint64_t getUsedBytes() const;
  uint64_t getFreeBytes() const;
  sdcard_type_t getCardType() const;

 private:
  SdManager() = default;
  bool ensureDirectoryExists(const char* path);
  String generateFilename() const;
  
  bool initialized_ = false;
  SPIClass spi_;
};
