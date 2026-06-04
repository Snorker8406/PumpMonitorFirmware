#include "modbus_server_manager.hpp"

#include "log.hpp"

// El Logging.h interno de eModbus define macros LOG_LEVEL_ERROR/INFO/DEBUG que
// chocan con el enum LogLevel de log.hpp. Incluimos log.hpp primero (para que el
// enum se parsee bien) y luego eliminamos esas macros para que LOGE/LOGI/LOGD
// vuelvan a referirse al enum.
#include "ModbusServerETH.h"
#undef LOG_LEVEL_ERROR
#undef LOG_LEVEL_WARN
#undef LOG_LEVEL_INFO
#undef LOG_LEVEL_DEBUG

using namespace Modbus;  // NOLINT  (WRITE_HOLD_REGISTER, WRITE_MULT_REGISTERS, Error codes)

// ── Instancia estática del servidor Modbus TCP (ETH.h / WT32-ETH01) ──
static ModbusServerEthernet s_mbServer;

ModbusServerManager &ModbusServerManager::instance() {
  static ModbusServerManager inst;
  return inst;
}

void ModbusServerManager::enqueueEvent(uint8_t fc, uint16_t address, uint16_t value) {
  if (!mutex_) return;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) return;

  // Guardar valor en el banco de registros si la dirección es válida
  if (address < kModbusServerRegCount) {
    regs_[address] = value;
  }

  size_t next = (head_ + 1) % kEventBufferSize;
  if (next != tail_) {  // Hay espacio (descarta si el buffer está lleno)
    events_[head_].functionCode = fc;
    events_[head_].address = address;
    events_[head_].value = value;
    head_ = next;
  }
  xSemaphoreGive(mutex_);
}

uint16_t ModbusServerManager::registerValue(uint16_t address) const {
  if (address >= kModbusServerRegCount) return 0;
  return regs_[address];
}

uint16_t ModbusServerManager::activeClients() const {
  return s_mbServer.activeClients();
}

void ModbusServerManager::begin() {
  mutex_ = xSemaphoreCreateMutex();
  if (!mutex_) {
    LOGE("Failed to create Modbus server mutex\n");
    return;
  }

  // Worker FC06 (Write Single Holding Register).
  // Request:  serverID, FC, address(2), value(2)
  // Response: eco de la request.
  s_mbServer.registerWorker(kModbusServerUnitId, WRITE_HOLD_REGISTER,
      [](ModbusMessage request) -> ModbusMessage {
        uint16_t address = 0;
        uint16_t value = 0;
        request.get(2, address);
        request.get(4, value);

        if (address >= kModbusServerRegCount) {
          ModbusMessage err;
          err.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
          return err;
        }

        ModbusServerManager::instance().enqueueEvent(WRITE_HOLD_REGISTER, address, value);

        ModbusMessage response;
        response.add(request.getServerID(), request.getFunctionCode(), address, value);
        return response;
      });

  // Worker FC16 (Write Multiple Holding Registers).
  // Request:  serverID, FC, startAddress(2), wordCount(2), byteCount(1), data...
  // Response: serverID, FC, startAddress(2), wordCount(2)
  s_mbServer.registerWorker(kModbusServerUnitId, WRITE_MULT_REGISTERS,
      [](ModbusMessage request) -> ModbusMessage {
        uint16_t address = 0;
        uint16_t words = 0;
        request.get(2, address);
        request.get(4, words);

        if (words == 0 || address + words > kModbusServerRegCount) {
          ModbusMessage err;
          err.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
          return err;
        }

        for (uint16_t i = 0; i < words; ++i) {
          uint16_t value = 0;
          request.get(7 + i * 2, value);  // 7 = offset al primer byte de datos
          ModbusServerManager::instance().enqueueEvent(WRITE_MULT_REGISTERS, address + i, value);
        }

        ModbusMessage response;
        response.add(request.getServerID(), request.getFunctionCode(), address, words);
        return response;
      });

  // Arrancar el servidor: puerto, clientes máx, timeout, core.
  s_mbServer.start(kModbusServerPort, kModbusServerMaxClients, kModbusServerTimeoutMs,
                   kModbusServerTaskCore);

  LOGI("Modbus Server iniciado | UnitID=%u | puerto=%u | maxClientes=%u | regs=%u\n",
       kModbusServerUnitId, kModbusServerPort, kModbusServerMaxClients, kModbusServerRegCount);
}

void ModbusServerManager::process() {
  if (!mutex_) return;

  for (;;) {
    WriteEvent ev;
    bool hasEvent = false;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
      if (tail_ != head_) {
        ev = events_[tail_];
        tail_ = (tail_ + 1) % kEventBufferSize;
        hasEvent = true;
      }
      xSemaphoreGive(mutex_);
    }

    if (!hasEvent) break;

    const char *fcName = (ev.functionCode == WRITE_HOLD_REGISTER) ? "FC06" : "FC16";
    LOGI("Modbus Server RX | %s | reg[%u] = %u (0x%04X)\n",
         fcName, ev.address, ev.value, ev.value);
  }
}
