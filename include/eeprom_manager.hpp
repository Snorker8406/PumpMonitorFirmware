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
  
  // ── Alarmas (lectura de Coils configurable) ──
  // Dispositivo Modbus, dirección inicial, cantidad de coils e intervalo.
  uint8_t  getAlarmDeviceIndex();
  bool     setAlarmDeviceIndex(uint8_t index);
  uint16_t getAlarmStartAddress();
  bool     setAlarmStartAddress(uint16_t address);
  uint16_t getAlarmCount();
  bool     setAlarmCount(uint16_t count);
  // true = FC02 Discrete Inputs; false = FC01 Coils
  bool     getAlarmDiscreteInputs();
  bool     setAlarmDiscreteInputs(bool discreteInputs);
  // Tipos de coil: 1 carácter por coil (A=Alarm, N=Notification, C=Confirmation).
  String   getAlarmCoilsTypes();
  bool     setAlarmCoilsTypes(const char* types);

  // ── Modbus Server (esclavo TCP) ──
  // Parámetros del servidor Modbus persistidos en EEPROM. Se leen en
  // ModbusServerManager::begin(); los cambios se aplican tras reiniciar.
  uint8_t  getServerUnitId();
  bool     setServerUnitId(uint8_t unitId);
  uint16_t getServerPort();
  bool     setServerPort(uint16_t port);
  uint8_t  getServerMaxClients();
  bool     setServerMaxClients(uint8_t maxClients);
  uint32_t getServerTimeoutMs();
  bool     setServerTimeoutMs(uint32_t timeoutMs);
  
  // ── Configuración de red (IP fija / DHCP) ──
  // Si la EEPROM está vacía (sin flag o sin IP guardada) se usa DHCP,
  // independientemente del valor del flag. Los cambios se aplican al reiniciar.
  bool getNetworkUseDhcp();
  bool setNetworkUseDhcp(bool useDhcp);
  IPAddress getNetworkStaticIp();
  IPAddress getNetworkGateway();
  IPAddress getNetworkSubnet();
  IPAddress getNetworkDns();
  bool setNetworkStaticConfig(const IPAddress& ip, const IPAddress& gateway,
                              const IPAddress& subnet, const IPAddress& dns);

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

  // ── Actuadores (configurables, persistidos en EEPROM) ──
  // Se cargan al arrancar; si la EEPROM está vacía se siembran desde
  // kActuatorModbusDeviceIndex / kActuatorCoilOn*/kActuatorCoilOff* / kActuatorConfirmationsEnabled.
  size_t getActuatorModbusDeviceIndex() const;
  uint16_t getActuatorCoilOnAddress(size_t coilIndex) const;
  bool     getActuatorCoilOnValue(size_t coilIndex) const;
  uint16_t getActuatorCoilOffAddress(size_t coilIndex) const;
  bool     getActuatorCoilOffValue(size_t coilIndex) const;
  bool getActuatorCoilEnabled(size_t coilIndex) const;
  uint8_t getActuatorCoilConfirmAlarmIndex(size_t coilIndex) const;
  // Reemplaza toda la configuración de actuadores y la persiste en EEPROM.
  // onAddresses/onValues: dirección y valor a escribir en secuencia de arranque (111).
  // offAddresses/offValues: dirección y valor a escribir en secuencia de paro (000).
  // confirmAlarmIndices: índice en el array de alarmas para confirmación de cada coil.
  bool setActuatorConfig(size_t deviceIndex,
                         const uint16_t* onAddresses, const bool* onValues,
                         const uint16_t* offAddresses, const bool* offValues,
                         const bool* enabled,
                         const uint8_t* confirmAlarmIndices);
  // Restablece la configuración de actuadores a los valores por defecto y persiste.
  bool clearActuatorConfig();
  
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

  // Estructura serializable (POD) para la configuración de cada coil del actuador.
  struct ActuatorCoilStored {
    uint16_t onAddress;
    uint8_t  onValue;          // bool
    uint16_t offAddress;
    uint8_t  offValue;         // bool
    uint8_t  enabled;          // bool
    uint8_t  confirmAlarmIndex; // índice en array de alarmas
  };

  // Carga la configuración de actuadores desde EEPROM (o siembra con defaults).
  void loadActuators();
  // Copia los valores por defecto de app_config a la RAM.
  void seedActuatorsFromDefaults();
  // Persiste la configuración de actuadores actual en RAM hacia EEPROM.
  bool saveActuators();
  
  Preferences prefs_;
  bool initialized_ = false;

  // Lista activa en RAM. Los nombres se guardan en un buffer estable para que
  // ModbusDeviceConfig::modbusModelName apunte a memoria válida.
  ModbusDeviceConfig modbusDevices_[kMaxModbusDevices]{};
  char modbusModelNames_[kMaxModbusDevices][kModbusModelNameLen]{};
  size_t modbusDeviceCount_ = 0;

  // Configuración activa de actuadores en RAM.
  size_t   actuatorDeviceIndex_ = kActuatorModbusDeviceIndex;
  uint16_t actuatorCoilOnAddresses_[kActuatorCoilCount]{};
  bool     actuatorCoilOnValues_[kActuatorCoilCount]{};
  uint16_t actuatorCoilOffAddresses_[kActuatorCoilCount]{};
  bool     actuatorCoilOffValues_[kActuatorCoilCount]{};
  bool     actuatorCoilEnabled_[kActuatorCoilCount]{};
  uint8_t  actuatorCoilConfirmAlarmIndex_[kActuatorCoilCount]{};
  
  static constexpr const char* kNamespace = "pumpmon";
  static constexpr const char* kKeyUrl = "webUrl";
  static constexpr const char* kKeyRTInterval = "rtInterval";
  static constexpr const char* kKeyIVInterval = "ivInterval";
  static constexpr const char* kKeyDeviceID = "deviceId";
  static constexpr const char* kKeyBackupResult = "backupRes";
  static constexpr const char* kKeyModbusCount = "mbDevCount";
  static constexpr const char* kKeyModbusDevs = "mbDevs";
  static constexpr const char* kKeyActDevIdx = "actDevIdx";
  static constexpr const char* kKeyActCoils = "actCoils";
  static constexpr const char* kKeyAlarmDevIdx = "almDevIdx";
  static constexpr const char* kKeyAlarmStart = "almStart";
  static constexpr const char* kKeyAlarmCount = "almCount";
  static constexpr const char* kKeyAlarmFunc = "almFunc";
  static constexpr const char* kKeyAlarmTypes = "almTypes";
  static constexpr const char* kKeyMbSrvUnit = "mbSrvUnit";
  static constexpr const char* kKeyMbSrvPort = "mbSrvPort";
  static constexpr const char* kKeyMbSrvMaxCli = "mbSrvMaxCli";
  static constexpr const char* kKeyMbSrvTout = "mbSrvTout";
  static constexpr const char* kKeyNetDhcp = "netDhcp";
  static constexpr const char* kKeyNetIp = "netIp";
  static constexpr const char* kKeyNetGw = "netGw";
  static constexpr const char* kKeyNetSn = "netSn";
  static constexpr const char* kKeyNetDns = "netDns";
  static constexpr const char* kDefaultUrl = "https://pumpmonitor.agrotecsa.com.mx/";
  static constexpr uint16_t kDefaultRTInterval = 3;
  static constexpr uint16_t kDefaultIVInterval = 60;  // 1 minuto por defecto
  static constexpr int32_t kDefaultDeviceID = 1;
  static constexpr size_t kMaxUrlLength = 128;
};
