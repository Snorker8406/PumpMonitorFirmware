#include "status_led_manager.hpp"

#include "log.hpp"

namespace {

/// Patrón de parpadeo de cada condición, en el mismo orden que DeviceStatus.
///
/// Los códigos de falla arrancan en 2 destellos a propósito: un grupo de 1
/// destello se confunde con el latido de operación normal, y este LED se lee a
/// oscuras en la caseta del pozo.
struct StatusPattern {
  uint8_t pulses;   ///< destellos del grupo (0 = encendido fijo)
  uint16_t onMs;    ///< duración de cada destello
  uint16_t offMs;   ///< separación entre destellos del grupo (ignorada si pulses <= 1)
  uint16_t gapMs;   ///< pausa después del grupo
};

constexpr StatusPattern kStatusPatterns[] = {
    // Booting: encendido fijo hasta que las tareas arrancan
    {0, 0, 0, 0},
    // BusyMaintenance: parpadeo rápido continuo
    {1, kStatusLedOtaBlinkMs, 0, kStatusLedOtaBlinkMs},
    // EthDown: 2 destellos
    {2, kStatusLedPulseOnMs, kStatusLedPulseOffMs, kStatusLedGroupPauseMs},
    // MqttDown: 3 destellos
    {3, kStatusLedPulseOnMs, kStatusLedPulseOffMs, kStatusLedGroupPauseMs},
    // StorageFault: 4 destellos
    {4, kStatusLedPulseOnMs, kStatusLedPulseOffMs, kStatusLedGroupPauseMs},
    // ModbusFault: 5 destellos
    {5, kStatusLedPulseOnMs, kStatusLedPulseOffMs, kStatusLedGroupPauseMs},
    // Ok: latido corto y pausa larga
    {1, kStatusLedHeartbeatOnMs, 0, kStatusLedHeartbeatPauseMs},
};

static_assert(sizeof(kStatusPatterns) / sizeof(kStatusPatterns[0]) ==
                  static_cast<size_t>(DeviceStatus::Count),
              "Falta el patrón de parpadeo de alguna DeviceStatus");

constexpr const char *kStatusNames[] = {
    "Booting", "BusyMaintenance", "EthDown", "MqttDown",
    "StorageFault", "ModbusFault", "Ok",
};

static_assert(sizeof(kStatusNames) / sizeof(kStatusNames[0]) ==
                  static_cast<size_t>(DeviceStatus::Count),
              "Falta el nombre de alguna DeviceStatus");

constexpr uint32_t statusBit(DeviceStatus status) {
  return 1u << static_cast<uint8_t>(status);
}

}  // namespace

StatusLedManager &StatusLedManager::instance() {
  static StatusLedManager inst;
  return inst;
}

void StatusLedManager::begin() {
  if (initialized_) {
    LOGW("Status LED already initialized\n");
    return;
  }

  pinMode(kStatusLedPin, OUTPUT);
  writeLed(false);
  initialized_ = true;

  mask_.fetch_or(statusBit(DeviceStatus::Booting), std::memory_order_relaxed);
  restartPattern(DeviceStatus::Booting);

  LOGI("Status LED initialized (GPIO%d, %s)\n", kStatusLedPin,
       kStatusLedActiveLow ? "activo-bajo" : "activo-alto");
}

void StatusLedManager::setStatus(DeviceStatus status, bool active) {
  // Ok no se marca: es el patrón por defecto cuando no hay nada más activo.
  if (status == DeviceStatus::Ok || status == DeviceStatus::Count) return;

  const uint32_t bit = statusBit(status);
  const uint32_t before = active
                              ? mask_.fetch_or(bit, std::memory_order_relaxed)
                              : mask_.fetch_and(~bit, std::memory_order_relaxed);

  // Log solo en el cambio de flanco: estas llamadas vienen de bucles de tarea.
  if (((before & bit) != 0) != active) {
    LOGD("Status LED: %s %s\n", kStatusNames[static_cast<uint8_t>(status)],
         active ? "ON" : "OFF");
  }
}

bool StatusLedManager::isStatusActive(DeviceStatus status) const {
  if (status == DeviceStatus::Ok) {
    return mask_.load(std::memory_order_relaxed) == 0;
  }
  if (status == DeviceStatus::Count) return false;
  return (mask_.load(std::memory_order_relaxed) & statusBit(status)) != 0;
}

void StatusLedManager::tick() {
  if (!initialized_) return;

  const DeviceStatus dominant = dominantStatus();
  if (dominant != current_) {
    restartPattern(dominant);
    return;
  }

  if (remainingTicks_ > 1) {
    remainingTicks_--;
    return;
  }

  advancePhase();
}

DeviceStatus StatusLedManager::dominantStatus() const {
  const uint32_t mask = mask_.load(std::memory_order_relaxed);
  // El orden del enum es la prioridad: gana el bit más bajo que esté activo.
  for (uint8_t i = 0; i < static_cast<uint8_t>(DeviceStatus::Ok); i++) {
    if (mask & (1u << i)) return static_cast<DeviceStatus>(i);
  }
  return DeviceStatus::Ok;
}

void StatusLedManager::restartPattern(DeviceStatus status) {
  current_ = status;
  pulsesDone_ = 0;

  const StatusPattern &pattern = kStatusPatterns[static_cast<uint8_t>(status)];
  if (pattern.pulses == 0) {
    enterPhase(Phase::Pulse, kStatusLedGroupPauseMs);  // encendido fijo
    return;
  }

  pulsesDone_ = 1;
  enterPhase(Phase::Pulse, pattern.onMs);
}

void StatusLedManager::enterPhase(Phase phase, uint16_t durationMs) {
  phase_ = phase;
  writeLed(phase == Phase::Pulse);

  const uint32_t ticks = (durationMs + kStatusLedTickMs - 1) / kStatusLedTickMs;
  remainingTicks_ = ticks > 0 ? ticks : 1;
}

void StatusLedManager::advancePhase() {
  const StatusPattern &pattern = kStatusPatterns[static_cast<uint8_t>(current_)];

  // Encendido fijo: solo re-afirma la salida y sigue esperando.
  if (pattern.pulses == 0) {
    enterPhase(Phase::Pulse, kStatusLedGroupPauseMs);
    return;
  }

  if (phase_ == Phase::Pulse) {
    if (pulsesDone_ < pattern.pulses) {
      enterPhase(Phase::Gap, pattern.offMs);  // separación dentro del grupo
    } else {
      pulsesDone_ = 0;
      enterPhase(Phase::Gap, pattern.gapMs);  // pausa de fin de grupo
    }
    return;
  }

  // Venimos de un apagado: toca el siguiente destello.
  pulsesDone_++;
  enterPhase(Phase::Pulse, pattern.onMs);
}

void StatusLedManager::writeLed(bool on) {
  digitalWrite(kStatusLedPin, (on != kStatusLedActiveLow) ? HIGH : LOW);
}
