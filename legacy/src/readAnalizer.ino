String RTValues;
String readRealTimetValues() {
  uint16_t  modbusAddresses[] = {0x00,0x30,0x200};
  uint8_t  quantity[] = {48,46,4};
  uint16_t data[50];
  RTValues = "";
  int readMaxTries = 2;
  int tries = 0;

  Serial.println("RTValues...");
  for (int i = 0; i < 3; i++) {
    uint8_t result = modbus.readHoldingRegisters(modbusAddresses[i], quantity[i]);
    // do something with data if read is successful
    while (result != modbus.ku8MBSuccess && tries <= readMaxTries)
    {
      Serial.print("result ");
      Serial.println(result);
      result = modbus.readHoldingRegisters(modbusAddresses[i], quantity[i]);
      Serial.print("try ");
      Serial.println(tries);
      tries++;
      delay(100);
    }
    tries = 0;
    if (result == modbus.ku8MBSuccess)
    {
      for (int j = 0; j < quantity[i]; j++)
      {
        data[j] = modbus.getResponseBuffer(j);
        RTValues += data[j];
        RTValues += ",";
      }
    }
    else {
      Serial.println("read RTValues FAILED........");
      break;
    }
  }
  // Serial.println(RTValues);
  // Serial.println();
  return RTValues;
}


String instantValues;
String readInstantValues() {
  uint16_t  modbusAddresses[] = {0x00,0x30,0x200};
  uint8_t  quantity[] = {48,46,4};
  uint16_t data[50];
  instantValues = "";
  int readMaxTries = 2;
  int tries = 0;

  Serial.println("instantValues...");
  for (int i = 0; i < 3; i++) {
    uint8_t result = modbus.readHoldingRegisters(modbusAddresses[i], quantity[i]);
    // do something with data if read is successful
    while (result != modbus.ku8MBSuccess && tries <= readMaxTries)
    {
      Serial.print("result ");
      Serial.println(result);
      result = modbus.readHoldingRegisters(modbusAddresses[i], quantity[i]);
      Serial.print("try ");
      Serial.println(tries);
      tries++;
      delay(100);
    }
    tries = 0;
    if (result == modbus.ku8MBSuccess)
    {
      for (int j = 0; j < quantity[i]; j++)
      {
        data[j] = modbus.getResponseBuffer(j);
        instantValues += data[j];
        instantValues += ",";
      }
    }
    else {
      Serial.println("read instantValues FAILED........");
      break;
    }
  }
  // Serial.println(instantValues);
  // Serial.println();
  return instantValues;
}

String maxValues;
String readMaxValues() {
  uint16_t  modbusAddresses[] = {0x106,0x136,0x204};
  uint8_t  quantity[] = {48,46,4};
  uint16_t data[50];
  maxValues = "";
  int readMaxTries = 2;
  int tries = 0;

  Serial.println("maxValues...");
  for (int i = 0; i < 3; i++) {
    uint8_t result = modbus.readHoldingRegisters(modbusAddresses[i], quantity[i]);
    // do something with data if read is successful
    while (result != modbus.ku8MBSuccess && tries <= readMaxTries)
    {
      Serial.print("result ");
      Serial.println(result);
      result = modbus.readHoldingRegisters(modbusAddresses[i], quantity[i]);
      Serial.print("try ");
      Serial.println(tries);
      tries++;
      delay(100);
    }
    tries = 0;
    if (result == modbus.ku8MBSuccess)
    {
      for (int j = 0; j < quantity[i]; j++)
      {
        data[j] = modbus.getResponseBuffer(j);
        maxValues += data[j];
        maxValues += ",";
      }
    }
    else {
      Serial.println("read maxValues FAILED........");
      eventLocalLogger = "read maxValues FAILED";
      break;
    }
  }
  Serial.println(maxValues);
  Serial.println();
  return maxValues;
}

String minValues;
String readMinValues() {
  uint16_t  modbusAddresses[] = {0x164,0x194};
  uint8_t  quantity[] = {48,34};
  uint16_t data[50];
  minValues = "";
  int readMaxTries = 2;
  int tries = 0;

  Serial.println("minValues...");
  for (int i = 0; i < 2; i++) {
    uint8_t result = modbus.readHoldingRegisters(modbusAddresses[i], quantity[i]);
    // do something with data if read is successful
    while (result != modbus.ku8MBSuccess && tries <= readMaxTries)
    {
      Serial.print("result ");
      Serial.println(result);
      result = modbus.readHoldingRegisters(modbusAddresses[i], quantity[i]);
      Serial.print("try ");
      Serial.println(tries);
      tries++;
      delay(100);
    }
    tries = 0;
    if (result == modbus.ku8MBSuccess)
    {
      for (int j = 0; j < quantity[i]; j++)
      {
        data[j] = modbus.getResponseBuffer(j);
        minValues += data[j];
        minValues += ",";
      }
    }
    else {
      Serial.println("read minValues FAILED........");
      eventLocalLogger = "read minValues FAILED";
      break;
    }
  }
  Serial.println(minValues);
  Serial.println();
  return minValues;
}


String armonicValues;
String readArmonics(armonics key) {
  uint16_t  modbusAddress = 0;
  uint8_t  quantity = 32;
  uint16_t data[50];
  armonicValues = "";
  int readMaxTries = 2;
  int tries = 0; 
  switch (key)
  {
    case V1:
      modbusAddress = 0xA28;
      break;
    case V2:
      modbusAddress = 0xA48;
      break;
    case V3:
      modbusAddress = 0xA68;
      break;
    case I1:
      modbusAddress = 0xA88;
      break;
    case I2:
      modbusAddress = 0xAA8;
      break;
    case I3:
      modbusAddress = 0xAC8;
      break;
    
    default:
      break;
  }
  Serial.print("armonicValues...");
  Serial.println(key);
  Serial.println(modbusAddress);
  uint8_t result = modbus.readHoldingRegisters(modbusAddress, quantity);
  // do something with data if read is successful
  while (result != modbus.ku8MBSuccess && tries <= readMaxTries)
  {
    Serial.print("result ");
    Serial.println(result);
    result = modbus.readHoldingRegisters(modbusAddress, quantity);
    Serial.print("try ");
    Serial.println(tries);
    tries++;
    delay(100);
  }
  tries = 0;
  if (result == modbus.ku8MBSuccess)
  {
    for (int j = 0; j < quantity; j++)
    {
      data[j] = modbus.getResponseBuffer(j);
      armonicValues += data[j];
      armonicValues += ",";
    }
  }
  else {
    Serial.println("read armonicValues FAILED........");
    eventLocalLogger = "read armonicValues FAILED";
    return "";
  }
  Serial.println(armonicValues);
  Serial.println();
  return armonicValues;
}

String rateValues;
String readRates(rates key) {
  uint16_t  modbusAddress = 0;
  uint8_t  quantity = 42;
  uint16_t data[50];
  rateValues = "";
  int readMaxTries = 2;
  int tries = 0; 
  switch (key)
  {
    case R1:
      modbusAddress = 0x5E;
      break;
    case R2:
      modbusAddress = 0x88;
      break;
    case R3:
      modbusAddress = 0xB2;
      break;
    case RT:
      modbusAddress = 0xDC;
      break;        
    default:
      break;
  }
  Serial.print("rateValues...");
  Serial.println(key);
  uint8_t result = modbus.readHoldingRegisters(modbusAddress, quantity);
  // do something with data if read is successful
  while (result != modbus.ku8MBSuccess && tries <= readMaxTries)
  {
    Serial.print("result ");
    Serial.println(result);
    result = modbus.readHoldingRegisters(modbusAddress, quantity);
    Serial.print("try ");
    Serial.println(tries);
    tries++;
    delay(100);
  }
  tries = 0;
  if (result == modbus.ku8MBSuccess)
  {
    for (int j = 0; j < quantity; j++)
    {
      data[j] = modbus.getResponseBuffer(j);
      rateValues += data[j];
      rateValues += ",";
    }
  }
  else {
    Serial.println("read rateValues FAILED........");
    eventLocalLogger = "read rateValues FAILED";
    return "";
  }
  Serial.println(rateValues);
  Serial.println();
  return rateValues;
}


String alarmValues;
String readAlarmValues() {
  uint16_t  modbusAddresses = 0x4E21;
  uint8_t  quantity = 1;
  uint16_t data;
  alarmValues = "";
  int readMaxTries = 2;
  int tries = 0;

  Serial.println("alarmValues...");
  uint8_t result = modbus.readHoldingRegisters(modbusAddresses, quantity);
    // do something with data if read is successful
    while (result != modbus.ku8MBSuccess && tries <= readMaxTries)
    {
      Serial.print("result ");
      Serial.println(result);
      result = modbus.readHoldingRegisters(modbusAddresses, quantity);
      Serial.print("try ");
      Serial.println(tries);
      tries++;
      delay(100);
    }
    tries = 0;
    if (result == modbus.ku8MBSuccess)
    {
      data = modbus.getResponseBuffer(0);
      alarmValues += data;
    }
    else {
      Serial.println("read alarmValues FAILED........");
    }
  Serial.println(alarmValues);
  Serial.println();
  return alarmValues;
}


String resetResponse;
void write_ResetParam(uint16_t paramAddress, String ParamName) {
  uint16_t  modbusAddresses = paramAddress;
  uint16_t data;
  resetResponse = "";
  int readMaxTries = 2;
  int tries = 0;

  Serial.print(ParamName);
  Serial.println(" reset response...");
  uint8_t result = modbus.writeSingleCoil(modbusAddresses, 1);
    // do something with data if read is successful
    while (result != modbus.ku8MBSuccess && tries <= readMaxTries)
    {
      Serial.print("result ");
      Serial.println(result);
      result = modbus.writeSingleCoil(modbusAddresses, 1);
      Serial.print("try ");
      Serial.println(tries);
      tries++;
      delay(100);
    }
    tries = 0;
    if (result == modbus.ku8MBSuccess)
    {
      data = modbus.getResponseBuffer(0);
      resetResponse += data;
    }
    else {
      Serial.print(ParamName);
      Serial.println(" write resetResponse FAILED........");
    }
  Serial.println(resetResponse);
  Serial.println();
}

void writeModbus() {
  String addressQuantityValues = modbusRequest;
  int j = 0;
  int s = 0;
  String params[3]; // address, quantity, value|value|value
  char address[5];
  // split data by ,
  for (int i = 0; i < addressQuantityValues.length(); i++) {
    if (addressQuantityValues[i] == ',') {
      params[j] = addressQuantityValues.substring(s, i);
      s = i + 1;
      j++;
    }
    if (i == addressQuantityValues.length() - 1) {
      params[j] = addressQuantityValues.substring(s);
    }
  };
  // build parameters address and quantity
  params[0].toCharArray(address, 5);
  unsigned long hexAddress = strtoul(address, NULL, 16);
  int quantity = params[1].toInt();
  Serial.println(params[0]);
  Serial.println(address);
  Serial.println(hexAddress);
  Serial.println(quantity);

  j = 0;
  s = 0;
  String value;
  for (int i = 0; i < params[2].length(); i++) {
    if (params[2][i] == '|') {
      value = params[2].substring(s, i);
      modbus.setTransmitBuffer(j, value.toInt());
      s = i + 1;
      j++;
      Serial.println(value);
    }
    if (i == params[2].length() - 1) {
      value = params[2].substring(s);
      modbus.setTransmitBuffer(j, value.toInt());
      Serial.println(value);
    }
  };
    // slave: write TX buffer to (2) 16-bit registers starting at register 0
    uint8_t result = modbus.writeMultipleRegisters(hexAddress, quantity);
    if (result == modbus.ku8MBSuccess) {
      modbusResponse = "wirteModbus success";
    } else {
      modbusResponse = "wirteModbus failed ";
      modbusResponse += result;
    }
    writeModbusMQTTRequest = false;
    Serial.print(result);
}

void readModbus() {
  modbusResponse = "readModbus failed";
  String addressQuantity = modbusRequest;
  String modbusValues;
  int j = 0;
  int s = 0;
  String params[2]; // address, quantity
  char address[5];
  uint16_t data[50];
  int readMaxTries = 2;
  int tries = 0;
  // split data by ,
  for (int i = 0; i < addressQuantity.length(); i++) {
    if (addressQuantity[i] == ',') {
      params[j] = addressQuantity.substring(s, i);
      s = i + 1;
      j++;
    }
    if (i == addressQuantity.length() - 1) {
      params[j] = addressQuantity.substring(s);
    }
  };
  // build parameters address and quantity
  params[0].toCharArray(address, 5);
  unsigned long hexAddress = strtoul(address, NULL, 16);
  int quantity = params[1].toInt();
  uint8_t result = modbus.readHoldingRegisters(hexAddress, quantity);
  while (result != modbus.ku8MBSuccess && tries <= readMaxTries)
    {
      Serial.print("result ");
      Serial.println(result);
      result = modbus.readHoldingRegisters(hexAddress, quantity);
      Serial.print("try ");
      Serial.println(tries);
      tries++;
      delay(100);
    }
    tries = 0;
  if (result == modbus.ku8MBSuccess)
  {
    for (int j = 0; j < quantity; j++)
    {
      data[j] = modbus.getResponseBuffer(j);
      modbusValues += data[j];
      modbusValues += ",";
    }
    modbusResponse = modbusValues;
  }
  Serial.print("modbus Response: ");
  Serial.println(modbusValues);
  
}