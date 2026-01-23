#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @brief Gestor de actualizaciones OTA (Over-The-Air)
 * 
 * Maneja la descarga e instalación de firmware desde un servidor remoto.
 * Utiliza WiFiClientSecure para conexiones HTTPS y la librería Update de ESP32.
 */
class OtaManager {
 public:
  static OtaManager &instance();

  /**
   * @brief Registra los handles de las tareas para poder suspenderlas durante OTA
   * @param taskHandles Array de handles de tareas
   * @param count Número de tareas
   */
  void registerTasks(TaskHandle_t* taskHandles, size_t count);

  /**
   * @brief Ejecuta la actualización OTA para una versión específica
   * 
   * Este método bloquea hasta completar o fallar la actualización.
   * Si es exitoso, reinicia el ESP32 automáticamente.
   * 
   * @param version Versión del firmware a instalar (ej: "1.0.0")
   * @return true si la actualización fue exitosa (no debería retornar, reinicia)
   * @return false si hubo un error
   */
  bool performUpdate(const char* version);

 private:
  OtaManager() = default;
  OtaManager(const OtaManager &) = delete;
  OtaManager &operator=(const OtaManager &) = delete;

  static constexpr size_t kMaxTasks = 8;
  TaskHandle_t registeredTasks_[kMaxTasks] = {nullptr};
  size_t taskCount_ = 0;

  /**
   * @brief Suspende todas las tareas registradas para dar prioridad al OTA
   */
  void suspendAllTasks();

  /**
   * @brief Reanuda todas las tareas registradas (en caso de fallo del OTA)
   */
  void resumeAllTasks();

  /**
   * @brief Parsea la URL base para extraer host, puerto y protocolo
   */
  void parseBaseUrl(String &host, int &port, bool &useHttps);

  /**
   * @brief Conecta al servidor y envía el request HTTP GET
   * @return true si la conexión fue exitosa
   */
  bool connectAndSendRequest(const char* host, int port, const char* path);

  /**
   * @brief Parsea los headers HTTP de la respuesta
   * @return true si se recibió HTTP 200 y Content-Length válido
   */
  bool parseResponseHeaders(int &contentLength, bool &isValidContentType);

  /**
   * @brief Ejecuta la escritura del firmware
   * @return true si la escritura fue exitosa
   */
  bool writeAndFinalize(int contentLength);
};
