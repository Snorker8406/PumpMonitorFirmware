
#include <ETH.h>
#include <WiFiClientSecure.h>
#include "ModbusMaster.h" //https://github.com/4-20ma/ModbusMaster
#include <PubSubClient.h>
#include "ArduinoJson.h"
#include <HTTPClient.h>
#include "RTClib.h"
#include <WebServer.h>
#include <ESPmDNS.h>
#include <EEPROM.h>
#include <ModbusIP_ESP8266.h>

// project libreries
#include "SDhelper.h"

#define EEPROM_SIZE 1170
#define EEPROM_TOPICS_ADDRESS 0
#define EEPROM_API_OWNER_ADDRESS 200
#define EEPROM_DEVICE_ID_ADDRESS 280
#define EEPROM_DEVICE_REGISTERED_ADDRESS 290
#define EEPROM_CLIENT_NAME_ADDRESS 320
#define EEPROM_FIREBASE_URL_ADDRESS 350
#define EEPROM_FIREBASE_KEY_ADDRESS 400
#define EEPROM_TIME_ACTIVATE_RATE2_ADDRESS 800
#define EEPROM_TIME_ACTIVATE_RATE3_ADDRESS 850
#define EEPROM_RESET_BILL_RATES_INFO_ADDRESS 900
#define EEPROM_MQTT_BROKER_URL 950
#define EEPROM_MQTT_BROKER_PORT 1030
#define EEPROM_MQTT_USER 1060
#define EEPROM_MQTT_PASSWORD 1090
#define EEPROM_MODBUS_IP_ADDRESS 1120
#define EEPROM_MODBUS_IP_VAR_QUANTITY 1150
#define EEPROM_MODBUS_IP_ALARM_QUANTITY 1160
//***************************************
//*********** ESP32 PINOUT **************
//***************************************

#define RXD2                36
#define TXD2                4
// #define MAX485_RE_NEG       5 //D5 RS485 has a enable/disable pin to transmit or receive data. Arduino Digital Pin 2 = Rx/Tx 'Enable'; High to Transmit, Low to Receive
#define Slave_ID            1
#define relay_start         32
#define relay_stop          33
#define pumpState           34
#define relay_rateSelector2 17
#define relay_rateSelector3 5
#define overload_input      39
#define alarm1_input_SrlMd  35
#define alarm2_input        1

//**************************************
//******   RESET ADDRESSES   ***********
//**************************************
#define reset_Energies                  0x0834
#define reset_MaxMin                    0x0838
#define reset_InitMaxDemand             0x0839
#define reset_RatesCounters             0x083D
#define reset_MaxMinDemandValue         0x083F
#define reset_AllEnergyRatesMaxDemandMin  0x0848


//***************************************
//*********** MQTT CONFIGS **************
//***************************************

char mqtt_server[30] = "mqtt.agrotecsa.com.mx";
int mqtt_port = 8883;
char mqtt_user[30] = "PM_New";
char mqtt_pass[30] = "AgroTECSA321";

// const char *mqtt_server = "ioticos.org";
// const int mqtt_port = 1883;

// const char *mqtt_server = "broker.mqttdashboard.com";
// const int mqtt_port = 1883;
// char *root_topic_pumpAction = "kl9GXL64poVzRn9/pumpAction";
// char *root_topic_pumpState = "kl9GXL64poVzRn9/pumpState";
// char *root_topic_getState = "kl9GXL64poVzRn9/getState";
char root_topic_pumpAction[100];
char root_topic_pumpState[100];
char root_topic_getState[100];
char root_topic_config[100];
// Topic Default
String deviceTopic;
char root_topic_device[100];
char *root_topic_master = "master/#";
String masterTopic = "master/";

//*************************************
//*****     MODBUS IP CONFIGS     *****
//*************************************
const int REG[20] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};   // Modbus Hreg Offset
uint16_t RES[10][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};
float VAL[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
String VALBuff[10] = {"", "", "", "", "", "", "", "", "", ""};

const int REG_AL[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};   // Modbus Coils addresses
bool RES_AL[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
bool RES_AL_PREV[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

IPAddress remoteIP;        // Address of Modbus Slave device
// IPAddress remote(192, 168, 1, 91);
int activeVarModIP = 0; // 1 to 10
int activeAlarmModIP = 0; // 1 to 10
int activedAlarm = 0;
// period frecuency for handle MODBUS IP
int periodModbusIP = 1000; // 2seg
bool activeModbusIP = false;
int ModbusIPtryCount = 5;
ModbusIP mb;  //ModbusIP object
// TaskHandle_t modbusIP_Task;
//**************************************
//***********  GLOBALS   ***************
//**************************************
// bool serialMode = 0;
WiFiClientSecure espClient;
PubSubClient client(espClient);
WebServer server(80);
RTC_DS3231 rtc;
ModbusMaster modbus;
TaskHandle_t Monitor_Task;

// Certificado raíz del broker MQTT (EMQ RootCA)
const char* root_ca = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDUTCCAjmgAwIBAgIJAPPYCjTmxdt/MA0GCSqGSIb3DQEBCwUAMD8xCzAJBgNV\n" \
"BAYTAkNOMREwDwYDVQQIDAhoYW5nemhvdTEMMAoGA1UECgwDRU1RMQ8wDQYDVQQD\n" \
"DAZSb290Q0EwHhcNMjAwNTA4MDgwNjUyWhcNMzAwNTA2MDgwNjUyWjA/MQswCQYD\n" \
"VQQGEwJDTjERMA8GA1UECAwIaGFuZ3pob3UxDDAKBgNVBAoMA0VNUTEPMA0GA1UE\n" \
"AwwGUm9vdENBMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAzcgVLex1\n" \
"EZ9ON64EX8v+wcSjzOZpiEOsAOuSXOEN3wb8FKUxCdsGrsJYB7a5VM/Jot25Mod2\n" \
"juS3OBMg6r85k2TWjdxUoUs+HiUB/pP/ARaaW6VntpAEokpij/przWMPgJnBF3Ur\n" \
"MjtbLayH9hGmpQrI5c2vmHQ2reRZnSFbY+2b8SXZ+3lZZgz9+BaQYWdQWfaUWEHZ\n" \
"uDaNiViVO0OT8DRjCuiDp3yYDj3iLWbTA/gDL6Tf5XuHuEwcOQUrd+h0hyIphO8D\n" \
"tsrsHZ14j4AWYLk1CPA6pq1HIUvEl2rANx2lVUNv+nt64K/Mr3RnVQd9s8bK+TXQ\n" \
"KGHd2Lv/PALYuwIDAQABo1AwTjAdBgNVHQ4EFgQUGBmW+iDzxctWAWxmhgdlE8Pj\n" \
"EbQwHwYDVR0jBBgwFoAUGBmW+iDzxctWAWxmhgdlE8PjEbQwDAYDVR0TBAUwAwEB\n" \
"/zANBgkqhkiG9w0BAQsFAAOCAQEAGbhRUjpIred4cFAFJ7bbYD9hKu/yzWPWkMRa\n" \
"ErlCKHmuYsYk+5d16JQhJaFy6MGXfLgo3KV2itl0d+OWNH0U9ULXcglTxy6+njo5\n" \
"CFqdUBPwN1jxhzo9yteDMKF4+AHIxbvCAJa17qcwUKR5MKNvv09C6pvQDJLzid7y\n" \
"E2dkgSuggik3oa0427KvctFf8uhOV94RvEDyqvT5+pgNYZ2Yfga9pD/jjpoHEUlo\n" \
"88IGU8/wJCx3Ds2yc8+oBg/ynxG8f/HmCC1ET6EHHoe2jlo8FpU/SgGtghS1YL30\n" \
"IWxNsPrUP+XsZpBJy/mvOhE5QXo6Y35zDqqj8tI7AGmAWu22jg==\n" \
"-----END CERTIFICATE-----\n";

enum armonics {V1, V2, V3, I1, I2, I3};
enum rates {R1, R2, R3, RT};
enum eventTypes {zero, localON, localOFF, remoteON, remoteOFF, alarm1, alarm2, overload, alarmIP};
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
int pumpLastState = 2;
int overloadLastState = 2;
int alarm1LastState = 2;
int alarm2LastState = 2;
String deviceMAC;
String deviceIP;
eventTypes eventLogger = zero;
String eventLocalLogger = "";
String eventResetParam = "";
String eventForceSaveType = "";
bool forceReadValues = false;
String UpdateHistoryFileName = "";
bool publishDeviceTime = false;
bool isTimeToUpdateHistory = false;
bool isRemote = false;
char deviceRegistered = false;
static bool eth_connected = false;
static bool rtcEnabled = false;
static bool rs485Enabled = false;
static bool sdEnabled = false;
static bool realTimeEnabled = true;
static bool isJustRestarted = true;
static bool isJustRestartedOverload = true;
static bool isJustRestartedAlarm1 = true;
static bool isJustRestartedAlarm2 = true;
// String lastAlarmState = "-1";
// String alarmInstantRead = "";
bool isOverload = false;
bool isAlarm1 = false;
bool isAlarm2 = false;

static bool readModbusMQTTRequest = false;
static bool writeModbusMQTTRequest = false;
static bool writeAlarmConfigsMQTTRequest = false;
static bool readAlarmConfigsMQTTRequest = false;
String modbusRequest = "";
String modbusResponse = "";
static bool isInstallingFirmware = false;
String firmwareVersionToInstall = "";

// start stop secuency
bool sw1 = false;
bool sw2 = false;
bool sw3 = false;

// rate states
bool rate2State = false;
bool rate3State = false;

//**************************************
//********  CONFIGURATIONS  ************
//**************************************
String firmwareVersion = "Jor5";
int deviceID = 1;
String deviceName = "PM_";
String ApiOwner = "";
String hostMaster = "castaindigo-001-site6.itempurl.com";
String ApiMaster = "http://" + hostMaster + "/api/";
String ApiRTURL = ApiOwner + "RealTime";
String ApiHistoryInstantURL = ApiOwner + "History/PostInstant";
String ApiHistoryMaxMinRatesURL = ApiOwner + "History/PostMaxMinRates";
String ApiHistoryLogEventURL = ApiOwner + "History/PostLogEvent";
// String ApiHistoryLogErrorURL = ApiOwner + "History/PostLogError";
String ApiOwnerRegisterDeviceURL = ApiOwner + "Device/Register";
String ApiOwnerUpdateHistoryURL = ApiOwner + "History/UpdateHistory";
String ApiOwnerPushNotificationURL = ApiOwner + "Firebase/PushNotification";
String ApiOwnerSaveDeviceConfigsURL = ApiOwner + "ConfigDevice/saveDeviceConfigs";
String ApiMasterRegisterDeviceURL = ApiMaster + "Device/Register";
String ApiMasterUpdateTopicsURL = ApiMaster + "MQTTTopics/CreateDeviceTopics";
String InstallFirmwareURL = "/api/Device/InstallFirmware/";
String MqttBrokerURL = "";
int MqttBrokerPort = 1883;
String MqttUser = "";
String MqttPassword = "";
int MqttConnTry = 5;
String ModbusIPaddress = "";
String ClientName;
String FirebaseNotyURL;
String FireBaseAuthKey;

const char* host = "agrotecsa";
int hourStartRate2 = 0;
int minStartRate2 = 0;
int hourStopRate2 = 0;
int minStopRate2 = 0;
int hourStartRate3 = 0;
int minStartRate3 = 0;
int hourStopRate3 = 0;
int minStopRate3 = 0;

int hourResetMaxMin = 0;
int minResetMaxMin = 0;

int monthResetRates[6] = {1,3,5,7,9,12};
int dayResetRates = 1;
int hourResetRates = 0;
int minResetRates = 0;
bool isMonthlyResetRates = true;

//*******************************
//***       WIFI EVENT        ***
//*******************************

void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case SYSTEM_EVENT_ETH_START:
      Serial.println("ETH Started");
      //set eth hostname here
      ETH.setHostname("esp32-ethernet");
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
      deviceMAC = ETH.macAddress();
      deviceIP = ETH.localIP().toString();
      Serial.print("ETH MAC: ");
      Serial.print(deviceMAC);
      Serial.print(", IPv4: ");
      Serial.print(deviceIP);

      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      eth_connected = true;
      setup_Topics();
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case SYSTEM_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    case SYSTEM_EVENT_STA_GOT_IP:
      deviceMAC = WiFi.macAddress();
      deviceIP = WiFi.localIP().toString();
      Serial.print("ETH MAC: ");
      Serial.print(deviceMAC);
      Serial.print(", IPv4: ");
      Serial.println(deviceIP);
      eth_connected = true;
      setup_Topics();
      break;
    default:
      break;
  }
}

//********************************
//***       LOAD TOPICS        ***
//********************************

void loadTopics(String allTopics) {
  String topics[4];
  int j = 0;
  int s = 0;
  for (int i = 0; i < allTopics.length(); i++) {
    if (allTopics[i] == ',') {
      topics[j] = allTopics.substring(s, i);
      s = i + 1;
      j++;
    }
    if (i == allTopics.length() - 1) {
      topics[j] = allTopics.substring(s);
    }
  }
  topics[0].toCharArray(root_topic_pumpAction, 100);
  topics[1].toCharArray(root_topic_pumpState, 100);
  topics[2].toCharArray(root_topic_getState, 100);
  topics[3].toCharArray(root_topic_config, 100);
  Serial.println(root_topic_pumpAction);
  Serial.println(root_topic_pumpState);
  Serial.println(root_topic_getState);
  Serial.println(root_topic_config);
}

//********************************
//***     LOAD RATES2 INFO     ***
//********************************

void loadRate2Time(String allValues) {
  // startHour,startMin,stopHour,stopMin
  int j = 0;
  int s = 0;
  String values[4];
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
  
  hourStartRate2 = values[0].toInt();
  minStartRate2 = values[1].toInt();
  hourStopRate2 = values[2].toInt();
  minStopRate2 = values[3].toInt();
}

//********************************
//***     LOAD RATES3 INFO     ***
//********************************

void loadRate3Time(String allValues) {
  // startHour,startMin,stopHour,stopMin
  int j = 0;
  int s = 0;
  String values[4];
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
  
  hourStartRate3 = values[0].toInt();
  minStartRate3 = values[1].toInt();
  hourStopRate3 = values[2].toInt();
  minStopRate3 = values[3].toInt();
}

//**************************************
//***     LOAD MODBUS IP ADDRESS     ***
//**************************************

void loadModbusIPaddress(String address) {
  // 192.168.1.1
  int j = 0;
  int s = 0;
  String values[4];
  for (int i = 0; i < address.length(); i++) {
    if (address[i] == '.') {
      values[j] = address.substring(s, i);
      s = i + 1;
      j++;
    }
    if (i == address.length() - 1) {
      values[j] = address.substring(s);
    }
  }
  remoteIP = IPAddress(values[0].toInt(), values[1].toInt(), values[2].toInt(), values[3].toInt());
  // remoteIP = IPAddress(192, 168, 0, 101);
}

//********************************
//***   LOAD BILL RATES INFO   ***
//********************************

void loadResetBillRatesTime(String allValues) {
  int j = 0;
  int s = 0;
  String values[4];
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
  isMonthlyResetRates = values[0].toInt() == 3; // 3 = reset monthly, 1 or 2 = reset every 2 months
  if (!isMonthlyResetRates) // if is every 2 months, load pased months
  {
    j = 2;
    for (int i = 0; i < 6; i++) {
      if (values[0].toInt() == 2) { // if first value is = 2 reset months will be feb, mar, apr...
        monthResetRates[i] = j;
      } else {
        monthResetRates[i] = j - 1;
      }
      j += 2;
    }
  }
  dayResetRates = values[1].toInt();
  hourResetRates = values[2].toInt();
  minResetRates = values[3].toInt();
}

//**********************************
//***   SAVE DEVICE REGISTERED   ***
//**********************************
void saveDeviceRegistered(char registeredStatus) {
  EEPROM.writeChar(EEPROM_DEVICE_REGISTERED_ADDRESS, registeredStatus);
  EEPROM.commit();
}

//**********************************
//***       SAVE DEVICE ID       ***
//**********************************
void saveDeviceID(int deviceID) {
  EEPROM.writeInt(EEPROM_DEVICE_ID_ADDRESS, deviceID);
  EEPROM.commit();
}

//**********************************
//***      SAVE API OWNER        ***
//**********************************
void saveApiOwner(String ApiOwner) {
  EEPROM.writeString(EEPROM_API_OWNER_ADDRESS, ApiOwner);
  EEPROM.commit();
}

//**********************************
//***        SAVE TOPICS         ***
//**********************************
void saveTopics(String allTopics) {
  EEPROM.writeString(EEPROM_TOPICS_ADDRESS, allTopics);
  EEPROM.commit();
  ESP.restart();
}

//**********************************
//***      SAVE CLIENT NAME      ***
//**********************************
void saveClientName(String clientName) {
  EEPROM.writeString(EEPROM_CLIENT_NAME_ADDRESS, clientName);
  EEPROM.commit();
}

//**********************************
//***      SAVE FIREBASE URL     ***
//**********************************
void saveFireBaseNotyURL(String url) {
  EEPROM.writeString(EEPROM_FIREBASE_URL_ADDRESS, url);
  EEPROM.commit();
}

//**********************************
//***      SAVE FIREBASE KEY     ***
//**********************************
void saveFireBaseAuthKEY(String key) {
  EEPROM.writeString(EEPROM_FIREBASE_KEY_ADDRESS, key);
  EEPROM.commit();
}

//**********************************
//** SAVE TIME TO ACTIVATE RATE2 ***
//**********************************
void saveTimeActivateRate2(String key) {
  EEPROM.writeString(EEPROM_TIME_ACTIVATE_RATE2_ADDRESS, key);
  EEPROM.commit();
}

//**********************************
//** SAVE TIME TO ACTIVATE RATE3 ***
//**********************************
void saveTimeActivateRate3(String key) {
  EEPROM.writeString(EEPROM_TIME_ACTIVATE_RATE3_ADDRESS, key);
  EEPROM.commit();
}

//**********************************
//*** SAVE RESET BILL RATES INFO ***
//**********************************
void saveResetBillRatesInfo(String key) {
  EEPROM.writeString(EEPROM_RESET_BILL_RATES_INFO_ADDRESS, key);
  EEPROM.commit();
}

// //**************************************
// //** SAVE SERIAL OR OPERATIONAL MODE ***
// //**************************************
// void saveSerialOperationalMode(int value) {
//   EEPROM.writeInt(EEPROM_SERIAL_OPERATIONAL_MODE, value);
//   EEPROM.commit();
// }

//**************************************
//******* SAVE MODBUS IP ADDRESS *******
//**************************************
void saveModbusIPAddress(String key) {
  EEPROM.writeString(EEPROM_MODBUS_IP_ADDRESS, key);
  EEPROM.commit();
}

//**************************************
//*******     SAVE BROKER URL    *******
//**************************************
void saveBrokerURL(String key) {
  Serial.print("saving BROKER URL ------> ");
  Serial.println(key);
  EEPROM.writeString(EEPROM_MQTT_BROKER_URL, key);
  EEPROM.commit();
}

//**************************************
//*******     SAVE BROKER PORT   *******
//**************************************
void saveBrokerPort(int port) {
  Serial.print("saving BROKER PORT ------> ");
  Serial.println(port);
  EEPROM.writeInt(EEPROM_MQTT_BROKER_PORT, port);
  EEPROM.commit();
}

//**************************************
//*******     SAVE BROKER USER   *******
//**************************************
void saveBrokerUser(String key) {
  Serial.print("saving BROKER USER ------> ");
  Serial.println(key);
  EEPROM.writeString(EEPROM_MQTT_USER, key);
  EEPROM.commit();
}

//**************************************
//******   SAVE BROKER PASSWORD  *******
//**************************************
void saveBrokerPassword(String key) {
  Serial.print("saving BROKER Password ------> ");
  Serial.println(key);
  EEPROM.writeString(EEPROM_MQTT_PASSWORD, key);
  EEPROM.commit();
}

//*******************************************
//******* SAVE MODBUS IP VAR QUANTITY *******
//*******************************************
void saveModbusIPVarQuantity(int quantity) {
  Serial.print("saving MBIP QTY ------> ");
  Serial.println(quantity);
  EEPROM.writeInt(EEPROM_MODBUS_IP_VAR_QUANTITY, quantity);
  EEPROM.commit();
}

//*********************************************
//******* SAVE MODBUS IP ALARM QUANTITY *******
//*********************************************
void saveModbusIPAlarmQuantity(int quantity) {
  Serial.print("saving Alarm MBIP QTY ------> ");
  Serial.println(quantity);
  EEPROM.writeInt(EEPROM_MODBUS_IP_ALARM_QUANTITY, quantity);
  EEPROM.commit();
}

//**********************************
//***       UPDATE TOPICS        ***
//**********************************

void updateTopics() {
  if(eth_connected) {
    String allTopics = serverPost(ApiMasterUpdateTopicsURL, deviceMAC, "updateTopics");
    saveTopics(allTopics);
  }
}

//*********************************
//***       TOPICS SETUP        ***
//*********************************

void setup_Topics() {
  if(eth_connected) {
    // default device topic
    deviceTopic =  "device/";
    deviceTopic += deviceMAC.substring(0,2);
    deviceTopic += deviceMAC.substring(3,5);
    deviceTopic += deviceMAC.substring(6,8);
    deviceTopic += deviceMAC.substring(9,11);
    deviceTopic += "/";
    String allMessages = deviceTopic;
    allMessages += "#";
    allMessages.toCharArray(root_topic_device, 100);
    if (deviceRegistered == 'y') {
      String allTopics = EEPROM.readString(EEPROM_TOPICS_ADDRESS);
      loadTopics(allTopics);
    }
  }
}


//*********************************
//***         API SETUP         ***
//*********************************

void setup_API() {
  ApiOwner = EEPROM.readString(EEPROM_API_OWNER_ADDRESS);
  // ApiOwner = "https://192.168.1.78:45456/api/";
  ApiRTURL = ApiOwner + "RealTime";
  ApiHistoryInstantURL = ApiOwner + "History/PostInstant";
  ApiHistoryMaxMinRatesURL = ApiOwner + "History/PostMaxMinRates";
  ApiHistoryLogEventURL = ApiOwner + "History/PostLogEvent";
  ApiOwnerUpdateHistoryURL = ApiOwner + "History/UpdateHistory";
  ApiOwnerPushNotificationURL = ApiOwner + "Firebase/PushNotification";
  ApiOwnerSaveDeviceConfigsURL = ApiOwner + "ConfigDevice/saveDeviceConfigs";
  //ApiHistoryLogErrorURL = ApiOwner + "History/PostLogError";
  ApiOwnerRegisterDeviceURL = ApiOwner + "Device/Register";
  
  
}

//*********************************************
//***         WiFi CONNECTION SETUP         ***
//*********************************************
// const char* ssid = "Verizon-MiFi6620L-C852"; 
// const char* pass = "dd6cecaa";
const char* ssid = "INFINITUM4F66_2.4"; 
const char* pass = "uTTuLH4tdt"; 

void setup_WiFiConnection() {

  if ((WiFi.status() != WL_CONNECTED) && !eth_connected) {             //if we are not connected (when WL_CONNECTED =1 we have a successful connection)
    WiFi.begin(ssid, pass);                        //initialize wifi connection

    for(int16_t i = 0; i < 400; i++){
      if ((WiFi.status() != WL_CONNECTED) && !eth_connected) {        //while we are not connected
        Serial.print(".");                           //print "........connected"
        delay(50);                                   //a single dont is printed every 50 ms
      }else{
        break;
      }
    }
    
    if(WiFi.status() == WL_CONNECTED){
      Serial.println("Wifi connected");            //once connected print "connected"
    } else {
      Serial.println("Failed to connect to Wifi");
    }
  }
}

//*********************************
//***     MODBUS IP SETUP       ***
//*********************************

void setup_ModbusIP() {
  if (ModbusIPaddress != "NOMBIP" && ModbusIPaddress != "") {
    loadModbusIPaddress(ModbusIPaddress);
    mb.client();
    activeModbusIP = true;
  } else {
    activeModbusIP = false;
  }
  // xTaskCreatePinnedToCore(loop_modbus, "Task_DrainSensors", 2000, NULL, 0, &modbusIP_Task, 1);
}

//*********************************
//***       EEPROM SETUP        ***
//*********************************

void setup_EEPROM() {
  if (!EEPROM.begin(EEPROM_SIZE))
  {
    // Serial.println("Failed to initialise EEPROM");
    // Serial.println("Restarting...");
    delay(1000);
    ESP.restart();
  }
  // String topics = "kl9GXL64poVzRn9/pumpAction,kl9GXL64poVzRn9/pumpState,kl9GXL64poVzRn9/getState";
  // EEPROM.writeString(EEPROM_TOPICS_ADDRESS, topics);
  // EEPROM.commit();
}

//*********************************
//***       SERVER SETUP        ***
//*********************************

void setup_Webserver() {
   // Web Server setup
    if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("MDNS responder started");
    Serial.print("You can now connect to http://");
    Serial.print(host);
    Serial.println(".local");
  }
  // responses for data
  server.on("/setModbus", HTTP_GET, setModbusRegister);
  server.on("/getModbus", HTTP_GET, getModbusRegister);
  server.on("/getRates", HTTP_GET, getRates);
  server.on("/setRates", HTTP_GET, setRates);
  server.on("/switchRate", HTTP_GET, switchRate);
  server.on("/setRate2Time", HTTP_GET, setRate2Time);
  server.on("/setTime", HTTP_GET, setTime);
  server.on("/getDeviceTime", HTTP_GET, getDeviceTime);
  server.on("/setTopics", HTTP_GET, setTopics);

  server.begin();
  Serial.println("HTTP server started");
}

//*******************************
//***       MAIN SETUP        ***
//*******************************

void setup()
{
  // relay configuration and turned Off
  pinMode(relay_rateSelector2, OUTPUT);
  digitalWrite(relay_rateSelector2, LOW);
  pinMode(relay_rateSelector3, OUTPUT);
  digitalWrite(relay_rateSelector3, LOW);
  pinMode(alarm1_input_SrlMd, INPUT_PULLUP);
  pinMode(pumpState, INPUT_PULLUP);
  pinMode(overload_input, INPUT);;
  pinMode(alarm2_input, INPUT_PULLUP);

  // EEPROM initialize
  setup_EEPROM();

    //  0 = No effect, taken from switch input
    //  1 = Serial Mode
    //  2 = Operational Mode
  // int SerialModeValue = EEPROM.readInt(EEPROM_SERIAL_OPERATIONAL_MODE);
  // switch (SerialModeValue)
  // {
  // case 1:
  //   serialMode = true;
  //   break;
  // case 2:
  //   serialMode = false;
  //   break;  
  // default:
  //   serialMode = !digitalRead(alarm1_input_SrlMd);
  //   break;
  // }

  Serial.begin(115200);
  // if (serialMode) {//activates the serial communication
  //   Serial.begin(115200);
  //   Serial.print("\n Serial Mode ----> ");
  //   Serial.println(SerialModeValue);
  // } else {
  //   pinMode(relay_rateSelector3, OUTPUT);
  //   digitalWrite(relay_rateSelector3, LOW);
  // }

  pinMode(relay_start, OUTPUT);
  digitalWrite(relay_start, LOW);
  pinMode(relay_stop, OUTPUT);
  digitalWrite(relay_stop, LOW);
  // RTC initialize
  if (rtc.begin()) {
    rtcEnabled = true;
  } else {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    abort();
  }
  // if (rtc.lostPower()) {
  //   Serial.println("RTC is NOT running, let's set the time!");
  //   rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  // }

  // variables from EEPROM
  deviceRegistered = EEPROM.readChar(EEPROM_DEVICE_REGISTERED_ADDRESS);
  // force unregistered for DEV
  // deviceRegistered = 'n';
  deviceID = EEPROM.readInt(EEPROM_DEVICE_ID_ADDRESS);
  ClientName = EEPROM.readString(EEPROM_CLIENT_NAME_ADDRESS);
  FirebaseNotyURL = EEPROM.readString(EEPROM_FIREBASE_URL_ADDRESS);
  FireBaseAuthKey = EEPROM.readString(EEPROM_FIREBASE_KEY_ADDRESS);
  String rate2Info = EEPROM.readString(EEPROM_TIME_ACTIVATE_RATE2_ADDRESS);
  loadRate2Time(rate2Info);
  String rate3Info = EEPROM.readString(EEPROM_TIME_ACTIVATE_RATE3_ADDRESS);
  loadRate3Time(rate3Info);
  String resetBillRatesInfo = EEPROM.readString(EEPROM_RESET_BILL_RATES_INFO_ADDRESS);
  loadResetBillRatesTime(resetBillRatesInfo);
  ModbusIPaddress = EEPROM.readString(EEPROM_MODBUS_IP_ADDRESS);
  Serial.print("MBIP ------> ");
  Serial.println(ModbusIPaddress);
  MqttBrokerURL = EEPROM.readString(EEPROM_MQTT_BROKER_URL);
  Serial.print("Broker URL ------> ");
  Serial.println(MqttBrokerURL);
  MqttBrokerPort = EEPROM.readInt(EEPROM_MQTT_BROKER_PORT);
  Serial.print("Broker Port ------> ");
  Serial.println(MqttBrokerPort);
  MqttUser = EEPROM.readString(EEPROM_MQTT_USER);
  Serial.print("MQTT User ------> ");
  Serial.println(MqttUser);
  MqttPassword = EEPROM.readString(EEPROM_MQTT_PASSWORD);
  Serial.print("MQTT Password ------> ");
  Serial.println(MqttPassword);

  int MBIPqty = EEPROM.readInt(EEPROM_MODBUS_IP_VAR_QUANTITY);
  if (MBIPqty > 0) {
    activeVarModIP = MBIPqty;
  }
  Serial.print("MBIP QTY------> ");
  Serial.println(activeVarModIP);
  
  int MBIPAlarmQty = EEPROM.readInt(EEPROM_MODBUS_IP_ALARM_QUANTITY);
  if (MBIPAlarmQty > 0) {
    activeAlarmModIP = MBIPAlarmQty;
  }
  Serial.print("MBIP Alarm QTY------> ");
  Serial.println(activeAlarmModIP);

  setup_API();
  sdEnabled = initializeSD();
  WiFi.mode(WIFI_STA);
  WiFi.onEvent(WiFiEvent);
  ETH.begin();

 //solo para DEMOS
//setup_WiFiConnection();

  server.begin();
  // Configurar cliente SSL/TLS
  // Usar setInsecure() para certificados auto-firmados y reducir uso de memoria
  espClient.setInsecure(); 
  // Si quieres validar el certificado (usa más RAM): espClient.setCACert(root_ca);
  
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  // pinMode(MAX485_RE_NEG, OUTPUT);
  // Init in receive mode
  // digitalWrite(MAX485_RE_NEG, LOW);
  // Modbus communication runs at 9600 baud
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  modbus.begin(Slave_ID, Serial2);
   // Callbacks allow us to configure the RS485 transceiver correctly
  // modbus.preTransmission(preTransmission);
  // modbus.postTransmission(postTransmission);
  rs485Enabled = true;

  setup_Webserver();
  // start task of loop monitor analizer
  if (deviceRegistered == 'y') {
    // Reducido de 110000 a 80000 para liberar memoria para SSL/TLS
    xTaskCreatePinnedToCore(loop_Monitor, "Monitor_Task", 80000, NULL, 1, &Monitor_Task, 0);
    setup_ModbusIP();
  } else {
    Serial.println("DEVICE NOT REGISTERED");
    eventLocalLogger = "DEVICE NOT REGISTERED";
  }
}

//******************************
//***       MAIN LOOP        ***
//******************************

long lastMillis = 0;
unsigned long millisModbusIP = 0;
void loop()
{
  if (eth_connected) {
    // IF SWITCH change state manually
    if (digitalRead(pumpState) != pumpLastState) {
      delay(1000);
      if (digitalRead(pumpState) != pumpLastState) {
        pumpLastState = digitalRead(pumpState);
        if (pumpLastState == 0) {
          sw1 = 1;
          sw2 = 1;
          sw3 = 1;
          if (!isJustRestarted) {
            if (isRemote) {
              eventLogger = remoteON;
              Serial.println("REMOTE: ---- ON ----");
              isRemote = false;
            } else {
              eventLogger = localON;
              Serial.println("LOCAL: ---- ON ----");
            }
          } else {
            isJustRestarted = false;
          }
        } 
        else if (pumpLastState == 1) {
          sw1 = 0;
          sw2 = 0;
          sw3 = 0;
          if (!isJustRestarted) {
            if (isRemote) {
              Serial.println("REMOTE: ---- OFF ----");
              eventLogger = remoteOFF;
              isRemote = false;
            } else {
              Serial.println("LOCAL: ---- OFF ----");
              eventLogger = localOFF;
            }
          } else {
            isJustRestarted = false;
          }
        }
        if (client.connected()) {
          publishSwitchState();
          delay(300);
        } 
      }
    }

    // OVERLOAD LOGIC
    if (digitalRead(overload_input) != overloadLastState) {
      delay(1000);
      if (digitalRead(overload_input) != overloadLastState) {
        overloadLastState = digitalRead(overload_input);
        if (overloadLastState == 0) {
          if (!isJustRestartedOverload) {
            Serial.println("OVERLOAD: ---- ON ----");
            isOverload = true;
            eventLogger = overload;
            // if (sdEnabled) { // comprueba funcionamiento de SD
            //   String fileName = "/sdcard/LocalEvents/overload";
            //   writeHistoryFile(fileName, "prendio", "a");
            // }
          } else {
            isJustRestartedOverload = false;
          }
        } 
        else if (overloadLastState == 1) {
          if (!isJustRestartedOverload) {
            Serial.println("OVERLOAD: ---- OFF ----");
            isOverload = false;
            // if (sdEnabled) { // comprueba funcionamiento de SD
            //   String fileName = "/sdcard/LocalEvents/overload";
            //   writeHistoryFile(fileName, "apago", "a");
            // }
          } else {
            isJustRestartedOverload = false;
          }
        }
        // REVISAR SI ES NECESARIO QUE INFORME EL ESTADO DE LA BOMBA VIA MQTT
        if (client.connected()) {
          publishSwitchState();
          delay(300);
        } 
      }
    }

    // ALARM 1 LOGIC
    delay(1000);
    if (digitalRead(alarm1_input_SrlMd) != alarm1LastState) {
      alarm1LastState = digitalRead(alarm1_input_SrlMd);
      if (alarm1LastState == 0) {
        if (!isJustRestartedAlarm1) {
          isAlarm1 = true;
          eventLogger = alarm1;
          // Serial.println("ALARM 1: ---- ON ----");
          // publishDeviceTime = true; // comprueba funcionamiento en serialMode OFF
          // if (sdEnabled) { // comprueba funcionamiento de SD
          //   String fileName = "/sdcard/LocalEvents/alarm1";
          //   writeHistoryFile(fileName, "prendio", "a");
          // }
        } else {
          isJustRestartedAlarm1 = false;
        }
      } 
      else if (alarm1LastState == 1) {
        if (!isJustRestartedAlarm1) {
          isAlarm1 = false;
          // Serial.println("ALARM 1: ---- OFF ----");
          // publishDeviceTime = true; // comprueba funcionamiento en serialMode OFF
          // if (sdEnabled) { // comprueba funcionamiento de SD
          //   String fileName = "/sdcard/LocalEvents/alarm1";
          //   writeHistoryFile(fileName, "apago", "a");
          // }
        } else {
          isJustRestartedAlarm1 = false;
        }
      }
      // REVISAR SI ES NECESARIO QUE INFORME EL ESTADO DE LA BOMBA VIA MQTT
      if (client.connected()) {
        publishSwitchState();
        delay(300);
      } 
    }

    // ALARM 2 LOGIC
    if (digitalRead(alarm2_input) != alarm2LastState) {
      alarm2LastState = digitalRead(alarm2_input);
      if (alarm2LastState == 0) {
        if (!isJustRestartedAlarm2) {
          isAlarm2 = true;
          eventLogger = alarm2;
          // Serial.println("ALARM 2: ---- ON ----");
          // publishDeviceTime = true; // comprueba funcionamiento en serialMode OFF
          // if (sdEnabled) { // comprueba funcionamiento de SD
          //   String fileName = "/sdcard/LocalEvents/alarm2";
          //   writeHistoryFile(fileName, "prendio", "a");
          // }
        } else {
          isJustRestartedAlarm2 = false;
        }
      } 
      else if (alarm2LastState == 1) {
        if (!isJustRestartedAlarm2) {
          isAlarm2 = false;
          // // Serial.println("ALARM 2: ---- OFF ----");
          // publishDeviceTime = true; // comprueba funcionamiento en serialMode OFF
          // if (sdEnabled) { // comprueba funcionamiento de SD
          //   String fileName = "/sdcard/LocalEvents/alarm2";
          //   writeHistoryFile(fileName, "apago", "a");
          // }
        } else {
          isJustRestartedAlarm2 = false;
        }
      }
      // REVISAR SI ES NECESARIO QUE INFORME EL ESTADO DE LA BOMBA VIA MQTT
      if (client.connected()) {
        publishSwitchState();
        delay(300);
      } 
    }

    if (!client.connected()) {
      Serial.println("conectando...");
      eventLocalLogger = "Connecting MQTT";
      reconnect();
    }

    if (isInstallingFirmware) {
      installFirmware();
      isInstallingFirmware = false;
      firmwareVersionToInstall = "";
    }

    if  (millis() > millisModbusIP + periodModbusIP && activeModbusIP) {
      millisModbusIP = millis();
      read_ModbusIPValues();

      // alarms MBIP Check
      bool alarmChange = false;
      // String stateAlarms = "*************** ";
      for (size_t i = 0; i < activeAlarmModIP; i++) {
        // stateAlarms += RES_AL[i];
        if (RES_AL[i] != RES_AL_PREV[i])
        {
          // Serial.print("hay alarma -------------- > ");
          // Serial.print(RES_AL[i]);
          // Serial.print(" ---- > ");
          // Serial.println(i);
          if (RES_AL[i])
          {
            activedAlarm = i;
            eventLogger = alarmIP;
          }
          alarmChange = true;
          RES_AL_PREV[i] = RES_AL[i];
          }
        }
        if (client.connected() && alarmChange) {
          publishSwitchState();
          delay(300);
        } 
        // Serial.println(stateAlarms);
    }

    client.loop();
    server.handleClient();
  } else {
    delay(1000);
    Serial.print(".");
  }
}


//******************************************
//***       LOOP MONITOR ANALIZER        ***
//******************************************

// period for check every minute states
int periodMinuteCheck = 30000; // 1min
unsigned long millisMinuteCheck = 0;
// period for check every minute states with RS485 activated
int periodMinuteCheckInRS485 = 60000; // 1min
unsigned long millisMinuteCheckInRS485 = 0;
// period for send to save in server REAL TIME values
int periodRT = 4000;
unsigned long millisRT = 0;
// period for handle history instant values
int periodInstant = 300000; // 5mins
unsigned long millisInstant = 0;
// period for handle history Max Min Values
int periodMaxMinRates = 3600000; // 1hr
unsigned long millisMaxMinRates = 0;

void loop_Monitor(void *param) {
  Serial.println("monitor started in core -> " + String(xPortGetCoreID()));
  while (true)
  {
    if (isInstallingFirmware) {;
      Serial.print("*");
      delay(1000);
      continue;
    }
    if (!rtcEnabled) {
      Serial.println("set Device Time");
      delay(2000);
      continue;
    }

    // ******  CHECK EVERY MINUTE STATUS MANY THINGS  *************
    if ((millis() > millisMinuteCheck + periodMinuteCheck) && rtcEnabled ) {
      millisMinuteCheck = millis();
      DateTime now = rtc.now();
      // ******  CHECK TO ACTIVATE RATES  *************
      checkActivateRates(now.hour(), now.minute());
    }

    if (rs485Enabled) {

      // ******  REAL TIME VALUES TO SERVER  *************
      if ((millis() > millisRT + periodRT) && eth_connected && realTimeEnabled) {
        millisRT = millis();
        String values = readAndSave_RealTimeValues();
        // PARA MANEJAR ALARMAS POR LECTURA MODBUS
        // String alarmState = readAlarmValues();
        // if (lastAlarmState != alarmState) {
        //   if(alarmState == "1" && lastAlarmState != "-1") {
        //     eventLogger = alarm1;
        //     alarmInstantRead = values;
        //   } 
        //   else if (alarmState == "2" && lastAlarmState != "-1") {
        //     eventLogger = alarm2;
        //     alarmInstantRead = values;
        //   }
        //   lastAlarmState = alarmState;
        // }

      } else {
        delay(100); // prevent watchdog overflow restart ESP
      }

      // ******  ALL INSTANT VALUES TO SERVER  *************
      if (millis() > millisInstant + periodInstant) {
        millisInstant = millis();
        readAndSave_InstantValues();
      }

      // ******  MAX MIN VALUES TO SERVER  *************
      if (millis() > millisMaxMinRates + periodMaxMinRates) {
        millisMaxMinRates = millis();
        readAndSave_MaxMinRates();
      }

      // // ******  CHECK EVERY MINUTE STATUS MANY THINGS THAT USES RS485  *************
      if ((millis() > millisMinuteCheckInRS485 + periodMinuteCheckInRS485) && rtcEnabled ) {
        millisMinuteCheckInRS485 = millis();
        DateTime now = rtc.now();
        // ******  CHECK TO RESET MAX MIN  *************
        checkResetMaxMin(now.hour(), now.minute());
        // ******  CHECK TO RESET RATES  ***************
        checkResetRates(now.month(), now.day(), now.hour(), now.minute());
      }

      // events to check ***********

      if (eventLogger != zero) {
        logEvent(eventLogger);
        eventLogger = zero;
      }
      if (eventLocalLogger != "") {
        logLocalEvent(eventLocalLogger);
        eventLocalLogger = "";
      }
      if (publishDeviceTime) {
        publishTime();
        publishDeviceTime = false;
      }
      if (isTimeToUpdateHistory) {
        updateLocalHistoryToAPI(UpdateHistoryFileName, "updateHistory");
        //updateLocalHistoryToAPI("/sdcard/History/2042-7-29", "Instant");
        isTimeToUpdateHistory = false;
      }
      // event to reset params by MQTT
      if (eventResetParam != "") {
        if(eventResetParam == "resetMaxMinValues") {
          write_ResetParam(reset_MaxMin, "resetMaxMinValues");
        }
        else if(eventResetParam == "resetBillRates") {
          write_ResetParam(reset_AllEnergyRatesMaxDemandMin, "resetBillRates");
        }
        eventResetParam = "";
      }
      // event to force savings by MQTT
      if (eventForceSaveType != "") {
        if(eventForceSaveType == "RealTimeValues") {
          readAndSave_RealTimeValues();
        }
        else if(eventForceSaveType == "InstantValues") {
          readAndSave_InstantValues();
        }
        else if(eventForceSaveType == "MaxMinRatesValues") {
          readAndSave_MaxMinRates();
        }
        eventForceSaveType = "";
      }
      // event to force readins only show in serial output
      if (forceReadValues) {
        readMinValues();
        readMaxValues();
        readRates(R1);
        readRates(R2);
        readRates(R3);
        readRates(RT);
        forceReadValues = false;
      }

      // event to read modbus by MQTT
      if(readModbusMQTTRequest) {
        readModbus();
        readModbusMQTTRequest = false;
        modbusRequest = "";
        sendModbusResponse();
      }

      // event to read modbus by MQTT
      if(writeModbusMQTTRequest) {
        writeModbus();
        writeModbusMQTTRequest = false;
        modbusRequest = "";
        sendModbusResponse();
      }

      // write alarms configurations by MQTT
      if(writeAlarmConfigsMQTTRequest) {
        writeModbus();
        writeAlarmConfigsMQTTRequest = false;
        saveAlarmConfigsResponse();
        modbusRequest = "";
      }

      // read alarms configurations by MQTT
      if(readAlarmConfigsMQTTRequest) {
        readAlarmsConfig();
        readAlarmConfigsMQTTRequest = false;
        modbusRequest = "";
      }

    } else {
      delay(100); // prevent whatchdog timer reset ESP
    }
  }
  vTaskDelay(100);
}

//**********************************************
//***      MODBUS IP READS AND UPLOADS       ***
//**********************************************

void read_ModbusIPValues() {
  if (mb.isConnected(remoteIP)) {   // Check if connection to Modbus Slave is established
    size_t j = 0;
    for (size_t i = 0; i < activeVarModIP; i++)
    {
      mb.readHreg(remoteIP, REG[j], &RES[i][0]);
      j++;
      mb.readHreg(remoteIP, REG[j], &RES[i][1]);
      j++;
    }

    // ALARMS READINGS
    for (size_t i = 0; i < activeAlarmModIP; i++)
    {
      mb.readCoil(remoteIP, REG_AL[i], &RES_AL[i]);
    }

  } else {
    mb.connect(remoteIP);           // Try to connect if no connection
    if (ModbusIPtryCount == 0) {
      activeModbusIP = false;
      //saveModbusIPAddress("NOMBIP"); //prevent remove when IP server is not available
    } else {
      ModbusIPtryCount--;
      // Serial.print("*************************************** ");
      // Serial.println(ModbusIPtryCount);
    }
  }
  mb.task();                      // Common local Modbus task
  delay(100);                     // Pulling interval
  for (size_t i = 0; i < activeVarModIP; i++)
  {
    memcpy(reinterpret_cast<void*>(&VAL[i]), reinterpret_cast<void const*>(RES[i]), sizeof VAL[i]);
    VALBuff[i] += VAL[i];
    VALBuff[i] += ",";
  }
  // Serial.print("resFlow: ");              // Display Slave register value one time per second (with default settings)
  // Serial.print(VAL[0]);
  // Serial.print(" | ");
  // Serial.print("resLevel: ");              // Display Slave register value one time per second (with default settings)
  // Serial.print(VAL[1]);
  // Serial.print(" | ");
  // Serial.print("resPressure: ");              // Display Slave register value one time per second (with default settings)
  // Serial.println(VAL[2]);
}

String build_ModbusIPPayload() {
  String payload = "";
  for (size_t i = 0; i < activeVarModIP; i++)
  {
    payload += VALBuff[i];
    payload += "|";
    VALBuff[i] = "";
  }
  return payload;
}

String build_ModbusIPInstantPayload() {
  String payload = "";
  for (size_t i = 0; i < activeVarModIP; i++)
  {
    payload += VALBuff[i];
    payload += "|";
  }
  return payload;
}

//**********************************************
//***      REAL TIME READS AND UPLOADS       ***
//**********************************************

String readAndSave_RealTimeValues() {
  DateTime now = rtc.now();
  String values = "";
  values += now.year();
  values += "/";
  values += now.month();
  values += "/";
  values += now.day();
  values += " ";
  values += now.hour();
  values += ":";
  values += now.minute();
  values += ":";
  values += now.second();
  values += " @";
  values += deviceID;
  values += " @";
  values += !digitalRead(pumpState); // 0 = On, 1 = Off
  values += " @";
  values += readRealTimetValues();
  if (activeModbusIP) {
    values += " @";
    values += build_ModbusIPPayload();
  }
  values += "\n";
  Serial.println(values);
  // send to save in server
  serverPost(ApiRTURL, values, "RealTime");
  return values;
}

//**********************************************
//***   INSTANT VALUES READS AND UPLOADS     ***
//**********************************************

void readAndSave_InstantValues() { 
  // save in SD;
  DateTime now = rtc.now();

  String fileName = "/sdcard/History/";
  fileName += now.year();
  fileName += "-";
  fileName += now.month();
  fileName += "-";
  fileName += now.day();

  String values = "";
  values += now.year();
  values += "/";
  values += now.month();
  values += "/";
  values += now.day();
  values += " ";
  values += now.hour();
  values += ":";
  values += now.minute();
  values += ":";
  values += now.second();
  values += " @";
  values += deviceID;
  values += " @";
  values += !digitalRead(pumpState);  // 0 = On, 1 = Off
  values += " @";
  values += readInstantValues();
  if (activeModbusIP) {
    values += " @";
    values += build_ModbusIPInstantPayload();
  }
  values += "\n";
  if (eth_connected) {
    serverPost(ApiHistoryInstantURL, values, "Instant Values");
    //Serial.println(values);
  }
  if (sdEnabled) {
    writeHistoryFile(fileName, values, "a");
  }
}
//**********************************************
//***  MAX MIN AND RATES READS AND UPLOADS   ***
//**********************************************

void readAndSave_MaxMinRates() {
    // guardar en SD
    DateTime now = rtc.now();

    String fileName = "/sdcard/MaxMin/";
    fileName += now.year();
    fileName += "-";
    fileName += now.month();
    fileName += "-";
    fileName += now.day();

    String values = "";
    values += now.year();
    values += "/";
    values += now.month();
    values += "/";
    values += now.day();
    values += " ";
    values += now.hour();
    values += ":";
    values += now.minute();
    values += ":";
    values += now.second();
    values += " @";
    values += deviceID;
    values += "\n";
    values += "Max@";
    values += readMaxValues();
    values += "\n";
    values += "Min@";
    values += readMinValues();
    values += "\n";
    values += "R1@";
    values += readRates(R1);
    values += "\n";
    values += "R2@";
    values += readRates(R2);
    values += "\n";
    values += "R3@";
    values += readRates(R3);
    values += "\n";
    values += "RT@";
    values += readRates(RT);
    if (eth_connected) {
      serverPost(ApiHistoryMaxMinRatesURL, values, "Max Min Values");
      Serial.println(values);
    }
    if (sdEnabled) {
      writeHistoryFile(fileName, values, "w");
    }
}

//***********************************
//***       POST TO SERVER        ***
//***********************************

String serverPost(String url, String values, String callName) {
  if (eth_connected) {
    String responseBody; 
    //send to save in server
    Serial.print("sending to... ");
    Serial.println(url);
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "text/json"); //Preparamos el header text/plain si solo vamos a enviar texto plano sin un paradigma llave:valor.
    String postValues = "\"";
    postValues += values;
    postValues += "\"";
    int responseCode = http.POST(postValues);   //Enviamos el post pasándole, los datos que queremos enviar. (esta función nos devuelve un código que guardamos en un int)
    if (responseCode > 0){
      Serial.println("Código HTTP ► " + String(responseCode));   //Print return code
      if (responseCode == 200 || responseCode == 201) {
        responseBody = http.getString();
        Serial.println(responseCode);
        Serial.println("El servidor respondió ▼ ");
        Serial.println(responseBody);
      }
    } else {
      Serial.print("Error enviando POST, código: ");
      Serial.println(responseCode);
      eventLocalLogger = "Error Post - " + callName + " - " + responseCode;
    }
    http.end();
    return responseBody;
  }
}

//************************************************
//***       INSTALL FIRMWARE FROM SERVER        ***
//************************************************

void installFirmware() {
  if (eth_connected) {
    execOTA();
  }
}


//*************************************
//***      FIREBASE FUNCTIONS       ***
//*************************************

String fireBasePostNotification(eventTypes eventType, String msg) {
  if (eth_connected) { 
    //send to save in server
    Serial.println("mobile firebase noty... ");
    String values = String(deviceID);
    values += "|";
    if (eventType == alarmIP) {
      values +=  eventType + activedAlarm;
    } else {
      values +=  eventType;
    }
    values += "|";
    DateTime now = rtc.now();
    values += now.year();
    values += "/";
    values += now.month();
    values += "/";
    values += now.day();
    values += " ";
    values += now.hour();
    values += ":";
    values += now.minute();
    values += ":";
    values += now.second();
    values += "|";
    values += msg;
    Serial.println(values);
    String response = serverPost(ApiOwnerPushNotificationURL, values, "pushNotification");
    Serial.println(response);
    return response;
  }
}

//************************************
//*** MODBUS RESPONSES & FUNCTIONS ***
//************************************

bool getResultMsg(ModbusMaster *node, uint8_t result) 
{
  String tmpstr2 = "\r\n";

  switch (result) 
  {
  case node->ku8MBSuccess:
    return true;
    break;
  case node->ku8MBIllegalFunction:
    tmpstr2 += "Illegal Function";
    break;
  case node->ku8MBIllegalDataAddress:
    tmpstr2 += "Illegal Data Address";
    break;
  case node->ku8MBIllegalDataValue:
    tmpstr2 += "Illegal Data Value";
    break;
  case node->ku8MBSlaveDeviceFailure:
    tmpstr2 += "Slave Device Failure";
    break;
  case node->ku8MBInvalidSlaveID:
    tmpstr2 += "Invalid Slave ID";
    break;
  case node->ku8MBInvalidFunction:
    tmpstr2 += "Invalid Function";
    break;
  case node->ku8MBResponseTimedOut:
    tmpstr2 += "Response Timed Out";
    break;
  case node->ku8MBInvalidCRC:
    tmpstr2 += "Invalid CRC";
    break;
  default:
    tmpstr2 += "Unknown error: " + String(result);
    break;
  }
  Serial.println(tmpstr2);
  return false;
}

// void preTransmission()
// {
//   digitalWrite(MAX485_RE_NEG, HIGH); //Switch to transmit data
// }

// void postTransmission()
// {
//   digitalWrite(MAX485_RE_NEG, LOW); //Switch to receive data
// }

//*****************************
//***    CONEXION MQTT      ***
//*****************************

void reconnect() {

	while (!client.connected() && eth_connected) {
		// Liberar memoria antes de conectar SSL
		Serial.print("Free heap: ");
		Serial.println(ESP.getFreeHeap());
		Serial.print("Trying to connect Mqtt...");
		// Creamos un cliente ID
		String clientId = deviceName;
		clientId += String(random(0xffff), HEX);
		// Intentamos conectar
		//if (client.connect(clientId.c_str())) {  // for mqttdashboard.commonPrint
    if (MqttUser != "") {
      MqttUser.toCharArray(mqtt_user, 30);
    }
    if (MqttPassword != "") {
      MqttUser.toCharArray(mqtt_pass, 30);
    }
    if (MqttBrokerURL != "" && MqttBrokerPort > 0 && MqttConnTry > 0) {
      MqttBrokerURL.toCharArray(mqtt_server, 30);
      mqtt_port = MqttBrokerPort;
      client.setServer(mqtt_server, mqtt_port);
    }

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {  //for ioticos.org
			Serial.println("Connected!");
      MqttConnTry = 5;
      // Nos suscribimos
      if (client.subscribe(root_topic_device)){
        Serial.println("Subscribed to device");
      } else {
        Serial.println("Subscription device failed");
      }
      if (deviceRegistered == 'y') {
        if (client.subscribe(root_topic_pumpAction)){
          Serial.println("Subscribed to pumpAction");
          publishSwitchState();
        } else {
          Serial.println("Subscription pumpAction failed");
        }
        if (client.subscribe(root_topic_getState)){
          Serial.println("Subscribed to getState");
        } else {
          Serial.println("Subscription getState failed");
        }
        if (client.subscribe(root_topic_master)){
          Serial.println("Subscribed to master");
        } else {
          Serial.println("Subscription master failed");
        }
        char subscribeTopic [100];
        String deviceConfigTopic = root_topic_config;
        deviceConfigTopic += "/#";
        deviceConfigTopic.toCharArray(subscribeTopic, 100);
        if (client.subscribe(subscribeTopic)){
          Serial.println("Subscribed to config");
        } else {
          Serial.println("Subscription config failed");
        }
      }
      
		} else {
      // try to connect x times or setting default broker
      if (MqttConnTry == 0 ) {
        client.setServer("mqtt.agrotecsa.com.mx", 8883);
      } else 
      {
        MqttConnTry--;
      }
			Serial.print("Connection failed :( error -> ");
      eventLocalLogger = "Connection failed :( error MQTT";
			Serial.print(client.state());
			Serial.println(" trying in 3 sec...");
			delay(3000);
		}
	}
}


//**********************************
//***       CALLBACK MQTT        ***
//**********************************

void callback(char* topic, byte* payload, unsigned int length) {
	String incoming = "";
  String topicS = String(topic);
	Serial.print("Mensaje recibido desde -> ");
	Serial.print(topic);
	Serial.println("");
	for (int i = 0; i < length; i++) {
		incoming += (char)payload[i];
	}
  incoming.trim();
	Serial.println("Mensaje -> " + incoming);


  if (topicS.indexOf("pumpAction") > 0) {
    if (incoming == "on1" && ((!sw1 && !sw2 && !sw3) || (!sw1 && sw2 && sw3))) {
      sw1 = true;
      publishSwitchState();
    }
    else if (incoming == "on2" && ((sw1 && !sw2 && !sw3) || (!sw1 && !sw2 && sw3))) {
      sw2 = true;
      publishSwitchState();
    }
    else if (incoming == "on3" && (sw1 && sw2 && !sw3)) {
      sw3 = true;
    }
    else if (incoming == "off1" && ((sw1 && sw2 && sw3) || (sw1 && !sw2 && !sw3))) {
      sw1 = false;
      publishSwitchState();
    }
    else if (incoming == "off2" && ((!sw1 && sw2 && sw3) || (sw1 && sw2 && !sw3))) {
      sw2 = false;
      publishSwitchState();
    }
    else if (incoming == "off3" && (!sw1 && !sw2 && sw3)) {
      sw3 = false;
    }
    else {
      publishSwitchState();
    }
    if(!sw1 && !sw2 && !sw3 && digitalRead(pumpState) == 0) {
      sw3 = true;
      digitalWrite(relay_stop, HIGH);
      delay(1000);
      digitalWrite(relay_stop, LOW);
      isRemote = true;
    }
    else if(sw1 && sw2 && sw3 && digitalRead(pumpState) == 1) {
      sw3 = false;
      digitalWrite(relay_start, HIGH);
      delay(1000);
      digitalWrite(relay_start, LOW);
      isRemote = true;
    }
  }

  if (topicS.indexOf("getState") > 0) {
    publishSwitchState();
  }

  // device/[MAC]/[function]------TOPICS

  // update main topics from master
  if (topicS.indexOf("updateTopics") > 0) {
    if (incoming == "update") {
      eventLocalLogger = "deviceInfo by MQTT " + incoming;
      updateTopics();
    }
  }

  // set device time
  if (topicS.indexOf("adjustDeviceTime") > 0) {
    eventLocalLogger = "adjustDeviceTime by MQTT " + incoming;
    adjustDeviceTime(incoming);
  }
  // get device time
  if (topicS.indexOf("getDeviceTime") > 0) {
    eventLocalLogger = "getDeviceTime by MQTT ";
    publishDeviceTime = true;
  }

  // set device rate2 values
  if (topicS.indexOf("adjustRate2") > 0) {
    eventLocalLogger = "adjustRate2 by MQTT " + incoming;
    adjustRate2(incoming);
  }

  // set device rate2 values
  if (topicS.indexOf("adjustRate3") > 0) {
    eventLocalLogger = "adjustRate3 by MQTT " + incoming;
    adjustRate3(incoming);
  }

  // get drates 2 values
  if (topicS.indexOf("getRate2") > 0) {
    publishRate2Values();
  }

  // get drates 3 values
  if (topicS.indexOf("getRate3") > 0) {
    publishRate3Values();
  }

  // get drates 3 values
  if (topicS.indexOf("getActiveRate") > 0) {
    publishActiveRate();
  }

    // set reset Rates 1,1,0,1 -> [1 = Ene,Mar,etc o 2 = Feb,Abr,etc o 3 = every month], dia, hora, min
  if (topicS.indexOf("adjustResetBillRatesTime") > 0) {
    eventLocalLogger = "adjustResetBillRatesTime by MQTT " + incoming;
    adjustResetBillRatesTime(incoming);
  }
    // get reset Rates time
  if (topicS.indexOf("getResetBillRatesTime") > 0) {
    publishResetBillRatesTime();
  }
   // set reset Max Min time
  if (topicS.indexOf("adjustResetMaxMinTime") > 0) {
    eventLocalLogger = "adjustResetMaxMinTime by MQTT " + incoming;
    adjustResetMaxMinTime(incoming);
  }
    // get reset Max Min time
  if (topicS.indexOf("getResetMaxMinTime") > 0) {
    publishResetMaxMin();
  }
    // directly reset max min values:
    //  resetMaxMinValues
    //  resetBillRates
  if (topicS.indexOf("resetParam") > 0) {
    eventResetParam = incoming;
  }
    // directly Read and Save Real Time Values
    //  RealTimeValues
    //  InstantValues
    //  MaxMinRatesValues
  if (topicS.indexOf("forceSave") > 0) {
    eventForceSaveType = incoming;
  }

    // directly Read MAX MIN Values  ---------- test
  if (topicS.indexOf("readMaxMinRates") > 0) {
    forceReadValues = true;
  }

  // get if device is registered
  if (topicS.indexOf("deviceInfo") > 0) {
    deviceInfo();
  }
  
  // register device
  if (topicS.indexOf("registerDevice") > 0) {
    eventLocalLogger = "registerDevice by MQTT";
    registerDevice();
  }

  // restart device
  if (topicS.indexOf("restart") > 0) {
    eventLocalLogger = "restarted by MQTT";
    ESP.restart();
  }

  // send mobile firebase notification
  if (topicS.indexOf("mobileNoty") > 0) {
    eventLocalLogger = "triggered remote mobile notification - " + incoming ;
    fireBasePostNotification(zero, incoming);
  }

   // test device
  if (topicS.indexOf("file") > 0) {
    // String data = getHistoryFileContent("/sdcard/History/2042-7-29");
    UpdateHistoryFileName = incoming;
    isTimeToUpdateHistory = true;

  }

  // enable real time
  if (topicS.indexOf("RTenable") > 0) {
    if (incoming == "1") {
      realTimeEnabled = true;
    }
    else if (incoming == "0") {
      realTimeEnabled = false;
    }
  }

  // read directly from MODBUS
  if (topicS.indexOf("writeModbus") > 0) {
      //eventLocalLogger = "writeModbus by MQTT " + incoming;
      modbusRequest = incoming;
      writeModbusMQTTRequest = true;
  }

  // write directly to MODBUS
  if (topicS.indexOf("readModbus") > 0) {
      //eventLocalLogger = "readModbus by MQTT " + incoming;
      modbusRequest = incoming;
      readModbusMQTTRequest = true;
  }

  // write and save alarm configs
  if (topicS.indexOf("writeAlarmConfigs") > 0) {
      //eventLocalLogger = "readModbus by MQTT " + incoming;
      modbusRequest = incoming;
      writeAlarmConfigsMQTTRequest = true;
  }

  // read alarm configs
  if (topicS.indexOf("readAlarmConfigs") > 0) {
      //eventLocalLogger = "readModbus by MQTT " + incoming;
      modbusRequest = incoming;
      readAlarmConfigsMQTTRequest = true;
  }

  if (topicS.indexOf("InstallFirmware") > 0) {
      //eventLocalLogger = "readModbus by MQTT " + incoming;
      firmwareVersionToInstall = incoming;
      isInstallingFirmware = true;
  }

  if (topicS.indexOf("getVersion") > 0) {
      //eventLocalLogger = "readModbus by MQTT " + incoming;
      publishFirmwareVersion();
  }

    // Save Mode Serial/Operational
    //  0 = No effect, taken from switch input
    //  1 = Serial Mode
    //  2 = Operational Mode
  // if (topicS.indexOf("serial") > 0) {
  //     saveSerialOperationalMode(incoming.toInt());
  // }

    // assign MODBUS IP address
  if (topicS.indexOf("saveMBIP") > 0) {
    saveModbusIPAddress(incoming);
  }

  // assign MODBUS IP variables quantity
  if (topicS.indexOf("QtyMBIP") > 0) {
    saveModbusIPVarQuantity(incoming.toInt());
    activeVarModIP = incoming.toInt();
  }

    // assign MODBUS IP alarms quantity
  if (topicS.indexOf("AlarmQtyMBIP") > 0) {
    saveModbusIPAlarmQuantity(incoming.toInt());
    activeAlarmModIP = incoming.toInt();
  }

  if (topicS.indexOf("getMBIPaddress") > 0) {
    ModbusIPaddress = EEPROM.readString(EEPROM_MODBUS_IP_ADDRESS);
    publishMBIPaddress();
  }
  
  // assign MQTT Broker URL
  if (topicS.indexOf("saveBrokerURL") > 0) {
    saveBrokerURL(incoming);
  }

  // assign MQTT Broker Port
  if (topicS.indexOf("saveBrokerPort") > 0) {
    saveBrokerPort(incoming.toInt());
  }

  // assign MQTT Broker User
  if (topicS.indexOf("saveBrokerUser") > 0) {
    saveBrokerUser(incoming);
  }

  // assign MQTT Broker Password
  if (topicS.indexOf("saveBrokerPassword") > 0) {
    saveBrokerPassword(incoming);
  }

  // if (topicS.indexOf("getSerialMode") > 0) {
  //   publishSerialOperationalMode();
  // }
}

//*************************************
//***       PUBLISH SW STATE        ***
//*************************************
void publishSwitchState() {
  String switchState;
  char message [30];
  if (sw1) {
    switchState += "1";
  } else {
    switchState += "0";
  }
  if (sw2) {
    switchState += "1";
  } else {
    switchState += "0";
  }
  if (sw3) {
    switchState += "1";
  } else {
    switchState += "0";
  }
  switchState += "-pumpState-";
  if (isAlarm1) switchState += "1";
  if (isAlarm2) switchState += "2";
  if (isOverload) switchState += "O";
  if (activeModbusIP) { 
    switchState += "-";
    for (size_t i = 0; i < activeAlarmModIP; i++) {
      if (RES_AL[i]) {
        switchState += (i + 1);
      }
    }
    Serial.println(switchState);
  }
  switchState.toCharArray(message, 30);
  client.publish(root_topic_pumpState, message);
}

//*****************************************
//***       CHECK FOR RATE 2 or 3       ***
//*****************************************
void checkActivateRates(int hour, int min) {

  int currentMins = (hour * 60) + min;
  // int duringMins = ((hourStopRate2 * 60) + minStopRate2) - ((hourStartRate2 * 60) + minStartRate2);
  int startMinsRate2 = (hourStartRate2 * 60) + minStartRate2;
  int stopMinsRate2 = ((hourStopRate2 * 60) + minStopRate2);
  int startMinsRate3 = (hourStartRate3 * 60) + minStartRate3;
  int stopMinsRate3 = ((hourStopRate3 * 60) + minStopRate3);

  if (hourStartRate2 == 0 &&
      minStartRate2 == 0 &&
      hourStopRate2 == 0 &&
      minStopRate2 == 0) {
        rate2State = false;
        return;
  }
  else if (currentMins >= startMinsRate2 && currentMins < stopMinsRate2) {
    digitalWrite(relay_rateSelector2, HIGH);
    rate2State = true;
    digitalWrite(relay_rateSelector3, LOW);
    rate3State = false;
    Serial.println("Rate 2 Activated");
  }
  else if (currentMins < startMinsRate2 || currentMins >= stopMinsRate2) {
    rate2State = false;
    digitalWrite(relay_rateSelector2, LOW);
    Serial.println("Rate 2 Desactivated");
  } 

  if (hourStartRate3 == 0 &&
      minStartRate3 == 0 &&
      hourStopRate3 == 0 &&
      minStopRate3 == 0) {
        rate3State = false;
        return;
  }
  else if (currentMins >= startMinsRate3 && currentMins < stopMinsRate3) {
    digitalWrite(relay_rateSelector3, HIGH);
    digitalWrite(relay_rateSelector2, LOW);
    rate3State = true;
    rate2State = false;
    Serial.println("Rate 3 Activated");
  }
  else if (currentMins < startMinsRate3 || currentMins >= stopMinsRate3) {
    digitalWrite(relay_rateSelector3, LOW);
    rate3State = false;
    Serial.println("Rate 3 Desactivated");
  } 
}

//********************************************
//***       CHECK FOR RESET MAX MIN        ***
//********************************************
void checkResetMaxMin(int hour, int min) {
  if (hour == hourResetMaxMin && min == minResetMaxMin) {
    readAndSave_MaxMinRates();
    write_ResetParam(reset_MaxMin, "resetMaxMinValues");
    Serial.println("MAX MIN values Reseted");
  }
}

//********************************************
//***        CHECK FOR RESET RATES         ***
//********************************************
void checkResetRates(int month, int day, int hour, int min) {
  bool isMonth = false;
  if (!isMonthlyResetRates) 
  {
    for (int i = 0; (i < 6 && !isMonth); i++) {
      isMonth = monthResetRates[i] == month;
    }
  } else {
    isMonth = true;
  }
  
  if (isMonth && day == dayResetRates && hour == hourResetRates && min == minResetRates) {
    readAndSave_MaxMinRates();
    write_ResetParam(reset_AllEnergyRatesMaxDemandMin, "resetAllRates");
    Serial.println("RATES COUNTERS Reseted");
  }
}

//*************************************
//***             EVENTS            ***
//*************************************

void logEvent(eventTypes type) {
  if (deviceRegistered == 'y' && rtcEnabled) {
    DateTime now = rtc.now();

    String logEv = "";
    logEv += now.year();
    logEv += "/";
    logEv += now.month();
    logEv += "/";
    logEv += now.day();
    logEv += " ";
    logEv += now.hour();
    logEv += ":";
    logEv += now.minute();
    logEv += ":";
    logEv += now.second();
    logEv += "@";
    logEv += deviceID;
    logEv += "@";
    
    if (eth_connected) {
      switch (type)
      {
      case localON:
      case remoteON:
      case localOFF:
      case remoteOFF:
        logEv += type;
        fireBasePostNotification(type, "PumpMonitor State");
        break;
      case alarm1:
      case alarm2:
      case overload:
        logEv += type;
        logEv += "@";
        logEv += readRealTimetValues();
        fireBasePostNotification(type, "PumpMonitor Alarm");
        break;
      case alarmIP:
        logEv += type + activedAlarm;
        logEv += "@";
        logEv += readRealTimetValues();
        fireBasePostNotification(type, "PumpMonitor Process Alarm");
        break;
      }
      logEv += "\n";
      Serial.println(logEv);
      serverPost(ApiHistoryLogEventURL, logEv, "logEvent");
    }
    if (sdEnabled) {
      String fileName = "/sdcard/Events/";
      fileName += now.year();
      fileName += "-";
      fileName += now.month();
      fileName += "-";
      fileName += now.day();
      writeHistoryFile(fileName, logEv, "a");
    }
    // alarmInstantRead = "";
  }
}

//*************************************
//***         LOG LOCAL SD          ***
//*************************************

void logLocalEvent(String errorMessage) {
  if (rtcEnabled) {
    DateTime now = rtc.now();

    String localEv = "";
    localEv += now.year();
    localEv += "/";
    localEv += now.month();
    localEv += "/";
    localEv += now.day();
    localEv += " ";
    localEv += now.hour();
    localEv += ":";
    localEv += now.minute();
    localEv += ":";
    localEv += now.second();
    localEv += "@";
    localEv += deviceID;
    localEv += "@";
    localEv += errorMessage;
    localEv += "\n";
    // Serial.println(logEv);
    // if (eth_connected) {
    //   serverPost(ApiHistoryLogErrorURL, logErr, "logError");
    // }
    if (sdEnabled) {
      String fileName = "/sdcard/LocalEvents/";
      fileName += now.year();
      fileName += "-";
      fileName += now.month();
      fileName += "-";
      fileName += now.day();
      writeHistoryFile(fileName, localEv, "a");
    }
  }
}


//*********************************************
//***  UPDATE LOCAL HISTORY BACKUP TO API   ***
//*********************************************

void updateLocalHistoryToAPI(String fileName, String backupName) {
  if (eth_connected && sdEnabled) {
    String fileInfo = getHistoryFileContent(fileName, backupName);;
    //Serial.println(fileInfo);
    serverPost(ApiOwnerUpdateHistoryURL, fileInfo, ("updateHistory - " + backupName));
  }
}