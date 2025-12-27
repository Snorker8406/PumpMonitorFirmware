#include <Arduino.h>

#include "app_config.hpp"
#include "log.hpp"
#include "modbus_manager.hpp"
#include "mqtt_manager.hpp"
#include "network_manager.hpp"
#include "rtc_manager.hpp"
#include "sd_manager.hpp"

namespace {

TaskHandle_t networkTaskHandle = nullptr;
TaskHandle_t mqttTaskHandle = nullptr;
TaskHandle_t modbusTaskHandle = nullptr;
TaskHandle_t sdTaskHandle = nullptr;

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
  auto &sd = SdManager::instance();
  auto &rtc = RtcManager::instance();
  modbus.begin();

  std::vector<ModbusDeviceData> devicesData;
  std::vector<SensorDataRecord> recordsBuffer;
  recordsBuffer.reserve(kModbusDeviceCount);

  for (;;) {
    if (net.isConnected()) {
      modbus.loop();
      
      if (modbus.readAllDevices(devicesData)) {
        recordsBuffer.clear();
        unsigned long timestamp = rtc.isAvailable() ? rtc.getUnixTime() : (millis() / 1000);
        
        // Mostrar y preparar datos para SD
        for (const auto &device : devicesData) {
          if (device.success) {
            LOGI("[%s] Data: ", device.name);
            for (size_t i = 0; i < device.values.size(); i++) {
              Serial.printf("%.2f%s", device.values[i], (i < device.values.size() - 1) ? ", " : "\n");
            }
            
            // Preparar registro para SD
            if (!device.values.empty()) {
              SensorDataRecord record;
              record.timestamp = timestamp;
              record.deviceName = device.name;
              record.deviceIp = device.ip;
              record.values = device.values;
              recordsBuffer.push_back(record);
            }
          }
        }
        
        // Guardar en SD si está disponible
        if (sd.isAvailable() && !recordsBuffer.empty()) {
          if (sd.writeDataBatch(recordsBuffer)) {
            LOGI("SD: %u records saved\n", recordsBuffer.size());
          } else {
            LOGW("SD: write failed\n");
          }
        }
      } else {
        LOGW("Some Modbus devices failed to read\n");
      }
      
      // Liberar vectores y consolidar heap
      devicesData.clear();
      devicesData.shrink_to_fit();
      recordsBuffer.clear();
      recordsBuffer.shrink_to_fit();
      
      vTaskDelay(pdMS_TO_TICKS(kModbusReadPeriodMs));
    } else {
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

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);

  // ----- Startup & initialization -----
  LOGI("Booting...\n");
  
  // Inicializar RTC primero
  const bool rtcOk = RtcManager::instance().begin();
  LOGI("%s\n", rtcOk ? "RTC initialized" : "RTC initialization failed");
  
  // Inicializar Ethernet
  const bool ethOk = NetworkManager::instance().begin();
  LOGI("%s\n", ethOk ? "Ethernet listo" : "Ethernet fallo al iniciar");

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
}

void loop() {
  // Idle loop to keep Arduino task alive; work happens in FreeRTOS tasks
  vTaskDelay(pdMS_TO_TICKS(1000));
}