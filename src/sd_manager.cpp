#include "sd_manager.hpp"

#include <time.h>
#include <stdarg.h>
#include <WiFiClientSecure.h>
#include "log.hpp"
#include "rtc_manager.hpp"

namespace {
  // Cliente WiFi estático para upload (evita problemas de memoria del heap)
  WiFiClientSecure uploadClient;
}

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
  
  // Crear directorio de errores si no existe
  if (!ensureDirectoryExists(kSdErrorPath)) {
    LOGW("Could not create error directory\n");
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

bool SdManager::ensureNestedDirectories(int year, int month) {
  if (!initialized_) return false;
  
  // Crear carpeta del año: /data/YYYY
  char yearPath[20];
  snprintf(yearPath, sizeof(yearPath), "%s/%04d", kSdDataPath, year);
  if (!SD.exists(yearPath)) {
    if (!SD.mkdir(yearPath)) {
      LOGE("Failed to create year directory: %s\n", yearPath);
      return false;
    }
  }
  
  // Crear carpeta del mes: /data/YYYY/MM
  char monthPath[24];
  snprintf(monthPath, sizeof(monthPath), "%s/%02d", yearPath, month);
  if (!SD.exists(monthPath)) {
    if (!SD.mkdir(monthPath)) {
      LOGE("Failed to create month directory: %s\n", monthPath);
      return false;
    }
  }
  
  return true;
}

void SdManager::generateFilename(char* buffer, size_t bufferSize) {
  // Formato: /data/YYYY/MM/DD.txt usando RTC
  auto &rtc = RtcManager::instance();
  
  int year, month, day;
  
  if (!rtc.isAvailable()) {
    // Fallback a tiempo del sistema si RTC no está disponible
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    year = timeinfo.tm_year + 1900;
    month = timeinfo.tm_mon + 1;
    day = timeinfo.tm_mday;
  } else {
    // Usar fecha del RTC
    DateTime now = rtc.now();
    year = now.year();
    month = now.month();
    day = now.day();
  }
  
  // Asegurar que existan las carpetas año/mes
  ensureNestedDirectories(year, month);
  
  // Generar ruta completa: /data/YYYY/MM/DD.txt
  snprintf(buffer, bufferSize, "%s/%04d/%02d/%02d.txt",
           kSdDataPath,
           year,
           month,
           day);
}

bool SdManager::writeDataRecord(const SensorDataRecord &record) {
  if (!initialized_) {
    return false;
  }

  char filename[48];
  generateFilename(filename, sizeof(filename));
  
  File file = SD.open(filename, FILE_APPEND);
  if (!file) {
    LOGE("Failed to open file for writing\n");
    return false;
  }

  // Formato: {timestamp},{modbusModelId},{modbusModelName},{hexadecimales}
  file.printf("%lu,%u,%s,", record.timestamp, record.modbusModelId, record.modbusModelName);

  // Escribir datos hexadecimales
  for (size_t i = 0; i < record.rawData.size(); i++) {
    file.printf("%04X", record.rawData[i]);
  }
  file.println();

  file.close();
  return true;
}

bool SdManager::writeDataBatch(const std::vector<SensorDataRecord> &records) {
  if (!initialized_ || records.empty()) {
    return false;
  }

  char filename[48];
  generateFilename(filename, sizeof(filename));
  
  File file = SD.open(filename, FILE_APPEND);
  if (!file) {
    LOGE("Failed to open file for batch writing\n");
    return false;
  }

  // Escribir todos los registros en formato: {timestamp},{modbusModelId},{modbusModelName},{hexadecimales}
  for (const auto &record : records) {
    file.printf("%lu,%u,%s,", record.timestamp, record.modbusModelId, record.modbusModelName);

    // Escribir datos hexadecimales
    for (size_t i = 0; i < record.rawData.size(); i++) {
      file.printf("%04X", record.rawData[i]);
    }
    file.println();
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

String SdManager::listFiles(int year, int month) {
  if (!initialized_) {
    return "SD_NOT_AVAILABLE";
  }
  
  // Construir ruta: /data/YYYY/MM
  char dirPath[24];
  snprintf(dirPath, sizeof(dirPath), "%s/%04d/%02d", kSdDataPath, year, month);
  
  // Verificar si el directorio existe
  if (!SD.exists(dirPath)) {
    LOGW("Directory does not exist: %s\n", dirPath);
    return "DIR_NOT_FOUND";
  }
  
  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    LOGE("Failed to open directory: %s\n", dirPath);
    return "DIR_OPEN_ERROR";
  }
  
  String fileList;
  File entry;
  bool first = true;
  
  while ((entry = dir.openNextFile())) {
    if (!entry.isDirectory()) {
      // Obtener solo el nombre del archivo sin extensión
      String filename = entry.name();
      
      // Extraer solo el nombre (sin la ruta completa si la tiene)
      int lastSlash = filename.lastIndexOf('/');
      if (lastSlash >= 0) {
        filename = filename.substring(lastSlash + 1);
      }
      
      // Remover extensión .txt si existe
      if (filename.endsWith(".txt")) {
        filename = filename.substring(0, filename.length() - 4);
      }
      
      if (!first) {
        fileList += ",";
      }
      fileList += filename;
      first = false;
    }
    entry.close();
  }
  
  dir.close();
  
  if (fileList.length() == 0) {
    return "EMPTY";
  }
  
  LOGI("Listed files in %s: %s\n", dirPath, fileList.c_str());
  return fileList;
}

String SdManager::getBackupFilePath(int year, int month, int day) {
  // Construir ruta: /data/YYYY/MM/DD.txt
  char filePath[48];
  snprintf(filePath, sizeof(filePath), "%s/%04d/%02d/%02d.txt", 
           kSdDataPath, year, month, day);
  return String(filePath);
}

bool SdManager::uploadBackupFile(int year, int month, int day, int32_t deviceId) {
  if (!initialized_) {
    LOGE("SD: Not initialized, cannot upload\n");
    return false;
  }
  
  // Obtener ruta del archivo
  String filePath = getBackupFilePath(year, month, day);
  
  // Verificar si el archivo existe
  if (!SD.exists(filePath.c_str())) {
    LOGE("SD: Backup file does not exist: %s\n", filePath.c_str());
    return false;
  }
  
  // Abrir archivo
  File file = SD.open(filePath.c_str(), FILE_READ);
  if (!file) {
    LOGE("SD: Failed to open file: %s\n", filePath.c_str());
    return false;
  }
  
  size_t fileSize = file.size();
  if (fileSize == 0) {
    LOGW("SD: File is empty: %s\n", filePath.c_str());
    file.close();
    return false;
  }
  
  LOGI("SD: Uploading %s (%u bytes)\n", filePath.c_str(), fileSize);
  
  // Usar cliente HTTPS estático
  uploadClient.setInsecure();  // Skip certificate validation
  
  // Parsear URL base (similar a OTA)
  String baseUrl = String(kFirmwareBaseUrl);
  String host;
  int port = 443;
  
  if (baseUrl.startsWith("https://")) {
    host = baseUrl.substring(8);
  } else if (baseUrl.startsWith("http://")) {
    host = baseUrl.substring(7);
    port = 80;
  }
  
  // Remover trailing slash
  if (host.endsWith("/")) {
    host.remove(host.length() - 1);
  }
  
  LOGI("SD: Connecting to %s:%d\n", host.c_str(), port);
  
  if (!uploadClient.connect(host.c_str(), port)) {
    LOGE("SD: Connection to server failed\n");
    file.close();
    return false;
  }
  
  LOGI("SD: Connected, uploading...\n");
  
  // Construir nombre del archivo para el servidor (YYYYMMDD.txt)
  char serverFilename[20];
  snprintf(serverFilename, sizeof(serverFilename), "%04d%02d%02d.txt", year, month, day);
  
  // Enviar HTTP POST con el archivo como body
  uploadClient.print(String("POST ") + kBackupUploadEndpointPath + " HTTP/1.1\r\n");
  uploadClient.print(String("Host: ") + host + "\r\n");
  uploadClient.print(String("Content-Length: ") + String(fileSize) + "\r\n");
  uploadClient.print(String("Content-Type: application/octet-stream\r\n"));
  uploadClient.print(String("deviceId: ") + String(deviceId) + "\r\n");
  uploadClient.print(String("Connection: close\r\n\r\n"));
  
  // Enviar archivo en chunks
  uint8_t buffer[1024];
  size_t totalSent = 0;
  
  while (file.available()) {
    size_t bytesRead = file.read(buffer, sizeof(buffer));
    if (bytesRead > 0) {
      size_t bytesWritten = uploadClient.write(buffer, bytesRead);
      if (bytesWritten != bytesRead) {
        LOGE("SD: Write error, sent %u of %u bytes\n", bytesWritten, bytesRead);
        uploadClient.stop();
        file.close();
        delay(100);  // Dar tiempo al TLS para liberar recursos
        return false;
      }
      totalSent += bytesWritten;
    }
  }
  
  file.close();
  LOGI("SD: Sent %u bytes\n", totalSent);
  
  // Esperar respuesta con timeout
  unsigned long timeout = millis();
  while (uploadClient.available() == 0) {
    if (millis() - timeout > 30000) {
      LOGE("SD: Timeout waiting for server response\n");
      uploadClient.stop();
      delay(100);  // Dar tiempo al TLS para liberar recursos
      return false;
    }
    delay(10);
  }
  
  // Leer respuesta
  bool success = false;
  while (uploadClient.available()) {
    String line = uploadClient.readStringUntil('\n');
    LOGD("SD: Response: %s\n", line.c_str());
    
    // Verificar código de respuesta HTTP
    if (line.startsWith("HTTP/1.1 2") || line.startsWith("HTTP/1.0 2")) {
      success = true;
      LOGI("SD: Upload successful (HTTP 2xx)\n");
    }
  }
  
  uploadClient.stop();
  delay(100);  // Dar tiempo al TLS para liberar recursos
  
  if (!success) {
    LOGE("SD: Upload failed (non-2xx response)\n");
  }
  
  return success;
}

void SdManager::generateErrorFilename(char* buffer, size_t bufferSize) const {
  // Formato: /error/err_YYYYMMDD.txt usando RTC
  auto &rtc = RtcManager::instance();
  
  if (!rtc.isAvailable()) {
    // Fallback a tiempo del sistema si RTC no está disponible
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    snprintf(buffer, bufferSize, "%s/err_%04d%02d%02d.txt",
             kSdErrorPath,
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday);
    return;
  }
  
  // Usar fecha del RTC
  DateTime now = rtc.now();
  snprintf(buffer, bufferSize, "%s/err_%04d%02d%02d.txt",
           kSdErrorPath,
           now.year(),
           now.month(),
           now.day());
}

void SdManager::getTimestamp(char* buffer, size_t bufferSize) const {
  auto &rtc = RtcManager::instance();
  
  if (!rtc.isAvailable()) {
    // Fallback a millis si RTC no está disponible
    unsigned long ms = millis();
    snprintf(buffer, bufferSize, "%lu", ms / 1000);
    return;
  }
  
  // Usar timestamp del RTC
  DateTime now = rtc.now();
  snprintf(buffer, bufferSize, "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
}

bool SdManager::writeErrorLog(const char* level, const char* message) {
  if (!initialized_ || level == nullptr || message == nullptr) {
    return false;
  }

  char filename[40];
  generateErrorFilename(filename, sizeof(filename));
  
  File file = SD.open(filename, FILE_APPEND);
  if (!file) {
    return false;
  }

  char timestamp[32];
  getTimestamp(timestamp, sizeof(timestamp));
  
  file.printf("[%s] [%s] %s", timestamp, level, message);
  
  // Agregar newline si no existe
  size_t len = strlen(message);
  if (len == 0 || message[len-1] != '\n') {
    file.println();
  }
  
  file.close();
  return true;
}

bool SdManager::writeErrorLogFormatted(const char* level, const char* fmt, ...) {
  if (!initialized_ || level == nullptr || fmt == nullptr) {
    return false;
  }

  char message[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);

  return writeErrorLog(level, message);
}

