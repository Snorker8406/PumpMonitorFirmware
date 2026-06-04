#pragma once

#include <Arduino.h>

#include "app_config.hpp"

// Servidor (esclavo) Modbus TCP.
//
// Este device escucha en kModbusServerPort y permite que dispositivos externos
// (maestros Modbus) escriban Holding Registers (FC06 / FC16). Cada escritura
// recibida se almacena en un buffer y la task del servidor la imprime por
// consola mediante process().
//
// Los workers de eModbus se ejecutan en el contexto interno de la librería; por
// eso solo guardan el evento en un buffer protegido por mutex y NO imprimen
// directamente (el logging se hace en la task).
class ModbusServerManager {
 public:
  static ModbusServerManager &instance();

  // Inicializa el buffer y arranca el servidor Modbus TCP.
  void begin();

  // Imprime por consola las escrituras recibidas pendientes.
  // Llamado periódicamente desde la task del servidor.
  void process();

  // Devuelve el valor actual de un registro expuesto (0 si fuera de rango).
  uint16_t registerValue(uint16_t address) const;

  // Número de clientes Modbus conectados en este momento.
  uint16_t activeClients() const;

 private:
  ModbusServerManager() = default;

  // Estructura de un evento de escritura recibido.
  struct WriteEvent {
    uint8_t functionCode;  // FC06 o FC16
    uint16_t address;      // Dirección del registro escrito
    uint16_t value;        // Valor escrito
  };

  // Encola un evento de escritura (llamado desde los workers de eModbus).
  void enqueueEvent(uint8_t fc, uint16_t address, uint16_t value);

  static constexpr size_t kEventBufferSize = 32;

  SemaphoreHandle_t mutex_{nullptr};
  WriteEvent events_[kEventBufferSize]{};
  volatile size_t head_{0};  // Índice de escritura (productor: workers)
  volatile size_t tail_{0};  // Índice de lectura (consumidor: process)

  // Banco de registros expuestos al maestro externo.
  uint16_t regs_[kModbusServerRegCount]{};
};
