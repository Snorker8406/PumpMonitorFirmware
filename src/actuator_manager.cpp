#include "actuator_manager.hpp"

#include "log.hpp"
#include "modbus_manager.hpp"
#include "mqtt_manager.hpp"
#include "network_manager.hpp"

namespace {
// Secuencia cíclica de estados válidos de los 3 switches.
// Cada paso cambia un solo switch:
//   000 -> 100 -> 110 -> 111 -> 011 -> 001 -> 000
// Se puede avanzar (siguiente) o retroceder (anterior / "arrepentirse").
//   - Llegar a 111 desde 110  -> escribe coil ON.
//   - Llegar a 000 desde 001  -> escribe coil OFF.
constexpr uint8_t kCycleLen = 6;
constexpr bool kCycle[kCycleLen][3] = {
    {false, false, false},  // 000
    {true, false, false},   // 100
    {true, true, false},    // 110
    {true, true, true},     // 111
    {false, true, true},    // 011
    {false, false, true},   // 001
};

// Devuelve la posición del estado en el ciclo, o -1 si no es válido.
int cyclePos(const bool s[3]) {
  for (int i = 0; i < kCycleLen; i++) {
    if (kCycle[i][0] == s[0] && kCycle[i][1] == s[1] && kCycle[i][2] == s[2]) {
      return i;
    }
  }
  return -1;
}

bool sameState(const bool a[3], const bool b[3]) {
  return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}
}  // namespace

ActuatorManager &ActuatorManager::instance() {
  static ActuatorManager inst;
  return inst;
}

void ActuatorManager::begin() {
  mutex_ = xSemaphoreCreateMutex();
  if (!mutex_) {
    LOGE("Failed to create Actuator mutex\n");
  }
  for (size_t i = 0; i < kActuatorCoilCount; i++) {
    confirmationsEnabled_[i] = kActuatorConfirmationsEnabled[i];
  }
  LOGI("ActuatorManager initialized (%u coils, %u confirmations/coil)\n",
       (unsigned)kActuatorCoilCount, (unsigned)kActuatorConfirmCount);
}

bool ActuatorManager::confirmationsEnabled(size_t coilIndex) const {
  if (coilIndex >= kActuatorCoilCount) {
    return false;
  }
  return confirmationsEnabled_[coilIndex];
}

void ActuatorManager::setConfirmationsEnabled(size_t coilIndex, bool enabled) {
  if (coilIndex >= kActuatorCoilCount) {
    LOGE("setConfirmationsEnabled: index out of range (coil=%u)\n", (unsigned)coilIndex);
    return;
  }
  confirmationsEnabled_[coilIndex] = enabled;
  LOGI("Actuator coil %u confirmations %s\n", (unsigned)coilIndex, enabled ? "ENABLED" : "DISABLED");
}

void ActuatorManager::setConfirmation(size_t coilIndex, uint8_t confirmIndex, bool value) {
  if (coilIndex >= kActuatorCoilCount || confirmIndex >= kActuatorConfirmCount) {
    LOGE("setConfirmation: index out of range (coil=%u confirm=%u)\n",
         (unsigned)coilIndex, (unsigned)confirmIndex);
    return;
  }

  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
    LOGE("setConfirmation: mutex timeout\n");
    return;
  }

  // Estado actual y estado solicitado (cambiando un solo switch).
  bool cur[3] = {confirm_[coilIndex][0], confirm_[coilIndex][1], confirm_[coilIndex][2]};
  bool next[3] = {cur[0], cur[1], cur[2]};
  next[confirmIndex] = value;

  bool applied = false;
  bool triggerOn = false;
  bool triggerOff = false;

  if (sameState(next, cur)) {
    // Sin cambio real (idempotente): se acepta sin disparar nada.
    applied = true;
  } else {
    int curPos = cyclePos(cur);
    int nextPos = cyclePos(next);
    // Transición válida solo si el nuevo estado es adyacente en el ciclo
    // (siguiente = avanzar, anterior = retroceder/arrepentirse).
    // Excepción: desde 000 (todos OFF) y desde 111 (todos ON) solo se permite
    // avanzar; retroceder está bloqueado para forzar iniciar la secuencia
    // desde el primer switch.
    bool isAllOff = !cur[0] && !cur[1] && !cur[2];
    bool isAllOn  =  cur[0] &&  cur[1] &&  cur[2];
    bool forwardOnly = isAllOff || isAllOn;
    bool isForward = (curPos >= 0 && nextPos >= 0) &&
                     (nextPos == (curPos + 1) % kCycleLen);
    bool isBackward = (curPos >= 0 && nextPos >= 0) &&
                      (nextPos == (curPos + kCycleLen - 1) % kCycleLen);
    bool adjacent = forwardOnly ? isForward : (isForward || isBackward);
    if (adjacent) {
      confirm_[coilIndex][confirmIndex] = value;
      applied = true;

      // Disparo de escritura (solo con confirmaciones activas).
      if (confirmationsEnabled_[coilIndex]) {
        // 110 -> 111  => ON
        if (next[0] && next[1] && next[2] && cur[0] && cur[1] && !cur[2]) {
          pendingWrite_[coilIndex] = true;
          pendingValue_[coilIndex] = true;
          triggerOn = true;
        }
        // 001 -> 000  => OFF
        else if (!next[0] && !next[1] && !next[2] && !cur[0] && !cur[1] && cur[2]) {
          pendingWrite_[coilIndex] = true;
          pendingValue_[coilIndex] = false;
          triggerOff = true;
        }
      }
    }
  }

  // Captura del estado actual de los switches para publicar.
  char states[kActuatorConfirmCount + 1];
  for (uint8_t i = 0; i < kActuatorConfirmCount; i++) {
    states[i] = confirm_[coilIndex][i] ? '1' : '0';
  }
  states[kActuatorConfirmCount] = '\0';

  xSemaphoreGive(mutex_);

  if (!applied) {
    LOGW("Actuator coil %u confirm[%u]=%u RECHAZADO (paso no secuencial, estado %s)\n",
         (unsigned)coilIndex, (unsigned)confirmIndex, value ? 1 : 0, states);
  } else {
    const char* trig = triggerOn ? " -> WRITE ON QUEUED"
                                 : (triggerOff ? " -> WRITE OFF QUEUED" : "");
    LOGI("Actuator coil %u confirm[%u]=%u (estado %s)%s\n",
         (unsigned)coilIndex, (unsigned)confirmIndex, value ? 1 : 0, states, trig);
  }

  // Publicar estado del coil: "index,estados" (ej: "1,110").
  char payload[24];
  snprintf(payload, sizeof(payload), "%u,%s", (unsigned)coilIndex, states);
  publishStatus("coilStatus", payload);
}

void ActuatorManager::requestCoil(size_t coilIndex, bool value) {
  if (coilIndex >= kActuatorCoilCount) {
    LOGE("requestCoil: index out of range (coil=%u)\n", (unsigned)coilIndex);
    return;
  }

  if (confirmationsEnabled_[coilIndex]) {
    LOGW("Actuator coil %u setCoil rechazado: confirmaciones activas (usa confirmCoil)\n",
         (unsigned)coilIndex);
    return;
  }

  triggerWrite(coilIndex, value);
  LOGI("Actuator coil %u direct write %u queued\n", (unsigned)coilIndex, value ? 1 : 0);
}

void ActuatorManager::publishStatus(const char* name, const char* payload) {
  const char* macColoned = NetworkManager::instance().macString();
  char macNoColon[13] = {0};
  int idx = 0;
  for (const char* p = macColoned; *p && idx < 12; ++p) {
    if (*p != ':') {
      macNoColon[idx++] = *p;
    }
  }
  char topic[64];
  snprintf(topic, sizeof(topic), "device/%s_var/%s", macNoColon, name);
  MqttManager::instance().publish(topic, payload);
}

void ActuatorManager::publishAllStatus() {
  char payload[160];
  size_t pos = 0;

  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
    LOGE("publishAllStatus: mutex timeout\n");
    return;
  }
  for (size_t i = 0; i < kActuatorCoilCount; i++) {
    char states[kActuatorConfirmCount + 1];
    for (uint8_t c = 0; c < kActuatorConfirmCount; c++) {
      states[c] = confirmationsEnabled_[i] ? (confirm_[i][c] ? '1' : '0') : '-';
    }
    states[kActuatorConfirmCount] = '\0';

    int n = snprintf(payload + pos, sizeof(payload) - pos, "%s%u,%u,%s",
                     (i == 0 ? "" : ";"), (unsigned)i,
                     confirmationsEnabled_[i] ? 1 : 0, states);
    if (n < 0 || (size_t)n >= sizeof(payload) - pos) {
      break;
    }
    pos += n;
  }
  xSemaphoreGive(mutex_);

  LOGI("Actuator statusCoils: %s\n", payload);
  publishStatus("statusCoils", payload);
}

void ActuatorManager::triggerWrite(size_t coilIndex, bool value) {
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
    LOGE("triggerWrite: mutex timeout\n");
    return;
  }
  pendingWrite_[coilIndex] = true;
  pendingValue_[coilIndex] = value;
  xSemaphoreGive(mutex_);
}

void ActuatorManager::process() {
  for (size_t i = 0; i < kActuatorCoilCount; i++) {
    bool doWrite = false;
    bool value = false;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
      continue;
    }
    if (pendingWrite_[i]) {
      doWrite = true;
      value = pendingValue_[i];
      pendingWrite_[i] = false;
    }
    xSemaphoreGive(mutex_);

    if (!doWrite) {
      continue;
    }

    uint16_t address = kActuatorCoilAddresses[i];
    // writeCoil es bloqueante (toma el mutex de Modbus); se llama fuera del mutex propio.
    bool ok = ModbusManager::instance().writeCoil(
        kActuatorModbusDeviceIndex, address, value ? "1" : "0");

    if (ok) {
      LOGI("Actuator coil %u (addr %u) -> %s OK\n", (unsigned)i, address, value ? "ON" : "OFF");
      // Los switches conservan su estado (representan la posición en la secuencia).
    } else {
      LOGE("Actuator coil %u (addr %u) write FAILED\n", (unsigned)i, address);
    }
  }
}
