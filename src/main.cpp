#include <Arduino.h>

#include "app_config.hpp"
#include "modbus_manager.hpp"
#include "mqtt_manager.hpp"
#include "network_manager.hpp"
#include "rtc_manager.hpp"
#include "sd_manager.hpp"
#include "eeprom_manager.hpp"
#include "ota_manager.hpp"
#include "log.hpp"

// Forward declarations para funciones de control de Real Time
void startRealTimeMode(uint32_t durationSeconds);
void stopRealTimeMode();
bool isRealTimeModeActive();

// Implementación de función auxiliar para escribir logs a SD
void logToSdFile(LogLevel level, const char* fmt, ...) {
  if (level > LOG_LEVEL_WARN) return; // Solo E y W van a SD
  
  SdManager& sd = SdManager::instance();
  
  char buffer[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  
  static const char *kLevelNames[] = {"ERROR", "WARN", "INFO", "DEBUG"};
  sd.writeErrorLog(kLevelNames[level], buffer);
}

// Variables globales de control para Real Time Mode
volatile bool isRealTimeActive = false;
volatile unsigned long realTimeEndMs = 0;

// Funciones auxiliares para Real Time Mode (fuera del namespace para extern)
void startRealTimeMode(uint32_t durationSeconds) {
  isRealTimeActive = true;
  realTimeEndMs = millis() + (durationSeconds * 1000);
  LOGI("Real Time Mode started for %lu seconds\n", durationSeconds);
}

void stopRealTimeMode() {
  if (isRealTimeActive) {
    isRealTimeActive = false;
    LOGI("Real Time Mode stopped\n");
  }
}

bool isRealTimeModeActive() {
  if (isRealTimeActive && millis() >= realTimeEndMs) {
    stopRealTimeMode();
  }
  return isRealTimeActive;
}

namespace {

TaskHandle_t networkTaskHandle = nullptr;
TaskHandle_t mqttTaskHandle = nullptr;
TaskHandle_t modbusTaskHandle = nullptr;
TaskHandle_t sdTaskHandle = nullptr;
TaskHandle_t realTimeModbusTaskHandle = nullptr;
TaskHandle_t instantValuesTaskHandle = nullptr;

// Mutex para sincronizar acceso a ModbusManager entre tareas
SemaphoreHandle_t modbusMutex = nullptr;

// ----- Network monitor task -----

void networkMonitorTask(void *) {
  auto &net = NetworkManager::instance();
  auto &rtc = RtcManager::instance();
  
  for (;;) {
    if (net.isConnected()) {
      IPAddress ip = net.localIP();
      if (rtc.isAvailable()) {
        char timeStr[16];
        DateTime now = rtc.now();
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
                 now.hour(), now.minute(), now.second());
        LOGI("ETH up | IP: %u.%u.%u.%u | MAC: %s | Speed: %lu Mbps | Time: %s\n",
             ip[0], ip[1], ip[2], ip[3], net.macString(), net.linkSpeedMbps(), timeStr);
      } else {
        LOGI("ETH up | IP: %u.%u.%u.%u | MAC: %s | Speed: %lu Mbps\n",
             ip[0], ip[1], ip[2], ip[3], net.macString(), net.linkSpeedMbps());
      }
    } else {
       LOGW("ETH down | waiting...\n");
    }
    vTaskDelay(pdMS_TO_TICKS(kNetworkTaskPeriodMs));
  }
}

// ----- MQTT task -----

void mqttTask(void *) {
  auto &net = NetworkManager::instance();
  auto &mqtt = MqttManager::instance();
  mqtt.begin();

  for (;;) {
    // Verificar si hay actualización de firmware pendiente (fuera del callback)
    if (mqtt.hasPendingFirmwareUpdate()) {
      mqtt.processPendingFirmwareUpdate();
      // Después del OTA, el dispositivo se reiniciará o continuará
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }
    
    if (net.isConnected()) {
      mqtt.ensureConnected();
      mqtt.loop();
      vTaskDelay(pdMS_TO_TICKS(kMqttLoopDelayMs));
    } else {
      mqtt.disconnect();
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
}

// ----- Modbus task -----

void modbusTask(void *) {
  auto &net = NetworkManager::instance();
  auto &modbus = ModbusManager::instance();
  auto &rtc = RtcManager::instance();
  modbus.begin();

  std::vector<ModbusDeviceData> devicesData;

  for (;;) {
    if (net.isConnected()) {
      modbus.loop();
      
      // Adquirir mutex antes de acceder a Modbus
      if (xSemaphoreTake(modbusMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        bool readSuccess = modbus.readAllDevices(devicesData);
        xSemaphoreGive(modbusMutex);  // Liberar mutex
        
        if (readSuccess) {
          // Mostrar datos en consola
          for (const auto &device : devicesData) {
            if (device.success) {
              LOGI("[%s] Data: ", device.modbusModelId);
              for (size_t i = 0; i < device.values.size(); i++) {
                Serial.printf("%.2f%s", device.values[i], (i < device.values.size() - 1) ? ", " : "\n");
              }
            } else {
              LOGE("Modbus %s read failed, no data available\n", device.modbusModelId);
            }
          }
        } else {
          LOGE("Modbus read failed for all devices\n");
        }
      } else {
        LOGW("Modbus mutex timeout in modbusTask\n");
      }
      
      // Liberar vectores y consolidar heap
      devicesData.clear();
      devicesData.shrink_to_fit();
      
      vTaskDelay(pdMS_TO_TICKS(kModbusReadPeriodMs));
    } else {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}

// ----- Real Time Modbus task -----
// Tarea independiente para publicar datos Modbus en tiempo real

void realTimeModbusTask(void *) {
  auto &modbus = ModbusManager::instance();
  auto &mqtt = MqttManager::instance();
  auto &net = NetworkManager::instance();
  
  vTaskDelay(pdMS_TO_TICKS(5000));  // Esperar inicialización
  
  for (;;) {
    // Solo ejecutar si Real Time Mode está activo
    if (isRealTimeModeActive() && mqtt.isConnected()) {
      // Obtener intervalo configurado
      uint16_t intervalSec = EepromManager::instance().getRealTimeIntervalSec();
      
      // Obtener MAC para construir topic
      const char *macColoned = net.macString();
      char macNoColon[13] = {0};
      int idx = 0;
      for (const char *p = macColoned; *p && idx < 12; ++p) {
        if (*p != ':') {
          macNoColon[idx++] = *p;
        }
      }
      
      char realTimeTopic[48];
      snprintf(realTimeTopic, sizeof(realTimeTopic), "device/%s_realTime", macNoColon);
      
      // Leer cada dispositivo Modbus y publicar
      std::vector<ModbusDeviceData> devicesData;
      devicesData.reserve(kModbusDeviceCount);
      
      // Adquirir mutex antes de acceder a Modbus
      if (xSemaphoreTake(modbusMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        bool readSuccess = modbus.readAllDevices(devicesData);
        xSemaphoreGive(modbusMutex);  // Liberar mutex inmediatamente
        
        if (readSuccess) {
        for (const auto &device : devicesData) {
          if (device.success && !device.rawData.empty()) {
            // Construir mensaje: modbusModelId,HEXDATA
            char msgBuffer[512];
            int offset = snprintf(msgBuffer, sizeof(msgBuffer), "%s,", device.modbusModelId);
            
            // Agregar datos hexadecimales
            for (size_t i = 0; i < device.rawData.size() && offset < (int)sizeof(msgBuffer) - 5; i++) {
              offset += snprintf(msgBuffer + offset, sizeof(msgBuffer) - offset, "%04X", device.rawData[i]);
            }
            
            // Log de diagnóstico
            size_t msgLen = strlen(msgBuffer);
            LOGI("RT: %s message size: %u bytes (%u regs)\n", device.modbusModelId, msgLen, device.rawData.size());
            
            // Publicar
            if (mqtt.publish(realTimeTopic, msgBuffer)) {
              LOGI("RT: Published %s\n", device.modbusModelId);
            } else {
              LOGE("RT: Failed to publish %s (size: %u bytes)\n", device.modbusModelId, msgLen);
            }
            
            // Esperar entre dispositivos
            vTaskDelay(pdMS_TO_TICKS(kModbusInterDeviceDelayMs));
          }
        }
        
        // Liberar memoria
        devicesData.clear();
        devicesData.shrink_to_fit();
        
        // Esperar intervalo configurado
        vTaskDelay(pdMS_TO_TICKS(intervalSec * 1000));
        } else {
          LOGE("RT: Modbus read failed\n");
          vTaskDelay(pdMS_TO_TICKS(1000));
        }
      } else {
        LOGW("RT: Modbus mutex timeout\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    } else {
      // Si no está activo, dormir más tiempo
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}

// ----- SD storage task -----
// NOTA: Esta tarea solo inicializa SD. Los datos son guardados por modbusTask

void sdTask(void *) {
  auto &sd = SdManager::instance();
  
  // Intentar inicializar SD
  if (!sd.begin()) {
    LOGE("SD task: initialization failed\n");
  } else {
    LOGI("SD task: initialization successful\n");
  }

  // Esta tarea solo se encarga de mantener SD disponible
  for (;;) {
    // Reintentar inicialización si falló
    if (!sd.isAvailable()) {
      LOGW("SD not available, retrying initialization...\n");
      if (sd.begin()) {
        LOGI("SD reinitialized successfully\n");
      }
      vTaskDelay(pdMS_TO_TICKS(10000));
    } else {
      // SD está disponible, solo esperar
      vTaskDelay(pdMS_TO_TICKS(30000));
    }
  }
}

// ----- Instant Values task -----
// Tarea para publicar datos Modbus periódicamente (siempre activa)

void instantValuesTask(void *) {
  auto &modbus = ModbusManager::instance();
  auto &mqtt = MqttManager::instance();
  auto &net = NetworkManager::instance();
  auto &sd = SdManager::instance();
  auto &rtc = RtcManager::instance();
  
  std::vector<SensorDataRecord> recordsBuffer;
  recordsBuffer.reserve(kModbusDeviceCount);
  
  vTaskDelay(pdMS_TO_TICKS(10000));  // Esperar inicialización
  
  for (;;) {
    // Obtener intervalo configurado
    uint16_t intervalSec = EepromManager::instance().getInstantValuesIntervalSec();
    
    // Solo ejecutar si MQTT está conectado
    if (mqtt.isConnected()) {
      // Obtener MAC para construir topic
      const char *macColoned = net.macString();
      char macNoColon[13] = {0};
      int idx = 0;
      for (const char *p = macColoned; *p && idx < 12; ++p) {
        if (*p != ':') {
          macNoColon[idx++] = *p;
        }
      }
      
      char instValTopic[48];
      snprintf(instValTopic, sizeof(instValTopic), "device/%s_instVal", macNoColon);
      
      // Obtener deviceId para incluir en los mensajes
      int32_t deviceId = EepromManager::instance().getDeviceID();
      
      // Leer cada dispositivo Modbus y publicar
      std::vector<ModbusDeviceData> devicesData;
      devicesData.reserve(kModbusDeviceCount);
      
      // Adquirir mutex antes de acceder a Modbus
      if (xSemaphoreTake(modbusMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        bool readSuccess = modbus.readAllDevices(devicesData);
        xSemaphoreGive(modbusMutex);  // Liberar mutex inmediatamente
        
        if (readSuccess) {
          recordsBuffer.clear();
          unsigned long timestamp = rtc.isAvailable() ? rtc.getUnixTime() : (millis() / 1000);
          
          for (const auto &device : devicesData) {
            if (device.success && !device.rawData.empty()) {
              // Construir mensaje: {deviceId},{modbusModelId},{rawData}
              char msgBuffer[512];
              int offset = snprintf(msgBuffer, sizeof(msgBuffer), "%d,%s,", deviceId, device.modbusModelId);
              
              // Agregar datos hexadecimales
              for (size_t i = 0; i < device.rawData.size() && offset < (int)sizeof(msgBuffer) - 5; i++) {
                offset += snprintf(msgBuffer + offset, sizeof(msgBuffer) - offset, "%04X", device.rawData[i]);
              }
              
              // Publicar por MQTT
              if (mqtt.publish(instValTopic, msgBuffer)) {
                LOGD("IV: Published %s\n", device.modbusModelId);
              } else {
                LOGE("IV: Failed to publish %s\n", device.modbusModelId);
              }
              
              // Preparar registro para SD
              SensorDataRecord record;
              record.timestamp = timestamp;
              record.modbusModelId = device.modbusModelId;
              record.deviceIp = device.ip;
              record.values = device.values;
              record.rawData = device.rawData;
              recordsBuffer.push_back(record);
              
              // Esperar entre dispositivos
              vTaskDelay(pdMS_TO_TICKS(kModbusInterDeviceDelayMs));
            }
          }
          
          // Guardar en SD si está disponible
          if (sd.isAvailable() && !recordsBuffer.empty()) {
            if (sd.writeDataBatch(recordsBuffer)) {
              LOGI("SD: %u records saved\n", recordsBuffer.size());
            } else {
              LOGE("SD: failed to write %u records\n", recordsBuffer.size());
            }
          }
          
          // Liberar memoria
          devicesData.clear();
          devicesData.shrink_to_fit();
          recordsBuffer.clear();
          recordsBuffer.shrink_to_fit();
        } else {
          LOGE("IV: Modbus read failed\n");
        }
      } else {
        LOGW("IV: Modbus mutex timeout\n");
      }
    }
    
    // Esperar intervalo configurado
    vTaskDelay(pdMS_TO_TICKS(intervalSec * 1000));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);

  // ----- Startup & initialization -----
  LOGI("Booting...\n");
  
  // Inicializar EEPROM
  EepromManager::instance().begin();
  
  // Obtener valores en variables locales antes de imprimir
  String webServiceUrl = EepromManager::instance().getWebServiceURL();
  LOGI("Web Service URL: %s\n", webServiceUrl.c_str());
  LOGI("Firmware Version: %s\n", kFirmwareVersion);
  
  // Inicializar RTC primero
  const bool rtcOk = RtcManager::instance().begin();
  LOGI("%s\n", rtcOk ? "RTC initialized" : "RTC initialization failed");
  
  // Inicializar Ethernet
  const bool ethOk = NetworkManager::instance().begin();
  LOGI("%s\n", ethOk ? "Ethernet listo" : "Ethernet fallo al iniciar");

  // Crear mutex para sincronización de Modbus
  modbusMutex = xSemaphoreCreateMutex();
  if (modbusMutex == nullptr) {
    LOGE("Failed to create modbus mutex\n");
  }

  // ----- Task creation -----
  xTaskCreatePinnedToCore(
      networkMonitorTask,
      "network-task",
      kNetworkTaskStackWords,
      nullptr,
      kNetworkTaskPriority,
      &networkTaskHandle,
      kNetworkTaskCore);

  xTaskCreatePinnedToCore(
      mqttTask,
      "mqtt-task",
      kMqttTaskStackWords,
      nullptr,
      kMqttTaskPriority,
      &mqttTaskHandle,
      kMqttTaskCore);

  xTaskCreatePinnedToCore(
      modbusTask,
      "modbus-task",
      kModbusTaskStackWords,
      nullptr,
      kModbusTaskPriority,
      &modbusTaskHandle,
      kModbusTaskCore);

  xTaskCreatePinnedToCore(
      sdTask,
      "sd-task",
      kSdTaskStackWords,
      nullptr,
      kSdTaskPriority,
      &sdTaskHandle,
      kSdTaskCore);

  xTaskCreatePinnedToCore(
      realTimeModbusTask,
      "realtime-task",
      kModbusTaskStackWords,
      nullptr,
      kModbusTaskPriority - 1,  // Prioridad menor que modbus-task
      &realTimeModbusTaskHandle,
      kModbusTaskCore);

  xTaskCreatePinnedToCore(
      instantValuesTask,
      "instval-task",
      kModbusTaskStackWords,
      nullptr,
      kModbusTaskPriority - 1,  // Prioridad menor que modbus-task
      &instantValuesTaskHandle,
      kModbusTaskCore);

  // Registrar tareas con OtaManager para suspenderlas durante actualizaciones
  // No incluimos mqttTaskHandle porque esa tarea maneja el OTA
  TaskHandle_t tasksToSuspend[] = {
    networkTaskHandle,
    modbusTaskHandle,
    sdTaskHandle,
    realTimeModbusTaskHandle,
    instantValuesTaskHandle
  };
  OtaManager::instance().registerTasks(tasksToSuspend, sizeof(tasksToSuspend) / sizeof(tasksToSuspend[0]));
}

void loop() {
  // Idle loop to keep Arduino task alive; work happens in FreeRTOS tasks
  vTaskDelay(pdMS_TO_TICKS(1000));
}