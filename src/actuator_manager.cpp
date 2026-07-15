#include "actuator_manager.hpp"

#include "eeprom_manager.hpp"
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
  reloadConfig();
  LOGI("ActuatorManager initialized (%u coils, %u confirmations/coil)\n",
       (unsigned)kActuatorCoilCount, (unsigned)kActuatorConfirmCount);
}

void ActuatorManager::reloadConfig() {
  auto &eeprom = EepromManager::instance();
  modbusDeviceIndex_ = eeprom.getActuatorModbusDeviceIndex();
  for (size_t i = 0; i < kActuatorCoilCount; i++) {
    coilOnAddresses_[i]       = eeprom.getActuatorCoilOnAddress(i);
    coilOnValues_[i]          = eeprom.getActuatorCoilOnValue(i);
    coilOffAddresses_[i]      = eeprom.getActuatorCoilOffAddress(i);
    coilOffValues_[i]         = eeprom.getActuatorCoilOffValue(i);
    confirmationsEnabled_[i]  = eeprom.getActuatorCoilEnabled(i);
    confirmAlarmIndex_[i]     = eeprom.getActuatorCoilConfirmAlarmIndex(i);
    confirmManualOn_[i]       = eeprom.getActuatorConfirmManualOn(i);
    confirmManualOff_[i]      = eeprom.getActuatorConfirmManualOff(i);
    confirmRemoteOn_[i]       = eeprom.getActuatorConfirmRemoteOn(i);
    confirmRemoteOff_[i]      = eeprom.getActuatorConfirmRemoteOff(i);
  }
  LOGI("ActuatorManager config loaded (deviceIndex=%u)\n", (unsigned)modbusDeviceIndex_);
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
  // Se omite la publicación SOLO cuando se disparó la escritura Modbus, porque
  // en esos casos el servidor recibe la confirmación vía el coil:
  //   - 110 -> 111 (arranque, triggerOn)
  //   - 001 -> 000 (paro, triggerOff)
  // Si se llega a 111/000 por retroceso ("arrepentirse"), NO se dispara Modbus
  // (p.ej. 100 -> 000 o 011 -> 111), por lo que SÍ se debe notificar el estado.
  if (applied && !triggerOn && !triggerOff) {
    char payload[24];
    snprintf(payload, sizeof(payload), "%u,%s", (unsigned)coilIndex, states);
    publishStatus("coilStatus", payload);
  }
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

bool ActuatorManager::initializeFromAlarms() {
  auto &eeprom = EepromManager::instance();
  uint8_t  almDeviceIndex  = eeprom.getAlarmDeviceIndex();
  uint16_t almStartAddress = eeprom.getAlarmStartAddress();
  uint16_t almCount        = eeprom.getAlarmCount();
  bool     almDiscrete     = eeprom.getAlarmDiscreteInputs();

  // Lectura bloqueante (thread-safe internamente) del rango de alarmas.
  std::vector<bool> almStates;
  if (!ModbusManager::instance().readBooleans(almDeviceIndex, almStartAddress,
                                              almCount, almStates, almDiscrete)) {
    LOGE("ActuatorInit: lectura de alarmas fallida (dev=%u addr=%u count=%u)\n",
         (unsigned)almDeviceIndex, (unsigned)almStartAddress, (unsigned)almCount);
    return false;
  }

  for (size_t i = 0; i < kActuatorCoilCount; i++) {
    uint8_t alarmIdx = confirmAlarmIndex_[i];
    if (alarmIdx >= almStates.size()) {
      LOGW("ActuatorInit: coil %u confirmAlarmIndex %u fuera de rango (alarmCount=%u)\n",
           (unsigned)i, (unsigned)alarmIdx, (unsigned)almStates.size());
      continue;
    }

    bool on = almStates[alarmIdx];

    // Sincronizar los switches al estado real SIN disparar escritura Modbus
    // (el actuador ya está físicamente en ese estado; solo reflejamos/publicamos).
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
      LOGE("ActuatorInit: mutex timeout (coil %u)\n", (unsigned)i);
      continue;
    }
    for (uint8_t c = 0; c < kActuatorConfirmCount; c++) {
      confirm_[i][c] = on;  // 111 si la alarma=1, 000 si la alarma=0
    }
    pendingWrite_[i] = false;  // garantizar que no quede una escritura pendiente

    char states[kActuatorConfirmCount + 1];
    for (uint8_t c = 0; c < kActuatorConfirmCount; c++) {
      states[c] = confirm_[i][c] ? '1' : '0';
    }
    states[kActuatorConfirmCount] = '\0';
    xSemaphoreGive(mutex_);

    LOGI("ActuatorInit: coil %u alarm[%u]=%u -> %s\n",
         (unsigned)i, (unsigned)alarmIdx, on ? 1 : 0, states);

    // Publicar estado por coil (mismo formato que setConfirmation): "index,estados".
    char payload[24];
    snprintf(payload, sizeof(payload), "%u,%s", (unsigned)i, states);
    publishStatus("coilStatus", payload);
  }

  // Publicar el resumen general de todas las coils.
  publishAllStatus();
  return true;
}

void ActuatorManager::handleConfirmationValue(uint16_t value) {
  // Buscar a qué coil y acción corresponde el valor (los valores son únicos por coil).
  int coilIndex = -1;
  bool on = false;
  const char* kind = nullptr;
  for (size_t i = 0; i < kActuatorCoilCount; i++) {
    if (value == confirmManualOn_[i])       { coilIndex = (int)i; on = true;  kind = "ManualON"; }
    else if (value == confirmManualOff_[i]) { coilIndex = (int)i; on = false; kind = "ManualOFF"; }
    else if (value == confirmRemoteOn_[i])  { coilIndex = (int)i; on = true;  kind = "RemoteON"; }
    else if (value == confirmRemoteOff_[i]) { coilIndex = (int)i; on = false; kind = "RemoteOFF"; }
    if (coilIndex >= 0) break;
  }

  if (coilIndex < 0) {
    LOGW("Actuator confirm: valor %u no corresponde a ninguna coil\n", (unsigned)value);
    return;
  }

  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
    LOGE("handleConfirmationValue: mutex timeout\n");
    return;
  }
  // Forzar todos los switches al estado confirmado (111 ON / 000 OFF) sin
  // disparar escrituras Modbus: el estado físico ya sucedió (por eso llegó la
  // confirmación); solo se refleja y publica.
  for (uint8_t c = 0; c < kActuatorConfirmCount; c++) {
    confirm_[coilIndex][c] = on;
  }
  pendingWrite_[coilIndex] = false;

  char states[kActuatorConfirmCount + 1];
  for (uint8_t c = 0; c < kActuatorConfirmCount; c++) {
    states[c] = on ? '1' : '0';
  }
  states[kActuatorConfirmCount] = '\0';
  xSemaphoreGive(mutex_);

  LOGI("Actuator confirm %s (valor %u) -> coil %u = %s\n",
       kind, (unsigned)value, (unsigned)coilIndex, states);

  // Publicar el estado confirmado: "index,111" o "index,000" (mismo formato
  // y topic que los estados intermedios de la secuencia).
  char payload[24];
  snprintf(payload, sizeof(payload), "%u,%s", (unsigned)coilIndex, states);
  publishStatus("coilStatus", payload);
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
    // pendingValue_ indica qué secuencia se completó:
    //   true  = secuencia de ARRANQUE (111)
    //   false = secuencia de PARO     (000)
    bool isOnSequence = false;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
      continue;
    }
    if (pendingWrite_[i]) {
      doWrite = true;
      isOnSequence = pendingValue_[i];
      pendingWrite_[i] = false;
    }
    xSemaphoreGive(mutex_);

    if (!doWrite) {
      continue;
    }

    // El valor a escribir y su dirección dependen SOLO de la configuración del coil,
    // no del flag de secuencia. Arranque (111) usa onAddress/onValue; paro (000)
    // usa offAddress/offValue. No se asume 1/0.
    uint16_t address  = isOnSequence ? coilOnAddresses_[i] : coilOffAddresses_[i];
    bool     writeVal = isOnSequence ? coilOnValues_[i]    : coilOffValues_[i];
    // writeCoil es bloqueante (toma el mutex de Modbus); se llama fuera del mutex propio.
    bool ok = ModbusManager::instance().writeCoil(
        modbusDeviceIndex_, address, writeVal ? "1" : "0");

    if (ok) {
      LOGI("Actuator coil %u (%s addr %u val %u) OK\n", (unsigned)i,
           isOnSequence ? "ON" : "OFF", address, writeVal ? 1 : 0);
      // Los switches conservan su estado (representan la posición en la secuencia).
    } else {
      LOGE("Actuator coil %u (%s addr %u) write FAILED\n", (unsigned)i,
           isOnSequence ? "ON" : "OFF", address);
    }
  }
}
