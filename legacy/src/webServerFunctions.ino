void setModbusRegister() {
    String word1 = server.arg(0);
    String word2 = server.arg(1);
    Serial.println(word1);
    Serial.println(word2);
      // set word 0 of TX buffer to least-significant word of counter (bits 15..0)
    modbus.setTransmitBuffer(0, word1.toInt());
    
    // set word 1 of TX buffer to most-significant word of counter (bits 31..16)
    modbus.setTransmitBuffer(1, word2.toInt());
    
    // slave: write TX buffer to (2) 16-bit registers starting at register 0
    uint8_t result = modbus.writeMultipleRegisters(0x2B66, 2);
    Serial.print(result);
    Serial.println("modbus writen...");
    server.send(200, "text/json", "modbus writen...");
}

void getModbusRegister() {
    String args = server.arg(0);
    uint16_t data[2];
    String modbusValues;
    Serial.print("......... ");
    Serial.println(args);
    uint8_t result = modbus.readHoldingRegisters(0x2B66, 2);
    if (result == modbus.ku8MBSuccess)
    {
      for (int j = 0; j < 2; j++)
      {
        data[j] = modbus.getResponseBuffer(j);
        modbusValues += data[j];
        modbusValues += ",";
      }
    }
    server.send(200, "text/json", modbusValues);
    Serial.print(modbusValues);
    Serial.println(" .........");
}

void getRates() {
    String key = server.arg(0);
    String modbusValues;
    modbusValues += readRates(R1);
    modbusValues += " *** ";
    modbusValues += readRates(R2);
    modbusValues += " *** ";
    modbusValues += readRates(R3);
    modbusValues += " *** ";
    modbusValues += readRates(RT);
    server.send(200, "text/json", modbusValues);
    Serial.print(modbusValues);
    Serial.println(" .........");
}

void setRates() {
    String word1 = server.arg(0);
    String word2 = server.arg(1);
    Serial.println(word1);
    Serial.println(word2);
    // set word 0 of TX buffer to least-significant word of counter (bits 15..0)
    modbus.writeSingleCoil(0x834, 1);
    modbus.writeSingleCoil(0x838, 1);
    modbus.writeSingleCoil(0x83D, 1);
    modbus.writeSingleCoil(0x272C, 50);
    modbus.writeSingleCoil(0x272D, 150);
    modbus.writeSingleCoil(0x272E, 200);
    Serial.println("setRates writen...");
    server.send(200, "text/json", "setRates writen...");
}
void switchRate() {
    String rate = server.arg(0);
    if (rate == "2") {
      Serial.println("activado rate 2");
      digitalWrite(relay_rateSelector2, HIGH);
      digitalWrite(relay_rateSelector3, LOW);
      server.send(200, "text/json", "prendido");
    } else if (rate == "3") {
      Serial.println("prendido");
      digitalWrite(relay_rateSelector3, HIGH);
      digitalWrite(relay_rateSelector2, LOW);
      server.send(200, "text/json", "prendido");
    } else {
      digitalWrite(relay_rateSelector2, LOW);
      digitalWrite(relay_rateSelector3, LOW);
      server.send(200, "text/json", "apagado");
    }
}

void setRate2Time() {
    String hourStart = server.arg(0);
    String minStart = server.arg(1);
    String hourStop = server.arg(2);
    String minStop = server.arg(3);
    hourStartRate2 = hourStart.toInt();
    minStartRate2 = minStart.toInt();
    hourStopRate2 = hourStop.toInt();
    minStopRate2 = minStop.toInt();
    server.send(200, "text/json", "rate2 time updated");
}

void setTime() {  // function called webserver trhu ip in the network
    String year = server.arg(0);
    String month = server.arg(1);
    String day = server.arg(2);
    String hour = server.arg(3);
    String min = server.arg(4);
    String sec = server.arg(5);
    rtc.adjust(DateTime(
        year.toInt(),
        month.toInt(),
        day.toInt(),
        hour.toInt(),
        min.toInt(),
        sec.toInt()));
    DateTime now = rtc.now();
    String value = "";
    value += now.year();
    value += "/";
    value += now.month();
    value += "/";
    value += now.day();
    value += " ";
    value += now.hour();
    value += ":";
    value += now.minute();
    value += ":";
    value += now.second();
    server.send(200, "text/json", value);
}

void getDeviceTime() {
    DateTime now = rtc.now();
    String value = "";
    value += now.year();
    value += "/";
    value += now.month();
    value += "/";
    value += now.day();
    value += " ";
    value += now.hour();
    value += ":";
    value += now.minute();
    value += ":";
    value += now.second();
    server.send(200, "text/json", value);
}

void setTopics() {
    String allTopics = server.arg(0);
    saveTopics(allTopics);
    // updateTopics();
    server.send(200, "text/json", allTopics);
}


//*************************************
//***        MQTTT FUNCTIONS        ***
//*************************************

void adjustDeviceTime(String allValues) { // function called from MQTT
  int j = 0;
  int s = 0;
  String values[6];
  for (int i = 0; i < allValues.length(); i++) {
    if (allValues[i] == ',') {
      values[j] = allValues.substring(s, i);
      s = i + 1;
      j++;
    }
    if (i == allValues.length() - 1) {
      values[j] = allValues.substring(s);
    }
  }
  rtc.adjust(DateTime(
      values[0].toInt(),
      values[1].toInt(),
      values[2].toInt(),
      values[3].toInt(),
      values[4].toInt(),
      values[5].toInt()));
      publishDeviceTime = true;
}

void publishTime() {
  char publishTopic [60];
  String outputTopic = deviceTopic;
  outputTopic += "deviceTime";
  outputTopic.toCharArray(publishTopic, 60);
  char message [30];
  DateTime now = rtc.now();
  String time = "";
  time += now.year();
  time += "/";
  time += now.month();
  time += "/";
  time += now.day();
  time += " ";
  time += now.hour();
  time += ":";
  time += now.minute();
  time += ":";
  time += now.second();
  time.toCharArray(message, 30);
  client.publish(publishTopic, message);
  rtcEnabled = true;
}

void adjustRate2(String allValues) { // function called from MQTT
  loadRate2Time(allValues);
  saveTimeActivateRate2(allValues);
  publishRate2Values();
}

void adjustRate3(String allValues) { // function called from MQTT
  loadRate3Time(allValues);
  saveTimeActivateRate3(allValues);
  publishRate3Values();
}

void adjustResetMaxMinTime(String allValues) { // function called from MQTT
  int j = 0;
  int s = 0;
  String values[2];
  for (int i = 0; i < allValues.length(); i++) {
    if (allValues[i] == ',') {
      values[j] = allValues.substring(s, i);
      s = i + 1;
      j++;
    }
    if (i == allValues.length() - 1) {
      values[j] = allValues.substring(s);
    }
  }
  hourResetMaxMin = values[0].toInt();
  minResetMaxMin = values[1].toInt();
  publishResetMaxMin();
}

void adjustResetBillRatesTime(String allValues) { // function called from MQTT
  loadResetBillRatesTime(allValues);
  saveResetBillRatesInfo(allValues);
  publishResetBillRatesTime();
}

void publishRate2Values() {
  char publishTopic [60];
  String outputTopic = deviceTopic;
  outputTopic += "rate2Values";
  outputTopic.toCharArray(publishTopic, 60);
  char message [60];
  String values = "start ";
  values += hourStartRate2;
  values += ":";
  values += minStartRate2;
  values += ", stop ";
  values += hourStopRate2;
  values += ":";
  values += minStopRate2;
  values.toCharArray(message, 60);
  client.publish(publishTopic, message);
}

void publishRate3Values() {
  char publishTopic [60];
  String outputTopic = deviceTopic;
  outputTopic += "rate3Values";
  outputTopic.toCharArray(publishTopic, 60);
  char message [60];
  String values = "start ";
  values += hourStartRate3;
  values += ":";
  values += minStartRate3;
  values += ", stop ";
  values += hourStopRate3;
  values += ":";
  values += minStopRate3;
  values.toCharArray(message, 60);
  client.publish(publishTopic, message);
}

void publishActiveRate() {
  char publishTopic [60];
  String outputTopic = deviceTopic;
  outputTopic += "activeRate";
  outputTopic.toCharArray(publishTopic, 60);
  char message [60];
  String values = "";
  if (!rate2State && !rate3State) {
    values = "rate1 ";
  }
  if (rate2State) {
    values += "rate2 ";
  }
  if (rate3State) {
    values += "rate3 ";
  }
  values.toCharArray(message, 60);
  client.publish(publishTopic, message);
}

void publishResetBillRatesTime() {
  char publishTopic [60];
  String outputTopic = deviceTopic;
  outputTopic += "ResetRatesTime";
  outputTopic.toCharArray(publishTopic, 60);
  char message [120];
  String values = "every month / ";
  if (!isMonthlyResetRates) {
    values = "months [";
    values += monthResetRates[0];
    values += ",";
    values += monthResetRates[1];
    values += ",";
    values += monthResetRates[2];
    values += ",";
    values += monthResetRates[3];
    values += ",";
    values += monthResetRates[4];
    values += ",";
    values += monthResetRates[5];
    values += "] / ";
  }
  values += dayResetRates;
  values += " - ";
  values += hourResetRates;
  values += ":";
  values += minResetRates;
  values.toCharArray(message, 120);
  client.publish(publishTopic, message);
}

void publishResetMaxMin() {
  char publishTopic [60];
  String outputTopic = deviceTopic;
  outputTopic += "ResetMaxMinTime";
  outputTopic.toCharArray(publishTopic, 60);
  char message [60];
  String values = "time ";
  values += hourResetMaxMin;
  values += ":";
  values += minResetMaxMin;
  values.toCharArray(message, 60);
  client.publish(publishTopic, message);
}
void publishFirmwareVersion() {
  char publishTopic [60];
  String outputTopic = deviceTopic;
  outputTopic += "InstalledFirmwareVersion";
  outputTopic.toCharArray(publishTopic, 60);
  char message [30];
  firmwareVersion.toCharArray(message, 60);
  client.publish(publishTopic, message);
}

void publishMBIPaddress() {
  char publishTopic [60];
  String outputTopic = deviceTopic;
  outputTopic += "MBIPaddress";
  outputTopic.toCharArray(publishTopic, 60);
  char message [30];
  ModbusIPaddress.toCharArray(message, 60);
  client.publish(publishTopic, message);
}

// void publishSerialOperationalMode() {
//   char publishTopic [60];
//   String outputTopic = deviceTopic;
//   outputTopic += "serialMode";
//   outputTopic.toCharArray(publishTopic, 60);
//   String mode = "Operation";
//   if (serialMode) {
//     mode = "Serial";
//   }
//   char message [10];
//   mode.toCharArray(message, 10);
//   client.publish(publishTopic, message);
// }

void deviceInfo() {
  char publishTopic [60];
  char message [100];
  String isRegisteredTopic = masterTopic;
  String deviceInfo = deviceMAC;
  if(deviceRegistered == 'y') {
    isRegisteredTopic += "registered";
    isRegisteredTopic.toCharArray(publishTopic, 60);
  } else {
    isRegisteredTopic += "unregistered";
    isRegisteredTopic.toCharArray(publishTopic, 60);
  }
  deviceInfo += "|";
  deviceInfo += deviceName;
  deviceInfo += "|";
  deviceInfo += deviceIP;
  deviceInfo += "|";
  deviceInfo += ApiOwner;
  deviceInfo += "|";
  deviceInfo += firmwareVersion;
  deviceInfo.toCharArray(message, 100);
  Serial.println(message);
  client.publish(publishTopic, message);
}

void registerDevice() {
  char publishTopic [60];
  char message [60];
  String registeringDevice = masterTopic;
  registeringDevice += "registerAction";
  registeringDevice.toCharArray(publishTopic, 60);

  String deviceInfo = deviceMAC;
  deviceInfo += "|";
  deviceInfo += deviceIP;
  deviceInfo += "|";
  deviceInfo += deviceTopic;
  Serial.println(deviceInfo);
  String masterResponse = serverPost(ApiMasterRegisterDeviceURL, deviceInfo, "registerDevice master");

  masterResponse.toCharArray(message, 60);
  client.publish(publishTopic, message);

  String masterData[13];
  int j = 0;
  int s = 0;
  for (int i = 0; i < masterResponse.length(); i++) {
    if (masterResponse[i] == '|') {
      masterData[j] = masterResponse.substring(s, i);
      s = i + 1;
      j++;
    }
    if (i == masterResponse.length() - 1) {
      masterData[j] = masterResponse.substring(s);
    }
  }
  
  int MasterDeviceID = masterData[0].toInt();
  deviceName = masterData[1];
  ApiOwner = masterData[2];
  String allTopics = masterData[3];
  ClientName = masterData[4];
  FirebaseNotyURL = masterData[5];
  FireBaseAuthKey = masterData[6];

  MqttBrokerURL = masterData[7];
  MqttBrokerPort = masterData[8].toInt();
  MqttUser = masterData[9];
  MqttPassword = masterData[10];

  ModbusIPaddress = masterData[11];
  activeVarModIP = masterData[12].toInt();
  activeAlarmModIP = masterData[13].toInt();

  deviceInfo += "|";
  deviceInfo += MasterDeviceID;
  deviceInfo += "|";
  deviceInfo += allTopics;

  Serial.println(MasterDeviceID);
  Serial.println(deviceName);
  Serial.println(ApiOwner);
  Serial.println(allTopics);
  Serial.println(ClientName);
  Serial.println(FirebaseNotyURL);
  Serial.println(FireBaseAuthKey);
  Serial.println(MqttBrokerURL);
  Serial.println(MqttBrokerPort);
  Serial.println(MqttUser);
  Serial.println(MqttPassword);
  Serial.println(ModbusIPaddress);
  Serial.println(activeVarModIP);
  Serial.println(activeAlarmModIP);

  ApiOwnerRegisterDeviceURL = ApiOwner + "Device/Register";
  Serial.println(ApiOwnerRegisterDeviceURL);
  Serial.println(deviceInfo);
  String ownerDeviceID = serverPost(ApiOwnerRegisterDeviceURL, deviceInfo, "registerDevice Owner");

  ownerDeviceID.toCharArray(message, 10);
  client.publish(publishTopic, message);

  int odi = ownerDeviceID.toInt();
  saveDeviceID(odi);
  if( odi > 0) {
    saveDeviceRegistered('y');
  } else {
    saveDeviceRegistered('n');
  }
  saveApiOwner(ApiOwner);
  saveClientName(ClientName);
  saveFireBaseNotyURL(FirebaseNotyURL);
  saveFireBaseAuthKEY(FireBaseAuthKey);
  saveBrokerURL(MqttBrokerURL);
  saveBrokerPort(MqttBrokerPort);
  saveBrokerUser(MqttUser);
  saveBrokerPassword(MqttPassword);
  saveModbusIPAddress(ModbusIPaddress);
  saveModbusIPVarQuantity(activeVarModIP);
  saveModbusIPAlarmQuantity(activeAlarmModIP);
  saveTopics(allTopics);
}

void sendModbusResponse() {
  char publishTopic [60];
  String modbusResponseTopic = root_topic_config;
  modbusResponseTopic += "/modbusResponse";
  modbusResponseTopic.toCharArray(publishTopic, 60);
  char message [60];
  modbusResponse.toCharArray(message, 60);
  client.publish(publishTopic, message);
}

void saveAlarmConfigsResponse() {
  if(modbusResponse == "wirteModbus success") {
    String configs = "";
    configs += deviceID;
    configs += ",";
    configs += modbusRequest;
    Serial.println(configs);
    serverPost(ApiOwnerSaveDeviceConfigsURL, configs, "saving alarms");
    char publishTopic [60];
    String modbusResponseTopic = root_topic_config;
    modbusResponseTopic += "/savedAlarm";
    modbusResponseTopic.toCharArray(publishTopic, 60);
    char message [60];
    modbusResponse.toCharArray(message, 60);
    client.publish(publishTopic, message);
  }
}

void readAlarmsConfig() {
  String configs = "";
  if(modbusRequest == "1") {
    modbusRequest = "2AF8,10";
    readModbus();
    configs = modbusResponse;
  }
  else if(modbusRequest == "2") {
    modbusRequest = "2B02,10";
    readModbus();
    configs = modbusResponse;
  }
  else if(modbusRequest == "all") {
    modbusRequest = "2AF8,10";
    readModbus();
    configs = modbusResponse;
    modbusRequest = "2B02,10";
    readModbus();
    configs += modbusResponse;
  }
  char publishTopic [60];
  String modbusResponseTopic = root_topic_config;
  modbusResponseTopic += "/AlarmConfigs";
  modbusResponseTopic.toCharArray(publishTopic, 60);
  char message [100];
  configs.toCharArray(message, 100);
  Serial.println(message);
  client.publish(publishTopic, message);
}