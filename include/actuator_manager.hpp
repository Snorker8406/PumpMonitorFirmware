#pragma once

#include <Arduino.h>

#include "app_config.hpp"

// Control de actuadores mediante Coils Modbus con confirmaciones.
//
// Cada coil tiene kActuatorConfirmCount booleanos de confirmación. Cuando el
// mecanismo de confirmaciones está activo, la escritura del coil (ON) solo se
// dispara cuando las primeras confirmaciones están en 1 y la última pasa a 1.
//
// La escritura Modbus real (bloqueante) se realiza en la task del actuador
// (process()), no dentro del callback MQTT.
class ActuatorManager {
 public:
  static ActuatorManager &instance();

  void begin();

  // Recarga la configuración (deviceIndex, direcciones y enabled) desde EEPROM.
  // Se llama al inicio y tras actualizar la config vía MQTT.
  void reloadConfig();

  // Activar/desactivar el mecanismo de confirmaciones de un coil concreto.
  void setConfirmationsEnabled(size_t coilIndex, bool enabled);
  bool confirmationsEnabled(size_t coilIndex) const;

  // Establecer una confirmación (switch) de un coil (llamado desde el callback MQTT).
  // Los 3 switches recorren una secuencia cíclica, cambiando un solo switch por paso:
  //   000 -> 100 -> 110 -> 111 -> 011 -> 001 -> 000
  // Se puede avanzar o retroceder (arrepentirse). Solo transiciones adyacentes son válidas.
  //   - Llegar a 111 (desde 110) dispara la escritura del coil en ON.
  //   - Llegar a 000 (desde 001) dispara la escritura del coil en OFF.
  // Tras cada intento publica el estado del coil: payload "index,estados" (ej: "1,110").
  void setConfirmation(size_t coilIndex, uint8_t confirmIndex, bool value);

  // Solicitar escritura directa de un coil (topic setCoil).
  // Solo aplica cuando las confirmaciones del coil están desactivadas.
  void requestCoil(size_t coilIndex, bool value);

  // Publica el estado de todas las coils en formato compacto:
  //   "index,enabled,estados;..."  (estados = "---" si confirmaciones desactivadas)
  void publishAllStatus();

  // Sincroniza el estado inicial de los actuadores leyendo las alarmas por Modbus.
  // Para cada coil usa su confirmAlarmIndex: si el bit de alarma asociado es 1,
  // todos sus switches quedan en 111; si es 0, en 000. NO dispara escrituras
  // Modbus (solo refleja el estado físico real ya existente) y publica el estado
  // de cada coil y el resumen general. Devuelve true si la lectura de alarmas
  // tuvo éxito. Llamar una vez al arranque cuando las conexiones estén listas.
  bool initializeFromAlarms();

  // Procesa un valor de confirmación recibido en reg[0] del Modbus Server.
  // Si el valor coincide con ConfirmManualON/ConfirmManualOFF/ConfirmRemoteON/
  // ConfirmRemoteOFF de alguna coil, fuerza sus switches a 111 (ON) o 000 (OFF)
  // y publica el estado confirmado ("index,111" / "index,000") en coilStatus.
  void handleConfirmationValue(uint16_t value);

  // Procesa escrituras pendientes; llamado desde la task del actuador.
  void process();

 private:
  ActuatorManager() = default;
  void triggerWrite(size_t coilIndex, bool value);
  // Publica en el topic device/{MAC}_var/{name}.
  void publishStatus(const char* name, const char* payload);

  SemaphoreHandle_t mutex_{nullptr};
  volatile bool confirmationsEnabled_[kActuatorCoilCount]{};

  // Configuración cargada desde EEPROM.
  uint16_t coilOnAddresses_[kActuatorCoilCount]{};
  bool     coilOnValues_[kActuatorCoilCount]{};
  uint16_t coilOffAddresses_[kActuatorCoilCount]{};
  bool     coilOffValues_[kActuatorCoilCount]{};
  size_t modbusDeviceIndex_ = kActuatorModbusDeviceIndex;

  bool confirm_[kActuatorCoilCount][kActuatorConfirmCount]{};
  volatile bool pendingWrite_[kActuatorCoilCount]{};
  bool pendingValue_[kActuatorCoilCount]{};
  uint8_t confirmAlarmIndex_[kActuatorCoilCount]{};
  // Valores de confirmación (0-99, únicos por coil) recibidos vía Modbus Server.
  uint8_t confirmManualOn_[kActuatorCoilCount]{};
  uint8_t confirmManualOff_[kActuatorCoilCount]{};
  uint8_t confirmRemoteOn_[kActuatorCoilCount]{};
  uint8_t confirmRemoteOff_[kActuatorCoilCount]{};
};
