#include <Arduino.h>

#include "app_config.hpp"
#include "log.hpp"
#include "network_manager.hpp"

namespace {

TaskHandle_t networkTaskHandle = nullptr;

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

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);

  LOGI("Booting...\n");
  const bool ethOk = NetworkManager::instance().begin();
    LOGI("%s\n", ethOk ? "Ethernet listo" : "Ethernet fallo al iniciar");

  xTaskCreatePinnedToCore(
      networkMonitorTask,
      "network-task",
      kNetworkTaskStackWords,
      nullptr,
      kNetworkTaskPriority,
      &networkTaskHandle,
      kNetworkTaskCore);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}