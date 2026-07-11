#pragma once

#include <SD.h>
#include <SPI.h>
#include <FS.h>
#include <vector>

#include "app_config.hpp"

struct SensorDataRecord {
  unsigned long timestamp;
  uint8_t modbusSlaveId;          // ID numérico del esclavo Modbus
  const char* modbusSlaveName;    // Nombre descriptivo del esclavo Modbus
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

  // Guardar un registro de alarmas (coils) en el archivo diario.
  // Formato: {timestamp},{modbusSlaveId},{coilsTypes},{CoilsValues}
  bool writeAlarmRecord(unsigned long timestamp, uint8_t modbusSlaveId,
                        const char* coilsTypes, const std::vector<bool> &states);

  // Guardar un evento recibido por el servidor Modbus en el archivo diario.
  // eventType: "confirmation", "alarm" o "notification".
  // Formato: {timestamp},{device},{eventType},{value}
  bool writeServerEventRecord(unsigned long timestamp, const char* device,
                              const char* eventType, uint16_t value);
  
  // Logging de errores
  bool writeErrorLog(const char* level, const char* message);
  bool writeErrorLogFormatted(const char* level, const char* fmt, ...);
  
  // Información de la tarjeta
  uint64_t getTotalBytes() const;
  uint64_t getUsedBytes() const;
  uint64_t getFreeBytes() const;
  sdcard_type_t getCardType() const;
  
  // Listar archivos de un directorio
  String listFiles(int year, int month);
  
  // Obtener ruta de archivo de backup
  String getBackupFilePath(int year, int month, int day);
  
  // Subir archivo de backup al servidor
  bool uploadBackupFile(int year, int month, int day, int32_t deviceId);

 private:
  SdManager() = default;
  bool ensureDirectoryExists(const char* path);
  bool ensureNestedDirectories(int year, int month);
  void generateFilename(char* buffer, size_t bufferSize);
  void generateErrorFilename(char* buffer, size_t bufferSize) const;
  void getTimestamp(char* buffer, size_t bufferSize) const;
  
  bool initialized_ = false;
  SPIClass spi_;
};
