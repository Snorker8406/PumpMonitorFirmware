#pragma once

#include <SD.h>
#include <SPI.h>
#include <FS.h>
#include <vector>

#include "app_config.hpp"

struct SensorDataRecord {
  unsigned long timestamp;
  const char* modbusModelId;  // Identificador del modelo Modbus
  IPAddress deviceIp;
  std::vector<float> values;
  std::vector<uint16_t> rawData;  // Datos hexadecimales crudos
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
  
  // Logging de errores
  bool writeErrorLog(const char* level, const char* message);
  bool writeErrorLogFormatted(const char* level, const char* fmt, ...);
  
  // Información de la tarjeta
  uint64_t getTotalBytes() const;
  uint64_t getUsedBytes() const;
  uint64_t getFreeBytes() const;
  sdcard_type_t getCardType() const;

 private:
  SdManager() = default;
  bool ensureDirectoryExists(const char* path);
  void generateFilename(char* buffer, size_t bufferSize) const;
  void generateErrorFilename(char* buffer, size_t bufferSize) const;
  void getTimestamp(char* buffer, size_t bufferSize) const;
  
  bool initialized_ = false;
  SPIClass spi_;
};
