#include <Arduino.h>

#include "app_config.hpp"
#include "log.hpp"
#include "mqtt_manager.hpp"
#include "network_manager.hpp"

namespace {

TaskHandle_t networkTaskHandle = nullptr;
TaskHandle_t mqttTaskHandle = nullptr;

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
}

void loop() {
  // Idle loop to keep Arduino task alive; work happens in FreeRTOS tasks
  vTaskDelay(pdMS_TO_TICKS(1000));
}