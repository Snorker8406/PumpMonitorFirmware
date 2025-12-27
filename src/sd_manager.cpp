#include "sd_manager.hpp"

#include <time.h>
#include "log.hpp"
#include "rtc_manager.hpp"

SdManager &SdManager::instance() {
  static SdManager inst;
  return inst;
}

bool SdManager::begin() {
  if (initialized_) {
    LOGW("SD already initialized\n");
    return true;
  }

  // Configurar SPI con los pines específicos
  spi_.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
  
  LOGI("SD initializing (CS=%d, MOSI=%d, CLK=%d, MISO=%d)...\n", 
       kSdCsPin, kSdMosiPin, kSdSckPin, kSdMisoPin);
  
  if (!SD.begin(kSdCsPin, spi_, kSdSpiFrequency)) {
    LOGE("SD initialization failed\n");
    return false;
  }

  sdcard_type_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    LOGE("No SD card detected\n");
    return false;
  }

  initialized_ = true;
  
  // Log información de la tarjeta
  const char* typeStr = "UNKNOWN";
  if (cardType == CARD_MMC) typeStr = "MMC";
  else if (cardType == CARD_SD) typeStr = "SDSC";
  else if (cardType == CARD_SDHC) typeStr = "SDHC";

  LOGI("SD initialized | Type: %s | Size: %llu MB | Free: %llu MB\n",
       typeStr, 
       getTotalBytes() / (1024 * 1024),
       getFreeBytes() / (1024 * 1024));

  // Crear directorio de datos si no existe
  if (!ensureDirectoryExists(kSdDataPath)) {
    LOGW("Could not create data directory\n");
  }

  return true;
}

void SdManager::end() {
  if (initialized_) {
    SD.end();
    initialized_ = false;
    LOGI("SD deinitialized\n");
  }
}

bool SdManager::isAvailable() const {
  return initialized_;
}

bool SdManager::ensureDirectoryExists(const char* path) {
  if (!initialized_) return false;
  
  if (SD.exists(path)) {
    return true;
  }
  
  return SD.mkdir(path);
}

void SdManager::generateFilename(char* buffer, size_t bufferSize) const {
  // Formato: /data/YYYYMMDD.csv usando RTC
  auto &rtc = RtcManager::instance();
  
  if (!rtc.isAvailable()) {
    // Fallback a tiempo del sistema si RTC no está disponible
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    snprintf(buffer, bufferSize, "%s/%04d%02d%02d.csv",
             kSdDataPath,
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday);
    return;
  }
  
  // Usar fecha del RTC
  DateTime now = rtc.now();
  snprintf(buffer, bufferSize, "%s/%04d%02d%02d.csv",
           kSdDataPath,
           now.year(),
           now.month(),
           now.day());
}

bool SdManager::writeDataRecord(const SensorDataRecord &record) {
  if (!initialized_) {
    return false;
  }

  char filename[32];
  generateFilename(filename, sizeof(filename));
  bool fileExists = SD.exists(filename);
  
  File file = SD.open(filename, FILE_APPEND);
  if (!file) {
    LOGE("Failed to open file for writing\n");
    return false;
  }

  // Escribir header si el archivo es nuevo
  if (!fileExists) {
    file.println("timestamp,device_name,device_ip,values");
  }

  // Escribir datos: timestamp,nombre,ip,"val1,val2,val3,..."
  file.printf("%lu,%s,%u.%u.%u.%u,\"",
              record.timestamp,
              record.deviceName,
              record.deviceIp[0],
              record.deviceIp[1],
              record.deviceIp[2],
              record.deviceIp[3]);

  for (size_t i = 0; i < record.values.size(); i++) {
    file.printf("%.2f", record.values[i]);
    if (i < record.values.size() - 1) {
      file.print(",");
    }
  }
  file.println("\"");

  file.close();
  return true;
}

bool SdManager::writeDataBatch(const std::vector<SensorDataRecord> &records) {
  if (!initialized_ || records.empty()) {
    return false;
  }

  char filename[32];
  generateFilename(filename, sizeof(filename));
  bool fileExists = SD.exists(filename);
  
  File file = SD.open(filename, FILE_APPEND);
  if (!file) {
    LOGE("Failed to open file for batch writing\n");
    return false;
  }

  // Escribir header si el archivo es nuevo
  if (!fileExists) {
    file.println("timestamp,device_name,device_ip,values");
  }

  // Escribir todos los registros
  for (const auto &record : records) {
    file.printf("%lu,%s,%u.%u.%u.%u,\"",
                record.timestamp,
                record.deviceName,
                record.deviceIp[0],
                record.deviceIp[1],
                record.deviceIp[2],
                record.deviceIp[3]);

    for (size_t i = 0; i < record.values.size(); i++) {
      file.printf("%.2f", record.values[i]);
      if (i < record.values.size() - 1) {
        file.print(",");
      }
    }
    file.println("\"");
  }

  file.close();
  LOGI("Batch written: %u records\n", records.size());
  return true;
}

uint64_t SdManager::getTotalBytes() const {
  return initialized_ ? SD.totalBytes() : 0;
}

uint64_t SdManager::getUsedBytes() const {
  return initialized_ ? SD.usedBytes() : 0;
}

uint64_t SdManager::getFreeBytes() const {
  if (!initialized_) return 0;
  return getTotalBytes() - getUsedBytes();
}

sdcard_type_t SdManager::getCardType() const {
  return initialized_ ? SD.cardType() : CARD_NONE;
}
