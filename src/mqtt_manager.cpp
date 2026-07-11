#include "mqtt_manager.hpp"

#include <Arduino.h>
#include <cstring>

#include "log.hpp"
#include "network_manager.hpp"
#include "eeprom_manager.hpp"
#include "rtc_manager.hpp"
#include "app_config.hpp"
#include "ota_manager.hpp"
#include "sd_manager.hpp"
#include "modbus_manager.hpp"
#include "actuator_manager.hpp"

// Forward declarations de funciones de control de Real Time
extern void startRealTimeMode(uint32_t durationSeconds);

namespace {
constexpr char kMqttClientIdPrefix[] = "pump-monitor";

// Helper para obtener la hora del RTC como string formateado
String getDeviceTimeString() {
  auto &rtc = RtcManager::instance();
  if (rtc.isAvailable()) {
    DateTime now = rtc.now();
    char timeBuffer[32];
    snprintf(timeBuffer, sizeof(timeBuffer), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    return String(timeBuffer);
  }
  return "RTC_NOT_AVAILABLE";
}

// Helper para construir JSON con todas las variables del dispositivo
void buildAllVariablesJson(char* buffer, size_t bufferSize, const char* macNoColon) {
  auto &eeprom = EepromManager::instance();
  
  String webServiceUrl = eeprom.getWebServiceURL();
  uint16_t rtInterval = eeprom.getRealTimeIntervalSec();
  uint16_t ivInterval = eeprom.getInstantValuesIntervalSec();
  int32_t deviceId = eeprom.getDeviceID();
  String deviceTime = getDeviceTimeString();
  
  snprintf(buffer, bufferSize,
           "{\"mac\":\"%s\",\"webService\":\"%s\",\"firmwareVersion\":\"%s\",\"realTimeIntervalSec\":%u,\"instantValuesIntervalSec\":%u,\"deviceId\":%d,\"deviceTime\":\"%s\"}",
           macNoColon,
           webServiceUrl.c_str(),
           kFirmwareVersion,
           rtInterval,
           ivInterval,
           deviceId,
           deviceTime.c_str());
}

// Construye el payload compacto de dispositivos Modbus en el buffer dado.
// Formato: ip,unitId,startReg,totalRegs,regType,swapWords,slaveId,slaveName;...
// regType: 0=HOLDING_REGISTER, 1=INPUT_REGISTER
void buildModbusDevicesString(char* buffer, size_t bufferSize) {
  auto &eeprom = EepromManager::instance();
  size_t count = eeprom.getModbusDeviceCount();
  buffer[0] = '\0';
  size_t offset = 0;
  for (size_t i = 0; i < count && offset < bufferSize - 1; ++i) {
    const ModbusDeviceConfig& d = eeprom.getModbusDevice(i);
    uint8_t rt = static_cast<uint8_t>(d.regType);  // 3=HOLDING_REGISTER, 4=INPUT_REGISTER
    int written = snprintf(buffer + offset, bufferSize - offset,
                           "%s%u.%u.%u.%u,%u,%u,%u,%u,%u,%u,%s",
                           (i > 0) ? ";" : "",
                           d.ip[0], d.ip[1], d.ip[2], d.ip[3],
                           d.unitId, d.startReg, d.totalRegs,
                           rt, (unsigned)(d.swapWords ? 1 : 0),
                           d.modbusSlaveId,
                           d.modbusSlaveName ? d.modbusSlaveName : "");
    if (written > 0) offset += (size_t)written;
  }
}

// Construye el payload compacto de los actuadores (coils) en el buffer dado.
// Formato: deviceIndex;coilIndex,onAddr,onVal,offAddr,offVal,enabled,confirmAlarmIdx;...
// Ej: "1;0,0,1,0,0,1,0;1,1,1,1,0,1,1;2,2,1,2,0,0,2;3,3,1,3,0,1,3"
void buildActuatorsString(char* buffer, size_t bufferSize) {
  auto &eeprom = EepromManager::instance();
  int n = snprintf(buffer, bufferSize, "%u", (unsigned)eeprom.getActuatorModbusDeviceIndex());
  size_t offset = (n > 0) ? (size_t)n : 0;
  for (size_t i = 0; i < kActuatorCoilCount && offset < bufferSize - 1; ++i) {
    int written = snprintf(buffer + offset, bufferSize - offset,
                           ";%u,%u,%u,%u,%u,%u,%u",
                           (unsigned)i,
                           (unsigned)eeprom.getActuatorCoilOnAddress(i),
                           (unsigned)(eeprom.getActuatorCoilOnValue(i) ? 1 : 0),
                           (unsigned)eeprom.getActuatorCoilOffAddress(i),
                           (unsigned)(eeprom.getActuatorCoilOffValue(i) ? 1 : 0),
                           (unsigned)(eeprom.getActuatorCoilEnabled(i) ? 1 : 0),
                           (unsigned)eeprom.getActuatorCoilConfirmAlarmIndex(i));
    if (written > 0) offset += (size_t)written;
  }
}
}

MqttManager &MqttManager::instance() {
  static MqttManager inst;
  return inst;
}

MqttManager::MqttManager() : client_(secureClient_) {}

void MqttManager::begin() {
  secureClient_.setCACert(kMqttCaCert);
  client_.setBufferSize(kMqttMaxPacketSize);  // DEBE ser antes de setServer
  client_.setServer(kMqttBrokerHost, kMqttBrokerPort);
  client_.setKeepAlive(kMqttKeepAliveSec);
  client_.setCallback(messageCallback);
}

void MqttManager::messageCallback(char* topic, byte* payload, unsigned int length) {
  // Log del mensaje recibido
  char msg[256];
  unsigned int len = (length < sizeof(msg) - 1) ? length : sizeof(msg) - 1;
  memcpy(msg, payload, len);
  msg[len] = '\0';
  LOGI("MQTT msg [%s]: %s\n", topic, msg);
  
  auto &eeprom = EepromManager::instance();
  
  // Procesar topic webService (actualizar)
  if (strstr(topic, "/webService") != nullptr) {
    if (eeprom.setWebServiceURL(msg)) {
      LOGI("MQTT: URL updated via MQTT\n");
    } else {
      LOGE("MQTT: Failed to update URL\n");
    }
  }
  // Procesar topic realTimeIntervalSec (actualizar)
  else if (strstr(topic, "/realTimeIntervalSec") != nullptr) {
    uint16_t interval = atoi(msg);
    if (eeprom.setRealTimeIntervalSec(interval)) {
      LOGI("MQTT: Real Time Interval updated via MQTT\n");
    } else {
      LOGE("MQTT: Failed to update Real Time Interval\n");
    }
  }
  // Procesar topic instantValuesIntervalSec (actualizar)
  else if (strstr(topic, "/instantValuesIntervalSec") != nullptr) {
    uint16_t interval = atoi(msg);
    if (eeprom.setInstantValuesIntervalSec(interval)) {
      LOGI("MQTT: Instant Values Interval updated via MQTT\n");
    } else {
      LOGE("MQTT: Failed to update Instant Values Interval\n");
    }
  }
  // Procesar topic deviceId (actualizar)
  else if (strstr(topic, "/deviceId") != nullptr) {
    int32_t deviceId = atoi(msg);
    if (eeprom.setDeviceID(deviceId)) {
      LOGI("MQTT: Device ID updated via MQTT\n");
    } else {
      LOGE("MQTT: Failed to update Device ID\n");
    }
  }
  // Procesar topic alarmConfig: configurar lectura de coils de alarmas
  // Payload: deviceIndex,startAddress,count[,funcCode[,coilsTypes]]
  //   funcCode opcional: 1 = Coils (FC01), 2 = Discrete Inputs (FC02)
  //   coilsTypes opcional: 1 carácter por coil (A=Alarm, N=Notification, C=Confirmation)
  //   Ej: "1,0,4,2,CNAA"
  // Solo se aplican los campos válidos; cualquier inválido se rechaza.
  else if (strstr(topic, "/alarmConfig") != nullptr) {
    unsigned int devIdx, startAddr, count, funcCode = 0;
    char coilsTypes[64] = {0};
    int parsed = sscanf(msg, "%u,%u,%u,%u,%63[A-Za-z]",
                        &devIdx, &startAddr, &count, &funcCode, coilsTypes);
    if (parsed < 3) {
      LOGE("MQTT: Invalid alarmConfig format. Expected: deviceIndex,startAddress,count[,funcCode[,coilsTypes]]\n");
      return;
    }
    bool ok = true;
    ok &= eeprom.setAlarmDeviceIndex((uint8_t)devIdx);
    ok &= eeprom.setAlarmStartAddress((uint16_t)startAddr);
    ok &= eeprom.setAlarmCount((uint16_t)count);
    if (parsed >= 4) {
      if (funcCode == 1 || funcCode == 2) {
        ok &= eeprom.setAlarmDiscreteInputs(funcCode == 2);
      } else {
        LOGE("MQTT: Invalid alarm funcCode (use 1=Coils or 2=DiscreteInputs)\n");
        ok = false;
      }
    }
    if (parsed >= 5) {
      ok &= eeprom.setAlarmCoilsTypes(coilsTypes);
    }
    if (ok) {
      LOGI("MQTT: Alarm config updated (dev=%u start=%u count=%u func=%s types=%s)\n",
           devIdx, startAddr, count,
           (parsed >= 4) ? (funcCode == 2 ? "FC02" : "FC01") : "unchanged",
           (parsed >= 5) ? coilsTypes : "unchanged");
    } else {
      LOGE("MQTT: Alarm config partially rejected (valores fuera de rango)\n");
    }
  }
  // Procesar topic serverConfig: configurar el servidor Modbus TCP (esclavo)
  // Payload: unitId,port,maxClients,timeoutMs
  //   unitId:     1-247  (Server/Unit ID que atiende este device)
  //   port:       1-65535 (puerto TCP de escucha; estándar = 502)
  //   maxClients: 1-8    (conexiones simultáneas)
  //   timeoutMs:  1000-120000 (timeout de inactividad por cliente, ms)
  //   Ej: "2,502,4,10000"
  // Los cambios se persisten en EEPROM y se aplican al reiniciar el dispositivo.
  else if (strstr(topic, "/saveModbusServerConfig") != nullptr) {
    unsigned int unitId, port, maxClients, timeoutMs;
    int parsed = sscanf(msg, "%u,%u,%u,%u", &unitId, &port, &maxClients, &timeoutMs);
    if (parsed != 4) {
      LOGE("MQTT: Invalid serverConfig format. Expected: unitId,port,maxClients,timeoutMs\n");
      return;
    }
    bool ok = true;
    ok &= eeprom.setServerUnitId((uint8_t)unitId);
    ok &= eeprom.setServerPort((uint16_t)port);
    ok &= eeprom.setServerMaxClients((uint8_t)maxClients);
    ok &= eeprom.setServerTimeoutMs((uint32_t)timeoutMs);
    if (ok) {
      LOGI("MQTT: Modbus Server config updated (unitId=%u port=%u maxClients=%u timeout=%u ms). Reinicie para aplicar.\n",
           unitId, port, maxClients, timeoutMs);
    } else {
      LOGE("MQTT: Modbus Server config partially rejected (valores fuera de rango)\n");
    }
  }
  // Procesar getModbusServerConfig: devolver la configuracion actual del servidor Modbus TCP
  // Response topic: device/{MAC}_var/modbusServerConfig
  // Payload: unitId,port,maxClients,timeoutMs
  else if (strstr(topic, "/getModbusServerConfig") != nullptr) {
    const char *macColoned = NetworkManager::instance().macString();
    char macNoColon[13] = {0};
    int idx = 0;
    for (const char *p = macColoned; *p && idx < 12; ++p) {
      if (*p != ':') macNoColon[idx++] = *p;
    }

    char srvBuf[48];
    snprintf(srvBuf, sizeof(srvBuf), "%u,%u,%u,%lu",
             eeprom.getServerUnitId(), eeprom.getServerPort(),
             eeprom.getServerMaxClients(), (unsigned long)eeprom.getServerTimeoutMs());

    char responseTopic[64];
    snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/modbusServerConfig", macNoColon);

    auto &mqtt = MqttManager::instance();
    if (mqtt.publish(responseTopic, srvBuf)) {
      LOGI("MQTT: Published serverConfig (%s)\n", srvBuf);
    } else {
      LOGE("MQTT: Failed to publish serverConfig\n");
    }
  }
  // Procesar saveNetworkConfig: configurar red (DHCP o IP fija)
  // Payload: "1"                              -> DHCP
  //          "0,ip[,gateway[,subnet[,dns]]]"  -> IP fija
  //   Ej: "0,192.168.1.120,192.168.1.254,255.255.255.0,8.8.8.8"
  // Los campos omitidos conservan el valor guardado (o el default).
  // Se persiste en EEPROM y el ESP se reinicia para aplicar los cambios.
  else if (strstr(topic, "/saveNetworkConfig") != nullptr) {
    unsigned int dhcpFlag;
    char ipStr[16] = {0}, gwStr[16] = {0}, snStr[16] = {0}, dnsStr[16] = {0};
    int parsed = sscanf(msg, "%u,%15[0-9.],%15[0-9.],%15[0-9.],%15[0-9.]",
                        &dhcpFlag, ipStr, gwStr, snStr, dnsStr);
    if (parsed < 1 || dhcpFlag > 1) {
      LOGE("MQTT: Invalid networkConfig format. Expected: 1 (DHCP) or 0,ip[,gateway[,subnet[,dns]]]\n");
      return;
    }
    if (dhcpFlag == 1) {
      if (eeprom.setNetworkUseDhcp(true)) {
        LOGI("MQTT: Network config updated (DHCP). Restarting ESP to apply...\n");
        delay(500);
        ESP.restart();
      } else {
        LOGE("MQTT: Failed to update network config\n");
      }
      return;
    }
    // IP fija: se requiere al menos la IP
    if (parsed < 2) {
      LOGE("MQTT: Static network config requires an IP. Expected: 0,ip[,gateway[,subnet[,dns]]]\n");
      return;
    }
    IPAddress ip, gateway = eeprom.getNetworkGateway();
    IPAddress subnet = eeprom.getNetworkSubnet(), dns = eeprom.getNetworkDns();
    if (!ip.fromString(ipStr)) {
      LOGE("MQTT: Invalid static IP: %s\n", ipStr);
      return;
    }
    if (parsed >= 3 && !gateway.fromString(gwStr)) {
      LOGE("MQTT: Invalid gateway: %s\n", gwStr);
      return;
    }
    if (parsed >= 4 && !subnet.fromString(snStr)) {
      LOGE("MQTT: Invalid subnet: %s\n", snStr);
      return;
    }
    if (parsed >= 5 && !dns.fromString(dnsStr)) {
      LOGE("MQTT: Invalid DNS: %s\n", dnsStr);
      return;
    }
    bool ok = eeprom.setNetworkStaticConfig(ip, gateway, subnet, dns);
    ok &= eeprom.setNetworkUseDhcp(false);
    if (ok) {
      LOGI("MQTT: Network config updated (Static %s). Restarting ESP to apply...\n",
           ip.toString().c_str());
      delay(500);
      ESP.restart();
    } else {
      LOGE("MQTT: Failed to update network config\n");
    }
  }
  // Procesar getNetworkConfig: devolver la configuración de red actual
  // Response topic: device/{MAC}_var/networkConfig
  // Payload: useDhcp,ip,gateway,subnet,dns,currentIp
  else if (strstr(topic, "/getNetworkConfig") != nullptr) {
    const char *macColoned = NetworkManager::instance().macString();
    char macNoColon[13] = {0};
    int idx = 0;
    for (const char *p = macColoned; *p && idx < 12; ++p) {
      if (*p != ':') macNoColon[idx++] = *p;
    }

    char netBuf[96];
    snprintf(netBuf, sizeof(netBuf), "%u,%s,%s,%s,%s,%s",
             eeprom.getNetworkUseDhcp() ? 1 : 0,
             eeprom.getNetworkStaticIp().toString().c_str(),
             eeprom.getNetworkGateway().toString().c_str(),
             eeprom.getNetworkSubnet().toString().c_str(),
             eeprom.getNetworkDns().toString().c_str(),
             NetworkManager::instance().localIP().toString().c_str());

    char responseTopic[64];
    snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/networkConfig", macNoColon);

    auto &mqtt = MqttManager::instance();
    if (mqtt.publish(responseTopic, netBuf)) {
      LOGI("MQTT: Published networkConfig (%s)\n", netBuf);
    } else {
      LOGE("MQTT: Failed to publish networkConfig\n");
    }
  }
  // Procesar startRealTime: activar modo Real Time por X segundos
  else if (strstr(topic, "/startRealTime") != nullptr) {
    uint32_t durationSeconds = atoi(msg);
    if (durationSeconds > 0 && durationSeconds <= 3600) {
      startRealTimeMode(durationSeconds);
      LOGI("MQTT: Real Time Mode activated for %lu seconds\n", durationSeconds);
    } else {
      LOGE("MQTT: Invalid Real Time duration (1-3600 seconds required)\n");
    }
  }
  // Procesar backupList: listar archivos de backup en una fecha específica
  else if (strstr(topic, "/backupList") != nullptr) {
    // El payload contiene año,mes (ej: "2026,01")
    int year, month;
    int parsed = sscanf(msg, "%d,%d", &year, &month);
    
    if (parsed != 2 || year < 2000 || year > 2100 || month < 1 || month > 12) {
      LOGE("MQTT: Invalid backupList format. Expected: year,month (e.g., 2026,01)\n");
      return;
    }
    
    // Obtener lista de archivos
    auto &sd = SdManager::instance();
    String fileList = sd.listFiles(year, month);
    
    // Obtener MAC para construir topic de respuesta
    const char *macColoned = NetworkManager::instance().macString();
    char macNoColon[13] = {0};
    int idx = 0;
    for (const char *p = macColoned; *p && idx < 12; ++p) {
      if (*p != ':') {
        macNoColon[idx++] = *p;
      }
    }
    
    char responseTopic[64];
    snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/backupList", macNoColon);
    
    auto &mqtt = MqttManager::instance();
    if (mqtt.publish(responseTopic, fileList.c_str())) {
      LOGI("MQTT: Published backup list for %04d/%02d\n", year, month);
    } else {
      LOGE("MQTT: Failed to publish backup list\n");
    }
  }
  // Procesar backupUpload: subir archivo de backup al servidor
  else if (strstr(topic, "/backupUpload") != nullptr) {
    // El payload contiene año,mes,dia (ej: "2026,01,25")
    int year, month, day;
    int parsed = sscanf(msg, "%d,%d,%d", &year, &month, &day);
    
    if (parsed != 3 || year < 2000 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) {
      LOGE("MQTT: Invalid backupUpload format. Expected: year,month,day (e.g., 2026,01,25)\n");
      return;
    }
    
    // Guardar parámetros para procesar fuera del callback
    MqttManager::instance().requestBackupUpload(year, month, day);
  }
  // Procesar writeRegister: escribir un Holding Register por Modbus (FC06)
  // Payload: deviceIndex,address,value  (ej: "1,0,10" escribe 10 en addr 0 del dispositivo 1)
  else if (strstr(topic, "/writeRegister") != nullptr) {
    unsigned int devIdx, addr;
    char valStr[32] = {0};
    int parsed = sscanf(msg, "%u,%u,%31s", &devIdx, &addr, valStr);
    
    if (parsed != 3) {
      LOGE("MQTT: Invalid writeRegister format. Expected: deviceIndex,address,value\n");
      return;
    }
    
    if (addr > 65535) {
      LOGE("MQTT: writeRegister address out of uint16 range\n");
      return;
    }
    
    auto &modbus = ModbusManager::instance();
    bool ok = modbus.writeRegister(devIdx, (uint16_t)addr, valStr);
    
    if (ok) {
      LOGI("MQTT: writeRegister device=%u addr=%u val=%s OK\n", devIdx, addr, valStr);
    } else {
      LOGE("MQTT: writeRegister device=%u addr=%u val=%s FAILED\n", devIdx, addr, valStr);
    }
  }
  // Procesar writeCoil: escribir un Single Coil por Modbus (FC05)
  // Payload: deviceIndex,address,value  (ej: "1,0,1" activa coil 0 del dispositivo 1)
  else if (strstr(topic, "/writeCoil") != nullptr) {
    unsigned int devIdx, addr;
    char valStr[32] = {0};
    int parsed = sscanf(msg, "%u,%u,%31s", &devIdx, &addr, valStr);
    
    if (parsed != 3) {
      LOGE("MQTT: Invalid writeCoil format. Expected: deviceIndex,address,value (1/0 or FF00/0000)\n");
      return;
    }
    
    if (addr > 65535) {
      LOGE("MQTT: writeCoil address out of uint16 range\n");
      return;
    }
    
    auto &modbus = ModbusManager::instance();
    bool ok = modbus.writeCoil(devIdx, (uint16_t)addr, valStr);
    
    if (ok) {
      LOGI("MQTT: writeCoil device=%u addr=%u val=%s OK\n", devIdx, addr, valStr);
    } else {
      LOGE("MQTT: writeCoil device=%u addr=%u val=%s FAILED\n", devIdx, addr, valStr);
    }
  }
  // Procesar confirmCoil: establecer una confirmación de un coil del actuador
  // Payload: coilIndex,confirmIndex,value  (ej: "0,2,1")
  else if (strstr(topic, "/confirmCoil") != nullptr) {
    unsigned int coilIdx, confirmIdx, val;
    int parsed = sscanf(msg, "%u,%u,%u", &coilIdx, &confirmIdx, &val);
    if (parsed != 3) {
      LOGE("MQTT: Invalid confirmCoil format. Expected: coilIndex,confirmIndex,value\n");
      return;
    }
    ActuatorManager::instance().setConfirmation(coilIdx, (uint8_t)confirmIdx, val != 0);
  }
  // Procesar setCoil: solicitar escritura directa de un coil del actuador
  // Payload: coilIndex,value  (ej: "0,1")
  else if (strstr(topic, "/setCoil") != nullptr) {
    unsigned int coilIdx, val;
    int parsed = sscanf(msg, "%u,%u", &coilIdx, &val);
    if (parsed != 2) {
      LOGE("MQTT: Invalid setCoil format. Expected: coilIndex,value\n");
      return;
    }
    ActuatorManager::instance().requestCoil(coilIdx, val != 0);
  }
  // Procesar coilConfirmEnable: activar/desactivar confirmaciones de un coil
  // Payload: coilIndex,value  (ej: "0,1")
  else if (strstr(topic, "/coilConfirmEnable") != nullptr) {
    unsigned int coilIdx, val;
    int parsed = sscanf(msg, "%u,%u", &coilIdx, &val);
    if (parsed != 2) {
      LOGE("MQTT: Invalid coilConfirmEnable format. Expected: coilIndex,value\n");
      return;
    }
    ActuatorManager::instance().setConfirmationsEnabled(coilIdx, val != 0);
  }
  // Procesar statusCoils: publicar el estado de todas las coils
  else if (strstr(topic, "/statusCoils") != nullptr) {
    ActuatorManager::instance().publishAllStatus();
  }
  // Procesar saveModbusDevices: reemplazar TODA la lista de dispositivos Modbus
  // en EEPROM y en el array en ejecución.
  // Payload compacto: dispositivos separados por ';', campos por ',':
  //   ip,unitId,startReg,totalRegs,regType,swapWords,slaveId,slaveName
  //   regType: 0 o 3 = HOLDING_REGISTER (FC03) ; 1 o 4 = INPUT_REGISTER (FC04)
  //   swapWords: 0/1
  // Ej: "192.168.1.200,1,0,100,1,0,1,Device_1;192.168.1.101,1,0,8,0,1,3,Device_2"
  else if (strstr(topic, "/saveModbusDevices") != nullptr) {
    // El payload puede exceder el buffer msg[256]; usar uno mayor desde payload.
    char buf[640];
    unsigned int blen = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
    memcpy(buf, payload, blen);
    buf[blen] = '\0';

    ModbusDeviceConfig devs[kMaxModbusDevices];
    char names[kMaxModbusDevices][kModbusSlaveNameLen];
    size_t count = 0;
    bool parseOk = true;

    char* devSave = nullptr;
    char* devTok = strtok_r(buf, ";", &devSave);
    while (devTok != nullptr && count < kMaxModbusDevices) {
      char* fSave = nullptr;
      char* ipStr    = strtok_r(devTok, ",", &fSave);
      char* unitStr  = strtok_r(nullptr, ",", &fSave);
      char* startStr = strtok_r(nullptr, ",", &fSave);
      char* totStr   = strtok_r(nullptr, ",", &fSave);
      char* rtStr    = strtok_r(nullptr, ",", &fSave);
      char* swStr    = strtok_r(nullptr, ",", &fSave);
      char* midStr   = strtok_r(nullptr, ",", &fSave);
      char* nameStr  = strtok_r(nullptr, ",", &fSave);

      if (!ipStr || !unitStr || !startStr || !totStr || !rtStr || !swStr || !midStr || !nameStr) {
        parseOk = false;
        break;
      }

      IPAddress ip;
      if (!ip.fromString(ipStr)) {
        parseOk = false;
        break;
      }

      // regType acepta dos convenciones:
      //   0 o 3 -> HOLDING_REGISTER (FC03)
      //   1 o 4 -> INPUT_REGISTER  (FC04)
      // Cualquier otro valor se considera invalido y rechaza el mensaje completo.
      int rt = atoi(rtStr);
      ModbusRegisterType regType;
      if (rt == 0 || rt == 3) {
        regType = ModbusRegisterType::HOLDING_REGISTER;
      } else if (rt == 1 || rt == 4) {
        regType = ModbusRegisterType::INPUT_REGISTER;
      } else {
        LOGE("MQTT saveModbusDevices: regType invalido '%s' (use 0/3=holding, 1/4=input)", rtStr);
        parseOk = false;
        break;
      }

      devs[count].ip = ip;
      devs[count].unitId = (uint8_t)atoi(unitStr);
      devs[count].startReg = (uint16_t)atoi(startStr);
      devs[count].totalRegs = (uint16_t)atoi(totStr);
      devs[count].regType = regType;
      devs[count].swapWords = (atoi(swStr) != 0);
      devs[count].modbusSlaveId = (uint8_t)atoi(midStr);
      strncpy(names[count], nameStr, kModbusSlaveNameLen - 1);
      names[count][kModbusSlaveNameLen - 1] = '\0';
      devs[count].modbusSlaveName = names[count];

      count++;
      devTok = strtok_r(nullptr, ";", &devSave);
    }

    if (parseOk && count > 0) {
      // setModbusDevices borra los anteriores, persiste en EEPROM y refresca el array en RAM.
      if (eeprom.setModbusDevices(devs, count)) {
        LOGI("MQTT: Modbus devices refreshed (%u)\n", (unsigned)count);
      } else {
        LOGE("MQTT: Failed to persist Modbus devices\n");
      }
    } else {
      LOGE("MQTT: Invalid saveModbusDevices payload\n");
    }
  }
  // Procesar getModbusDevices: devolver la lista actual de dispositivos Modbus guardada en EEPROM
  // Response topic: device/{MAC}_var/modbusDevices
  // Payload: mismo formato compacto que saveModbusDevices
  else if (strstr(topic, "/getModbusDevices") != nullptr) {
    const char *macColoned = NetworkManager::instance().macString();
    char macNoColon[13] = {0};
    int idx = 0;
    for (const char *p = macColoned; *p && idx < 12; ++p) {
      if (*p != ':') macNoColon[idx++] = *p;
    }

    char devBuf[640];
    buildModbusDevicesString(devBuf, sizeof(devBuf));

    char responseTopic[64];
    snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/modbusDevices", macNoColon);

    auto &mqtt = MqttManager::instance();
    if (mqtt.publish(responseTopic, devBuf)) {
      LOGI("MQTT: Published modbusDevices (%u devices)\n", (unsigned)eeprom.getModbusDeviceCount());
    } else {
      LOGE("MQTT: Failed to publish modbusDevices\n");
    }
  }
  // Procesar cleanModbusDevices: vaciar la lista de dispositivos Modbus de la EEPROM
  // y del array en ejecucion (payload: cualquiera).
  // Tras el borrado publica la lista (ahora vacia) en device/{MAC}_var/modbusDevices
  // para confirmar el resultado.
  else if (strstr(topic, "/cleanModbusDevices") != nullptr) {
    if (eeprom.clearModbusDevices()) {
      LOGI("MQTT: Modbus devices cleared\n");
    } else {
      LOGE("MQTT: Failed to clear Modbus devices\n");
    }

    const char *macColoned = NetworkManager::instance().macString();
    char macNoColon[13] = {0};
    int idx = 0;
    for (const char *p = macColoned; *p && idx < 12; ++p) {
      if (*p != ':') macNoColon[idx++] = *p;
    }

    char devBuf[640];
    buildModbusDevicesString(devBuf, sizeof(devBuf));

    char responseTopic[64];
    snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/modbusDevices", macNoColon);

    auto &mqtt = MqttManager::instance();
    if (mqtt.publish(responseTopic, devBuf)) {
      LOGI("MQTT: Published modbusDevices (%u devices)\n", (unsigned)eeprom.getModbusDeviceCount());
    } else {
      LOGE("MQTT: Failed to publish modbusDevices\n");
    }
  }
  // Procesar getActuators: devolver la configuracion actual de los actuadores (coils)
  // Response topic: device/{MAC}_var/actuators
  // Payload: coilIndex,modbusAddress,confirmationsEnabled;...
  else if (strstr(topic, "/getActuators") != nullptr) {
    const char *macColoned = NetworkManager::instance().macString();
    char macNoColon[13] = {0};
    int idx = 0;
    for (const char *p = macColoned; *p && idx < 12; ++p) {
      if (*p != ':') macNoColon[idx++] = *p;
    }

    char actBuf[256];
    buildActuatorsString(actBuf, sizeof(actBuf));

    char responseTopic[64];
    snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/actuators", macNoColon);

    auto &mqtt = MqttManager::instance();
    if (mqtt.publish(responseTopic, actBuf)) {
      LOGI("MQTT: Published actuators (%u coils)\n", (unsigned)kActuatorCoilCount);
    } else {
      LOGE("MQTT: Failed to publish actuators\n");
    }
  }
  // Procesar saveActuators: reemplazar la configuracion de los actuadores en EEPROM
  // y recargarla en ejecucion.
  // Payload compacto: deviceIndex;coilIndex,onAddress,onValue,offAddress,offValue,confirmationsEnabled;...
  //   deviceIndex: indice del dispositivo Modbus (0..count-1) usado para escribir los coils
  //   coilIndex:   indice del coil (0..kActuatorCoilCount-1)
  //   onAddress/onValue:   direccion y valor (0/1) a escribir en secuencia de arranque (111)
  //   offAddress/offValue: direccion y valor (0/1) a escribir en secuencia de paro (000)
  //   confirmationsEnabled: 0/1
  // Ej: "1;0,0,1,0,0,1;1,1,1,1,0,1;2,2,1,2,0,1;3,3,1,3,0,1"
  // Los coils no incluidos en el payload conservan su valor actual.
  else if (strstr(topic, "/saveActuators") != nullptr) {
    char buf[256];
    unsigned int blen = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
    memcpy(buf, payload, blen);
    buf[blen] = '\0';

    // Partir de la configuracion actual para permitir actualizaciones parciales.
    uint16_t onAddresses[kActuatorCoilCount];
    bool     onValues[kActuatorCoilCount];
    uint16_t offAddresses[kActuatorCoilCount];
    bool     offValues[kActuatorCoilCount];
    bool     enabled[kActuatorCoilCount];
    uint8_t  confirmAlarmIndices[kActuatorCoilCount];
    for (size_t i = 0; i < kActuatorCoilCount; ++i) {
      onAddresses[i]          = eeprom.getActuatorCoilOnAddress(i);
      onValues[i]             = eeprom.getActuatorCoilOnValue(i);
      offAddresses[i]         = eeprom.getActuatorCoilOffAddress(i);
      offValues[i]            = eeprom.getActuatorCoilOffValue(i);
      enabled[i]              = eeprom.getActuatorCoilEnabled(i);
      confirmAlarmIndices[i]  = eeprom.getActuatorCoilConfirmAlarmIndex(i);
    }

    bool parseOk = true;

    char* tokSave = nullptr;
    char* devIdxStr = strtok_r(buf, ";", &tokSave);
    if (!devIdxStr) {
      parseOk = false;
    }

    size_t deviceIndex = eeprom.getActuatorModbusDeviceIndex();
    if (parseOk) {
      int di = atoi(devIdxStr);
      if (di < 0 || (size_t)di >= eeprom.getModbusDeviceCount()) {
        LOGE("MQTT saveActuators: deviceIndex invalido '%s' (count=%u)\n",
             devIdxStr, (unsigned)eeprom.getModbusDeviceCount());
        parseOk = false;
      } else {
        deviceIndex = (size_t)di;
      }
    }

    if (parseOk) {
      char* coilTok = strtok_r(nullptr, ";", &tokSave);
      while (coilTok != nullptr) {
        char* fSave         = nullptr;
        char* idxStr        = strtok_r(coilTok, ",", &fSave);
        char* onAddrStr     = strtok_r(nullptr, ",", &fSave);
        char* onValStr      = strtok_r(nullptr, ",", &fSave);
        char* offAddrStr    = strtok_r(nullptr, ",", &fSave);
        char* offValStr     = strtok_r(nullptr, ",", &fSave);
        char* enStr         = strtok_r(nullptr, ",", &fSave);
        char* confAlmIdxStr = strtok_r(nullptr, ",", &fSave);

        if (!idxStr || !onAddrStr || !onValStr || !offAddrStr || !offValStr || !enStr || !confAlmIdxStr) {
          parseOk = false;
          break;
        }

        int ci = atoi(idxStr);
        if (ci < 0 || (size_t)ci >= kActuatorCoilCount) {
          LOGE("MQTT saveActuators: coilIndex invalido '%s'\n", idxStr);
          parseOk = false;
          break;
        }

        onAddresses[ci]         = (uint16_t)atoi(onAddrStr);
        onValues[ci]            = (atoi(onValStr) != 0);
        offAddresses[ci]        = (uint16_t)atoi(offAddrStr);
        offValues[ci]           = (atoi(offValStr) != 0);
        enabled[ci]             = (atoi(enStr) != 0);
        confirmAlarmIndices[ci] = (uint8_t)atoi(confAlmIdxStr);

        coilTok = strtok_r(nullptr, ";", &tokSave);
      }
    }

    if (parseOk) {
      if (eeprom.setActuatorConfig(deviceIndex, onAddresses, onValues, offAddresses, offValues, enabled, confirmAlarmIndices)) {
        ActuatorManager::instance().reloadConfig();
        LOGI("MQTT: Actuators config saved (deviceIndex=%u)\n", (unsigned)deviceIndex);
      } else {
        LOGE("MQTT: Failed to persist actuators config\n");
      }
    } else {
      LOGE("MQTT: Invalid saveActuators payload\n");
    }

    // Publicar la configuracion (re)cargada para confirmar el resultado.
    const char *macColoned = NetworkManager::instance().macString();
    char macNoColon[13] = {0};
    int idx = 0;
    for (const char *p = macColoned; *p && idx < 12; ++p) {
      if (*p != ':') macNoColon[idx++] = *p;
    }

    char actBuf[256];
    buildActuatorsString(actBuf, sizeof(actBuf));

    char responseTopic[64];
    snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/actuators", macNoColon);

    auto &mqtt = MqttManager::instance();
    if (mqtt.publish(responseTopic, actBuf)) {
      LOGI("MQTT: Published actuators (%u coils)\n", (unsigned)kActuatorCoilCount);
    } else {
      LOGE("MQTT: Failed to publish actuators\n");
    }
  }
  // Procesar cleanActuators: restablecer la configuracion de actuadores a los valores
  // por defecto (app_config) en EEPROM y recargarla. Tras el borrado publica la
  // configuracion en device/{MAC}_var/actuators para confirmar.
  else if (strstr(topic, "/cleanActuators") != nullptr) {
    if (eeprom.clearActuatorConfig()) {
      ActuatorManager::instance().reloadConfig();
      LOGI("MQTT: Actuators config reset to defaults\n");
    } else {
      LOGE("MQTT: Failed to reset actuators config\n");
    }

    const char *macColoned = NetworkManager::instance().macString();
    char macNoColon[13] = {0};
    int idx = 0;
    for (const char *p = macColoned; *p && idx < 12; ++p) {
      if (*p != ':') macNoColon[idx++] = *p;
    }

    char actBuf[256];
    buildActuatorsString(actBuf, sizeof(actBuf));

    char responseTopic[64];
    snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/actuators", macNoColon);

    auto &mqtt = MqttManager::instance();
    if (mqtt.publish(responseTopic, actBuf)) {
      LOGI("MQTT: Published actuators (%u coils)\n", (unsigned)kActuatorCoilCount);
    } else {
      LOGE("MQTT: Failed to publish actuators\n");
    }
  }
  // Procesar installFirmware: marcar para actualizar firmware (se procesa fuera del callback)
  else if (strstr(topic, "/installFirmware") != nullptr) {
    // El payload contiene la versión a instalar
    if (strlen(msg) == 0 || strlen(msg) >= 32) {
      LOGE("MQTT: Invalid firmware version\n");
      return;
    }
    
    LOGI("MQTT: Firmware install requested, version: %s\n", msg);
    MqttManager::instance().requestFirmwareUpdate(msg);
  }
  // Procesar solicitudes de lectura de variables: device/{MAC}/getValue con variable en payload
  else if (strstr(topic, "/getValue") != nullptr && strstr(topic, "/getValues") == nullptr) {
    // El payload contiene el nombre de la variable
    const char* varName = msg;
    
    // Obtener MAC para construir topic de respuesta
    const char *macColoned = NetworkManager::instance().macString();
    char macNoColon[13] = {0};
    int idx = 0;
    for (const char *p = macColoned; *p && idx < 12; ++p) {
      if (*p != ':') {
        macNoColon[idx++] = *p;
      }
    }
    
    char responseTopic[64];
    String value;
    
    if (strcmp(varName, "webService") == 0) {
      value = eeprom.getWebServiceURL();
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/webService", macNoColon);
    }
    else if (strcmp(varName, "firmwareVersion") == 0) {
      value = String(kFirmwareVersion);
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/firmwareVersion", macNoColon);
    }
    else if (strcmp(varName, "realTimeIntervalSec") == 0) {
      value = String(eeprom.getRealTimeIntervalSec());
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/realTimeIntervalSec", macNoColon);
    }
    else if (strcmp(varName, "instantValuesIntervalSec") == 0) {
      value = String(eeprom.getInstantValuesIntervalSec());
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/instantValuesIntervalSec", macNoColon);
    }
    else if (strcmp(varName, "deviceId") == 0) {
      value = String(eeprom.getDeviceID());
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/deviceId", macNoColon);
    }
    else if (strcmp(varName, "deviceTime") == 0) {
      value = getDeviceTimeString();
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/deviceTime", macNoColon);
    }
    else if (strcmp(varName, "modbusDevices") == 0) {
      char devBuf[640];
      buildModbusDevicesString(devBuf, sizeof(devBuf));
      value = String(devBuf);
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/modbusDevices", macNoColon);
    }
    else if (strcmp(varName, "actuators") == 0) {
      char actBuf[256];
      buildActuatorsString(actBuf, sizeof(actBuf));
      value = String(actBuf);
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/actuators", macNoColon);
    }
    else if (strcmp(varName, "serverConfig") == 0) {
      char srvBuf[48];
      snprintf(srvBuf, sizeof(srvBuf), "%u,%u,%u,%lu",
               eeprom.getServerUnitId(), eeprom.getServerPort(),
               eeprom.getServerMaxClients(), (unsigned long)eeprom.getServerTimeoutMs());
      value = String(srvBuf);
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/modbusServerConfig", macNoColon);
    }
    else {
      LOGW("MQTT: Unknown variable requested: %s\n", varName);
      return;
    }
    
    // Publicar el valor
    auto &mqtt = MqttManager::instance();
    if (mqtt.publish(responseTopic, value.c_str())) {
      LOGI("MQTT: Published %s = %s\n", varName, value.c_str());
    } else {
      LOGE("MQTT: Failed to publish %s\n", varName);
    }
  }
  // Procesar solicitud de todas las variables: device/{MAC}/getValues
  else if (strstr(topic, "/getValues") != nullptr) {
    // Obtener MAC para construir topic de respuesta
    const char *macColoned = NetworkManager::instance().macString();
    char macNoColon[13] = {0};
    int idx = 0;
    for (const char *p = macColoned; *p && idx < 12; ++p) {
      if (*p != ':') {
        macNoColon[idx++] = *p;
      }
    }
    
    // Construir JSON con todas las variables
    char jsonBuffer[512];
    buildAllVariablesJson(jsonBuffer, sizeof(jsonBuffer), macNoColon);
    
    // Publicar todas las variables en un solo mensaje
    char responseTopic[64];
    snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/all", macNoColon);
    
    auto &mqtt = MqttManager::instance();
    if (mqtt.publish(responseTopic, jsonBuffer)) {
      LOGI("MQTT: Published all variables\n");
    } else {
      LOGE("MQTT: Failed to publish all variables\n");
    }
  }
  // Procesar ajuste de hora del RTC: device/{MAC}/adjustDeviceTime
  else if (strstr(topic, "/adjustDeviceTime") != nullptr) {
    // Parsear mensaje: año,mes,dia,hora,minuto,segundo
    int year, month, day, hour, minute, second;
    int parsed = sscanf(msg, "%d,%d,%d,%d,%d,%d", &year, &month, &day, &hour, &minute, &second);
    
    if (parsed != 6) {
      LOGE("MQTT: Invalid time format. Expected: year,month,day,hour,minute,second\n");
      return;
    }
    
    // Validar rangos básicos
    if (year < 2000 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
      LOGE("MQTT: Time values out of range\n");
      return;
    }
    
    // Ajustar el RTC
    auto &rtc = RtcManager::instance();
    if (rtc.setDateTime(year, month, day, hour, minute, second)) {
      LOGI("MQTT: RTC time updated to %04d-%02d-%02d %02d:%02d:%02d\n",
           year, month, day, hour, minute, second);
      
      // Obtener hora actualizada del RTC y publicar confirmación
      const char *macColoned = NetworkManager::instance().macString();
      char macNoColon[13] = {0};
      int idx = 0;
      for (const char *p = macColoned; *p && idx < 12; ++p) {
        if (*p != ':') {
          macNoColon[idx++] = *p;
        }
      }
      
      String deviceTimeStr = getDeviceTimeString();
      
      char responseTopic[64];
      snprintf(responseTopic, sizeof(responseTopic), "device/%s_var/deviceTime", macNoColon);
      
      auto &mqtt = MqttManager::instance();
      if (mqtt.publish(responseTopic, deviceTimeStr.c_str())) {
        LOGI("MQTT: Published device time confirmation\n");
      } else {
        LOGE("MQTT: Failed to publish device time\n");
      }
    } else {
      LOGE("MQTT: Failed to update RTC time\n");
    }
  }
}

bool MqttManager::connectInternal() {
  if (!NetworkManager::instance().isConnected()) {
    return false;
  }

  // Build username as PM_ + MAC without colons, e.g., PM_AABBCCDDEEFF
  const char *macColoned = NetworkManager::instance().macString();
  char macNoColon[13] = {0};
  int idx = 0;
  for (const char *p = macColoned; *p && idx < 12; ++p) {
    if (*p != ':') {
      macNoColon[idx++] = *p;
    }
  }

  char username[20];
  snprintf(username, sizeof(username), "PM_%s", macNoColon);

  // Build a client ID with a suffix to avoid collisions
  char clientId[64];
  snprintf(clientId, sizeof(clientId), "%s-%lu", kMqttClientIdPrefix, millis());

  LOGI("MQTT connecting to %s:%u...\n", kMqttBrokerHost, kMqttBrokerPort);
  const bool ok = client_.connect(clientId, username, kMqttPassword);
  if (ok) {
    LOGI("MQTT connected\n");
    
    // Subscribirse automáticamente a device/{MAC}/# (wildcard para todos los subtopics)
    char deviceTopic[32];
    snprintf(deviceTopic, sizeof(deviceTopic), "device/%s/#", macNoColon);
    if (client_.subscribe(deviceTopic)) {
      LOGI("MQTT subscribed to %s\n", deviceTopic);
    } else {
      LOGE("MQTT subscribe to %s failed\n", deviceTopic);
    }
    
    // Publicar mensaje de arranque con todas las variables
    publishStartingMessage(macNoColon);
  } else {
    int state = client_.state();
    LOGE("MQTT connect failed rc=%d (broker=%s:%u, user=%s)\n", 
         state, kMqttBrokerHost, kMqttBrokerPort, username);
  }
  return ok;
}

void MqttManager::ensureConnected() {
  const unsigned long now = millis();
  if (client_.connected()) {
    return;
  }

  if (now - lastReconnectAttemptMs_ < kMqttReconnectDelayMs) {
    return;
  }

  lastReconnectAttemptMs_ = now;
  connectInternal();
}

void MqttManager::loop() {
  if (client_.connected()) {
    client_.loop();
    
    // Publicar mensajes pendientes si hay conexión establecida
    publishPendingBackupResult();
  }
}

void MqttManager::disconnect() {
  if (client_.connected()) {
    client_.disconnect();
    LOGW("MQTT disconnected\n");
  }
}

bool MqttManager::isConnected() {
  return client_.connected();
}

bool MqttManager::publish(const char* topic, const char* payload) {
  if (!client_.connected()) {
    LOGE("MQTT: Cannot publish, not connected\n");
    return false;
  }
  
  return client_.publish(topic, payload);
}

void MqttManager::requestFirmwareUpdate(const char* version) {
  strncpy(pendingFirmwareVersion_, version, sizeof(pendingFirmwareVersion_) - 1);
  pendingFirmwareVersion_[sizeof(pendingFirmwareVersion_) - 1] = '\0';
  firmwareUpdatePending_ = true;
  LOGI("MQTT: Firmware update queued for version: %s\n", pendingFirmwareVersion_);
}

void MqttManager::processPendingFirmwareUpdate() {
  if (!firmwareUpdatePending_) {
    return;
  }
  
  firmwareUpdatePending_ = false;
  
  LOGI("MQTT: Processing firmware update for version: %s\n", pendingFirmwareVersion_);
  
  // Desconectar MQTT para liberar memoria
  disconnect();
  LOGI("MQTT: Disconnected to free memory for OTA\n");
  
  // Esperar un poco para que se libere memoria
  delay(500);
  
  // Delegar al OtaManager
  OtaManager::instance().performUpdate(pendingFirmwareVersion_);
}

void MqttManager::requestBackupUpload(int year, int month, int day) {
  pendingBackupYear_ = year;
  pendingBackupMonth_ = month;
  pendingBackupDay_ = day;
  backupUploadPending_ = true;
  LOGI("MQTT: Backup upload queued for %04d/%02d/%02d\n", year, month, day);
}

void MqttManager::processPendingBackupUpload() {
  if (!backupUploadPending_) {
    return;
  }
  
  backupUploadPending_ = false;
  
  LOGI("MQTT: Processing backup upload for %04d/%02d/%02d\n", 
       pendingBackupYear_, pendingBackupMonth_, pendingBackupDay_);
  
  // Suspender todas las tareas para dar prioridad al upload
  auto &ota = OtaManager::instance();
  ota.suspendAllTasks();
  
  // Desconectar MQTT para liberar memoria y evitar conflictos de conexión
  disconnect();
  LOGI("MQTT: Disconnected for backup upload\n");
  
  // Esperar un poco para que se libere memoria
  delay(500);
  
  // Obtener deviceId
  auto &eeprom = EepromManager::instance();
  int32_t deviceId = eeprom.getDeviceID();
  
  // Subir archivo
  auto &sd = SdManager::instance();
  bool success = sd.uploadBackupFile(pendingBackupYear_, pendingBackupMonth_, pendingBackupDay_, deviceId);
  
  // Guardar resultado en EEPROM para publicar después de reconectar (persistente)
  // Formato: "deviceId-DD/MM/YYYY-resultado"
  char resultPayload[32];
  snprintf(resultPayload, sizeof(resultPayload), "%d-%02d/%02d/%04d-%d",
           deviceId, pendingBackupDay_, pendingBackupMonth_, pendingBackupYear_, success ? 1 : 0);
  eeprom.setPendingBackupResult(resultPayload);
  
  if (success) {
    LOGI("MQTT: Backup upload completed successfully\n");
  } else {
    LOGE("MQTT: Backup upload failed\n");
  }
  
  // Reiniciar el ESP para limpiar estado
  LOGI("MQTT: Restarting ESP after backup upload...\n");
  delay(500);
  ESP.restart();
}

void MqttManager::publishStartingMessage(const char* macNoColon) {
  // Construir JSON con todas las variables + MAC
  char jsonBuffer[512];
  buildAllVariablesJson(jsonBuffer, sizeof(jsonBuffer), macNoColon);
  
  // Construir topic: kMqttSystemTopic/starting
  char startingTopic[64];
  snprintf(startingTopic, sizeof(startingTopic), "%s/starting", kMqttSystemTopic);
  
  // Publicar mensaje de arranque
  if (client_.publish(startingTopic, jsonBuffer)) {
    LOGI("MQTT: Published starting message to %s\n", startingTopic);
  } else {
    LOGE("MQTT: Failed to publish starting message\n");
  }
}

void MqttManager::publishPendingBackupResult() {
  auto &eeprom = EepromManager::instance();
  
  if (!eeprom.hasPendingBackupResult()) {
    return;
  }
  
  String payload = eeprom.getPendingBackupResult();
  
  // Construir topic: kMqttSystemTopic/uploadBackup
  char topic[64];
  snprintf(topic, sizeof(topic), "%s/uploadBackup", kMqttSystemTopic);
  
  // Publicar resultado
  if (client_.publish(topic, payload.c_str())) {
    LOGI("MQTT: Published backup result to %s: %s\n", topic, payload.c_str());
    // Limpiar resultado pendiente de EEPROM
    eeprom.clearPendingBackupResult();
  } else {
    LOGE("MQTT: Failed to publish backup result\n");
  }
}
