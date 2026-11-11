#pragma once

#include <Arduino.h>

#include "app_config.hpp"

// Control de actuadores mediante Coils Modbus con confirmaciones.
//
// Cada coil tiene kActuatorConfirmCount booleanos de confirmación. Cuando el
// mecanismo de confirmaciones está activo, el último paso de la secuencia
// (110 -> 111 o 001 -> 000) no mueve el switch: escribe el coil y espera la
// confirmación que el PLC escribe en reg[0] del Modbus Server. Solo esa
// confirmación lleva el estado a 111 / 000.
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
  //   - Pedir 111 (desde 110) dispara la escritura del coil en ON.
  //   - Pedir 000 (desde 001) dispara la escritura del coil en OFF.
  // En ambos casos el estado se queda en 110 / 001 hasta que llegue la confirmación
  // (handleConfirmationValue) y, mientras tanto, se rechaza cualquier otro cambio del
  // coil. Si la escritura falla o vence kActuatorConfirmTimeoutMs, se desbloquea en
  // ese mismo estado y se publica, para poder reintentar o retroceder.
  // Tras cada cambio aplicado publica "index,estados" (ej: "1,110"), salvo al disparar
  // la escritura: ahí el cliente espera la confirmación.
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

  // Pide a la task del actuador que reintente initializeFromAlarms() sin esperar
  // al backoff en curso (y que lo repita aunque ya se hubiera completado, porque
  // el rango de alarmas o los esclavos cambiaron). Llamar tras alarmConfig /
  // saveModbusDevices. takeInitRetryRequest() lo consume la task.
  void requestInitRetry() { initRetryRequested_ = true; }
  bool takeInitRetryRequest() {
    if (!initRetryRequested_) return false;
    initRetryRequested_ = false;
    return true;
  }

  // Procesa un valor de confirmación recibido en reg[0] del Modbus Server.
  // Si el valor coincide con ConfirmManualON/ConfirmManualOFF/ConfirmRemoteON/
  // ConfirmRemoteOFF de alguna coil, fuerza sus switches a 111 (ON) o 000 (OFF),
  // termina la espera de ese coil y publica el estado confirmado ("index,111" /
  // "index,000") en coilStatus. Se aplica siempre, aunque no haya espera en curso
  // (arranque manual, confirmación tardía). El valor kActuatorConfirmValueUnset se ignora.
  void handleConfirmationValue(uint16_t value);

  // Procesa escrituras pendientes; llamado desde la task del actuador.
  void process();

 private:
  ActuatorManager() = default;
  void triggerWrite(size_t coilIndex, bool value);
  // Publica en el topic device/{MAC}_var/{name}.
  void publishStatus(const char* name, const char* payload);
  // Publica el estado actual de un coil en coilStatus ("index,estados").
  void publishCoilStatus(size_t coilIndex);
  // Avisa si un valor de confirmación se repite: el primer coil que coincide se
  // llevaría la confirmación del otro.
  void warnDuplicateConfirmValues() const;

  SemaphoreHandle_t mutex_{nullptr};
  volatile bool confirmationsEnabled_[kActuatorCoilCount]{};
  // Escrito por la task MQTT, leído por la task del actuador. Un bool alineado es
  // atómico en el ESP32; perder una carrera solo adelanta o atrasa un reintento.
  volatile bool initRetryRequested_{false};

  // Configuración cargada desde EEPROM.
  uint16_t coilOnAddresses_[kActuatorCoilCount]{};
  bool     coilOnValues_[kActuatorCoilCount]{};
  uint16_t coilOffAddresses_[kActuatorCoilCount]{};
  bool     coilOffValues_[kActuatorCoilCount]{};
  size_t modbusDeviceIndex_ = kActuatorModbusDeviceIndex;

  bool confirm_[kActuatorCoilCount][kActuatorConfirmCount]{};
  volatile bool pendingWrite_[kActuatorCoilCount]{};
  bool pendingValue_[kActuatorCoilCount]{};
  // Secuencia completada, a la espera de la confirmación del PLC. Se marca al encolar
  // la escritura (no después de escribir) para que un FC06 inmediato no se adelante.
  bool awaitingConfirm_[kActuatorCoilCount]{};
  uint32_t awaitTicks_[kActuatorCoilCount]{};  // Ciclos de process() desde la escritura
  uint8_t confirmAlarmIndex_[kActuatorCoilCount]{};
  // Valores de confirmación (0-99, únicos por coil) recibidos vía Modbus Server.
  uint8_t confirmManualOn_[kActuatorCoilCount]{};
  uint8_t confirmManualOff_[kActuatorCoilCount]{};
  uint8_t confirmRemoteOn_[kActuatorCoilCount]{};
  uint8_t confirmRemoteOff_[kActuatorCoilCount]{};
};
