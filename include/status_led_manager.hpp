#pragma once

#include <Arduino.h>

#include <atomic>

#include "app_config.hpp"

/// Condiciones que el LED testigo puede revelar, de mayor a menor prioridad.
///
/// El orden **es** la prioridad: el manager muestra siempre la condición activa
/// de menor valor numérico. Si agregas una, ponla en el lugar que le toca y
/// agrega su patrón en kStatusPatterns (status_led_manager.cpp).
///
/// `Booting` y `BusyMaintenance` no son fallas, pero comparten el LED porque son
/// los dos momentos en que el equipo no debe considerarse "en operación".
enum class DeviceStatus : uint8_t {
  Booting = 0,       ///< setup() en curso; se limpia al crear las tareas
  BusyMaintenance,   ///< OTA o subida de respaldo: tareas suspendidas, no cortar alimentación
  EthDown,           ///< sin enlace Ethernet o sin IP
  MqttDown,          ///< red lista pero sin sesión con el broker
  StorageFault,      ///< SD no disponible o RTC no encontrado
  ModbusFault,       ///< los esclavos Modbus no responden
  Ok,                ///< operación normal (latido); siempre la última
  Count
};

/// LED testigo de estado. Dueño único de kStatusLedPin: ningún otro módulo
/// debe tocar ese GPIO (ver docs/DECISIONS.md).
///
/// Las condiciones se marcan y se limpian por separado (`setStatus`), así que
/// representan **estado**, no eventos: un fallo que ya se recuperó deja de
/// mostrarse sin tener que acordarse de apagarlo. `tick()` avanza el patrón y
/// la llama la tarea `status-led-task` cada kStatusLedTickMs.
///
/// `setStatus` es seguro desde cualquier tarea (máscara atómica). No la llames
/// desde una ISR.
class StatusLedManager {
 public:
  static StatusLedManager &instance();

  /// Configura el GPIO y deja el LED encendido fijo (estado `Booting`).
  void begin();

  /// Marca (`active = true`) o limpia una condición. `DeviceStatus::Ok` se
  /// ignora: es el patrón por defecto cuando no hay ninguna otra activa.
  void setStatus(DeviceStatus status, bool active);

  bool isStatusActive(DeviceStatus status) const;

  /// Condición que el LED está mostrando ahora mismo.
  DeviceStatus currentStatus() const { return current_; }

  /// Avanza el patrón de parpadeo. Solo la llama la tarea del LED.
  void tick();

 private:
  StatusLedManager() = default;

  enum class Phase : uint8_t { Pulse, Gap };

  DeviceStatus dominantStatus() const;
  void restartPattern(DeviceStatus status);
  void enterPhase(Phase phase, uint16_t durationMs);
  void advancePhase();
  void writeLed(bool on);

  std::atomic<uint32_t> mask_{0};  ///< un bit por DeviceStatus activa

  bool initialized_ = false;
  DeviceStatus current_ = DeviceStatus::Count;  ///< fuerza reinicio en el 1er tick
  Phase phase_ = Phase::Gap;
  uint8_t pulsesDone_ = 0;
  uint32_t remainingTicks_ = 0;
};
