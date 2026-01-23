#include "ota_manager.hpp"

#include <WiFiClientSecure.h>
#include <Update.h>

#include "app_config.hpp"
#include "log.hpp"

namespace {
  // Cliente WiFi para la conexión HTTPS (global para acceso desde métodos privados)
  WiFiClientSecure otaClient;
}

OtaManager &OtaManager::instance() {
  static OtaManager inst;
  return inst;
}

void OtaManager::registerTasks(TaskHandle_t* taskHandles, size_t count) {
  taskCount_ = (count > kMaxTasks) ? kMaxTasks : count;
  for (size_t i = 0; i < taskCount_; i++) {
    registeredTasks_[i] = taskHandles[i];
  }
  LOGI("OTA: Registered %u tasks for suspension during update\n", taskCount_);
}

void OtaManager::suspendAllTasks() {
  LOGI("OTA: Suspending %u tasks...\n", taskCount_);
  for (size_t i = 0; i < taskCount_; i++) {
    if (registeredTasks_[i] != nullptr) {
      vTaskSuspend(registeredTasks_[i]);
    }
  }
  LOGI("OTA: All tasks suspended\n");
}

void OtaManager::resumeAllTasks() {
  LOGI("OTA: Resuming %u tasks...\n", taskCount_);
  for (size_t i = 0; i < taskCount_; i++) {
    if (registeredTasks_[i] != nullptr) {
      vTaskResume(registeredTasks_[i]);
    }
  }
  LOGI("OTA: All tasks resumed\n");
}

void OtaManager::parseBaseUrl(String &host, int &port, bool &useHttps) {
  String baseUrl = String(kFirmwareBaseUrl);
  
  if (baseUrl.startsWith("https://")) {
    host = baseUrl.substring(8);
    port = 443;
    useHttps = true;
  } else if (baseUrl.startsWith("http://")) {
    host = baseUrl.substring(7);
    port = 80;
    useHttps = false;
  }
  
  // Remover trailing slash si existe
  if (host.endsWith("/")) {
    host.remove(host.length() - 1);
  }
}

bool OtaManager::connectAndSendRequest(const char* host, int port, const char* path) {
  LOGI("OTA: Connecting to %s:%d\n", host, port);
  
  otaClient.setInsecure();  // Skip certificate validation for OTA
  
  if (!otaClient.connect(host, port)) {
    LOGE("OTA: Connection to %s failed\n", host);
    return false;
  }
  
  LOGI("OTA: Connected, sending request...\n");
  LOGI("OTA: Fetching: %s\n", path);
  
  // Enviar request HTTP GET
  otaClient.print(String("GET ") + path + " HTTP/1.1\r\n" +
                  "Host: " + host + "\r\n" +
                  "Cache-Control: no-cache\r\n" +
                  "Connection: close\r\n\r\n");
  
  // Esperar respuesta con timeout
  unsigned long timeout = millis();
  while (otaClient.available() == 0) {
    if (millis() - timeout > kOtaTimeoutMs) {
      LOGE("OTA: Client timeout waiting for response\n");
      otaClient.stop();
      return false;
    }
    delay(10);
  }
  
  return true;
}

bool OtaManager::parseResponseHeaders(int &contentLength, bool &isValidContentType) {
  contentLength = 0;
  isValidContentType = false;
  bool isHttpOk = false;
  
  while (otaClient.available()) {
    String line = otaClient.readStringUntil('\n');
    line.trim();
    
    // Línea vacía = fin de headers
    if (line.length() == 0) {
      break;
    }
    
    LOGD("OTA Header: %s\n", line.c_str());
    
    // Verificar código HTTP
    if (line.startsWith("HTTP/1.1")) {
      if (line.indexOf("200") > 0) {
        isHttpOk = true;
        LOGI("OTA: Got HTTP 200 OK\n");
      } else {
        LOGE("OTA: Got non-200 response: %s\n", line.c_str());
      }
    }
    
    // Extraer Content-Length
    if (line.startsWith("Content-Length: ")) {
      contentLength = atoi(line.substring(16).c_str());
      LOGI("OTA: Content-Length: %d bytes\n", contentLength);
    }
    
    // Extraer Content-Type
    if (line.startsWith("Content-Type: ")) {
      String contentType = line.substring(14);
      if (contentType.startsWith("application/octet-stream") || 
          contentType.startsWith("application/macbinary")) {
        isValidContentType = true;
        LOGI("OTA: Valid Content-Type: %s\n", contentType.c_str());
      }
    }
  }
  
  if (!isHttpOk) {
    LOGE("OTA: Server did not return HTTP 200\n");
    return false;
  }
  
  if (contentLength == 0) {
    LOGE("OTA: No content length received\n");
    return false;
  }
  
  if (!isValidContentType) {
    LOGW("OTA: Content-Type not application/octet-stream, proceeding anyway\n");
  }
  
  return true;
}

bool OtaManager::writeAndFinalize(int contentLength) {
  LOGI("OTA: Starting update, %d bytes to write...\n", contentLength);
  
  if (!Update.begin(contentLength)) {
    LOGE("OTA: Not enough space to begin update\n");
    return false;
  }
  
  LOGI("OTA: Writing firmware... This may take 2-5 minutes\n");
  
  // Escribir el stream directamente al Update
  size_t written = Update.writeStream(otaClient);
  
  if (written == static_cast<size_t>(contentLength)) {
    LOGI("OTA: Written %u bytes successfully\n", written);
  } else {
    LOGE("OTA: Written only %u of %d bytes\n", written, contentLength);
  }
  
  if (Update.end()) {
    LOGI("OTA: Update complete!\n");
    if (Update.isFinished()) {
      LOGI("OTA: Update successfully completed. Rebooting...\n");
      delay(1000);
      ESP.restart();
      return true;  // No debería llegar aquí
    } else {
      LOGE("OTA: Update not finished. Something went wrong!\n");
    }
  } else {
    LOGE("OTA: Update failed. Error #: %d\n", Update.getError());
  }
  
  return false;
}

bool OtaManager::performUpdate(const char* version) {
  LOGI("OTA: Processing firmware update for version: %s\n", version);
  
  // Suspender todas las tareas para dar prioridad al OTA
  suspendAllTasks();
  
  // Esperar un poco para que las tareas se detengan completamente
  delay(500);
  
  // Parsear host y puerto desde kFirmwareBaseUrl
  String host;
  int port = 443;
  bool useHttps = true;
  parseBaseUrl(host, port, useHttps);
  
  // Construir path completo usando la constante de configuración
  String path = String(kFirmwareEndpointPath) + version;
  
  // Conectar y enviar request
  if (!connectAndSendRequest(host.c_str(), port, path.c_str())) {
    LOGE("OTA: Connection failed, resuming tasks\n");
    resumeAllTasks();
    return false;
  }
  
  // Parsear headers de respuesta
  int contentLength = 0;
  bool isValidContentType = false;
  if (!parseResponseHeaders(contentLength, isValidContentType)) {
    LOGE("OTA: Invalid response, resuming tasks\n");
    otaClient.stop();
    resumeAllTasks();
    return false;
  }
  
  // Escribir firmware y finalizar
  bool success = writeAndFinalize(contentLength);
  
  otaClient.stop();
  
  // Si falló, reanudar tareas (si fue exitoso, el ESP se reinicia)
  if (!success) {
    LOGE("OTA: Update failed, resuming tasks\n");
    resumeAllTasks();
  }
  
  return success;
}
