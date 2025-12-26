#include <Arduino.h>

#include "app_config.hpp"
#include "log.hpp"
#include "modbus_manager.hpp"
#include "mqtt_manager.hpp"
#include "network_manager.hpp"
#include "sd_manager.hpp"

namespace {

TaskHandle_t networkTaskHandle = nullptr;
TaskHandle_t mqttTaskHandle = nullptr;
TaskHandle_t modbusTaskHandle = nullptr;
TaskHandle_t sdTaskHandle = nullptr;

// ----- Network monitor task -----

void networkMonitorTask(void *) {
  auto &net = NetworkManager::instance();
  for (;;) {
    if (net.isConnected()) {
       LOGI("ETH up | IP: %s | MAC: %s | Speed: %lu Mbps\n",
         net.localIP().toString().c_str(), net.macString(), net.linkSpeedMbps());
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
  modbus.begin();

  std::vector<ModbusDeviceData> devicesData;

  for (;;) {
    if (net.isConnected()) {
      modbus.loop();
      
      if (modbus.readAllDevices(devicesData)) {
        // Todos los dispositivos leídos correctamente
        for (const auto &device : devicesData) {
          if (device.success) {
            LOGI("[%s] Data: ", device.name);
            for (size_t i = 0; i < device.values.size(); i++) {
              Serial.printf("%.2f%s", device.values[i], (i < device.values.size() - 1) ? ", " : "\n");
            }
          }
        }
      } else {
        LOGW("Some Modbus devices failed to read\n");
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

// ----- SD storage task -----

void sdTask(void *) {
  auto &net = NetworkManager::instance();
  auto &sd = SdManager::instance();
  auto &modbus = ModbusManager::instance();
  
  // Intentar inicializar SD
  if (!sd.begin()) {
    LOGE("SD task: initialization failed, task will retry periodically\n");
  }

  std::vector<ModbusDeviceData> devicesData;
  std::vector<SensorDataRecord> recordsBuffer;
  recordsBuffer.reserve(kModbusDeviceCount);

  for (;;) {
    // Reintentar inicialización si falló
    if (!sd.isAvailable()) {
      LOGW("SD not available, retrying initialization...\n");
      sd.begin();
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    if (net.isConnected()) {
      // Leer datos de Modbus
      if (modbus.readAllDevices(devicesData)) {
        recordsBuffer.clear();
        
        // Convertir a registros para SD
        unsigned long timestamp = millis() / 1000;  // timestamp en segundos
        for (const auto &device : devicesData) {
          if (device.success && !device.values.empty()) {
            SensorDataRecord record;
            record.timestamp = timestamp;
            record.deviceName = device.name;
            record.deviceIp = device.ip;
            record.values = device.values;
            recordsBuffer.push_back(record);
          }
        }
        
        // Escribir batch a SD
        if (!recordsBuffer.empty()) {
          if (sd.writeDataBatch(recordsBuffer)) {
            LOGI("SD: %u records saved\n", recordsBuffer.size());
          } else {
            LOGW("SD: write failed\n");
          }
        }
      }
      
      // Liberar memoria
      devicesData.clear();
      devicesData.shrink_to_fit();
      recordsBuffer.clear();
      recordsBuffer.shrink_to_fit();
      
      vTaskDelay(pdMS_TO_TICKS(kSdWritePeriodMs));
    } else {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);

  // ----- Startup & network bring-up -----
  LOGI("Booting...\n");
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