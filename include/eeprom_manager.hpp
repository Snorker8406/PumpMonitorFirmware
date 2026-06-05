#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "app_config.hpp"

class EepromManager {
 public:
  static EepromManager &instance();

  void begin();
  
  // Web Service URL
  String getWebServiceURL();
  bool setWebServiceURL(const char* url);
  
  // Real Time Interval (seconds)
  uint16_t getRealTimeIntervalSec();
  bool setRealTimeIntervalSec(uint16_t seconds);
  
  // Instant Values Interval (seconds)
  uint16_t getInstantValuesIntervalSec();
  bool setInstantValuesIntervalSec(uint16_t seconds);
  
  // Device ID
  int32_t getDeviceID();
  bool setDeviceID(int32_t id);
  
  // Backup Result (persistente para publicar después de reinicio)
  String getPendingBackupResult();
  bool setPendingBackupResult(const char* result);
  void clearPendingBackupResult();
  bool hasPendingBackupResult();

  // ── Dispositivos Modbus (configurables, persistidos en EEPROM) ──
  // Lista activa leída desde EEPROM al arrancar. Si la EEPROM está vacía se
  // siembra con kDefaultModbusDevices.
  size_t getModbusDeviceCount() const;
  const ModbusDeviceConfig& getModbusDevice(size_t index) const;
  // Reemplaza la lista completa y la persiste en EEPROM.
  bool setModbusDevices(const ModbusDeviceConfig* devices, size_t count);
  // Vacía la lista de dispositivos Modbus (count = 0) y la persiste en EEPROM.
  bool clearModbusDevices();
  
  // Resetear a valores por defecto
  void resetToDefaults();
  
 private:
  EepromManager() = default;

  // Estructura serializable (POD, sin punteros) para guardar en EEPROM.
  struct ModbusDeviceConfigStored {
    uint8_t ip[4];
    uint8_t unitId;
    uint16_t startReg;
    uint16_t totalRegs;
    uint8_t regType;     // ModbusRegisterType
    uint8_t swapWords;   // bool
    uint8_t modbusModelId;
    char modbusModelName[kModbusModelNameLen];
  };

  // Carga la lista desde EEPROM (o siembra con defaults si está vacía).
  void loadModbusDevices();
  // Copia kDefaultModbusDevices a la lista en RAM.
  void seedModbusDevicesFromDefaults();
  // Persiste la lista actual en RAM hacia EEPROM.
  bool saveModbusDevices();
  
  Preferences prefs_;
  bool initialized_ = false;

  // Lista activa en RAM. Los nombres se guardan en un buffer estable para que
  // ModbusDeviceConfig::modbusModelName apunte a memoria válida.
  ModbusDeviceConfig modbusDevices_[kMaxModbusDevices]{};
  char modbusModelNames_[kMaxModbusDevices][kModbusModelNameLen]{};
  size_t modbusDeviceCount_ = 0;
  
  static constexpr const char* kNamespace = "pumpmon";
  static constexpr const char* kKeyUrl = "webUrl";
  static constexpr const char* kKeyRTInterval = "rtInterval";
  static constexpr const char* kKeyIVInterval = "ivInterval";
  static constexpr const char* kKeyDeviceID = "deviceId";
  static constexpr const char* kKeyBackupResult = "backupRes";
  static constexpr const char* kKeyModbusCount = "mbDevCount";
  static constexpr const char* kKeyModbusDevs = "mbDevs";
  static constexpr const char* kDefaultUrl = "https://pumpmonitor.agrotecsa.com.mx/";
  static constexpr uint16_t kDefaultRTInterval = 3;
  static constexpr uint16_t kDefaultIVInterval = 60;  // 1 minuto por defecto
  static constexpr int32_t kDefaultDeviceID = 1;
  static constexpr size_t kMaxUrlLength = 128;
};
