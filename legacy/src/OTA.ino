

#include <WiFi.h>
#include <Update.h>

WiFiClient clientUpdate;

// Variables to validate
// response from S3
int contentLength = 0;
bool isValidContentType = false;


// S3 Bucket Config
// String hostMaster = hostMaster; 
int port = 80;

//String bin = "/api/Device/InstallFirmware/1.1"; // bin file name with a slash in front.
// String bin = InstallFirmwareURL;
String contentType = "";
// Utility to extract header value from headers
String getHeaderValue(String header, String headerName) {
  return header.substring(strlen(headerName.c_str()));
}

// OTA Logic 
void execOTA() {
  Serial.println("Connecting to: " + String(hostMaster));
  // Connect to S3
  if (clientUpdate.connect(hostMaster.c_str(), port)) {
    // Connection Succeed.
    // Fecthing the bin
    Serial.println("Fetching Bin: " + String(InstallFirmwareURL + firmwareVersionToInstall));
    Serial.println("");

    // Get the contents of the bin file
    clientUpdate.print(String("GET ") + InstallFirmwareURL + firmwareVersionToInstall + " HTTP/1.1\r\n" +
                "Host: " + hostMaster + "\r\n" +
                "Cache-Control: no-cache\r\n" +
                "Connection: close\r\n\r\n");

    // Check what is being sent
    //    Serial.print(String("GET ") + bin + " HTTP/1.1\r\n" +
    //                 "Host: " + hostMaster + "\r\n" +
    //                 "Cache-Control: no-cache\r\n" +
    //                 "Connection: close\r\n\r\n");

    unsigned long timeout = millis();
    while (clientUpdate.available() == 0) {
        if (millis() - timeout > 5000) {
            Serial.println(">>> Client Timeout !");
            clientUpdate.stop();
            return;
        }
    }
    
    // Once the response is available,
    // check stuff

    /*
        Response Structure
        HTTP/1.1 200 OK
        x-amz-id-2: NVKxnU1aIQMmpGKhSwpCBh8y2JPbak18QLIfE+OiUDOos+7UftZKjtCFqrwsGOZRN5Zee0jpTd0=
        x-amz-request-id: 2D56B47560B764EC
        Date: Wed, 14 Jun 2017 03:33:59 GMT
        Last-Modified: Fri, 02 Jun 2017 14:50:11 GMT
        ETag: "d2afebbaaebc38cd669ce36727152af9"
        Accept-Ranges: bytes
        Content-Type: application/octet-stream
        Content-Length: 357280
        Server: AmazonS3
        {{BIN FILE CONTENTS}}
    */
    while (clientUpdate.available()) {
      // read line till /n
      String line = clientUpdate.readStringUntil('\n');
      // remove space, to check if the line is end of headers
      line.trim();
      Serial.println(line);

      // if the the line is empty,
      // this is end of headers
      // break the while and feed the
      // remaining `client` to the
      // Update.writeStream();
      if (!line.length()) {
        //headers ended
        break; // and get the OTA started
      }

      // Check if the HTTP Response is 200
      // else break and Exit Update
      if (line.startsWith("HTTP/1.1")) {
        if (line.indexOf("200") < 0) {
          Serial.println("Got a non 200 status code from server. Exiting OTA Update.");
          break;
        }
      }

      // extract headers here
      // Start with content length
      if (line.startsWith("Content-Length: ")) {
        contentLength = atoi((getHeaderValue(line, "Content-Length: ")).c_str());
        //Serial.println("Got " + String(contentLength) + " bytes from server");
      }

      // Next, the content type
      if (line.startsWith("Content-Type: ")) {
        contentType = getHeaderValue(line, "Content-Type: ");
        //Serial.println("Got " + contentType + " payload.");
        if (contentType == "application/octet-stream" || contentType == "application/macbinary") {
          isValidContentType = true;
        }
      }
    }
  } else {
    // Connect to S3 failed
    // May be try?
    // Probably a choppy network?
    Serial.println("Connection to " + String(hostMaster) + " failed. Please check your setup");
    // retry??
    // execOTA();
  }

  Serial.println("Got " + contentType + " payload.");
  Serial.println("Got " + String(contentLength) + " bytes from server");
  // Check what is the contentLength and if content type is `application/octet-stream`
  Serial.println("contentLength : " + String(contentLength) + ", isValidContentType : " + String(isValidContentType));
  Serial.println("");

  // check contentLength and content type
  if (contentLength && isValidContentType) {
    // Check if there is enough to OTA Update
    bool canBegin = Update.begin(contentLength);

    // If yes, begin
    if (canBegin) {
      Serial.println("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");
      // No activity would appear on the Serial monitor
      // So be patient. This may take 2 - 5mins to complete
      size_t written = Update.writeStream(clientUpdate);

      if (written == contentLength) {
        Serial.println("Written : " + String(written) + " successfully");
      } else {
        Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?" );
        // retry??
        // execOTA();
      }

      if (Update.end()) {
        Serial.println("OTA done!");
        if (Update.isFinished()) {
          Serial.println("Update successfully completed. Rebooting.");
          ESP.restart();
        } else {
          Serial.println("Update not finished? Something went wrong!");
        }
      } else {
        Serial.println("Error Occurred. Error #: " + String(Update.getError()));
      }
    } else {
      // not enough space to begin OTA
      // Understand the partitions and
      // space availability
      Serial.println("Not enough space to begin OTA");
      clientUpdate.flush();
    }
  } else {
    Serial.println("There was no content in the response");
    clientUpdate.flush();
  }
}

/*
 * Serial Monitor log for this sketch
 * 
 * If the OTA succeeded, it would load the preference sketch, with a small modification. i.e.
 * Print `OTA Update succeeded!! This is an example sketch : Preferences > StartCounter`
 * And then keeps on restarting every 10 seconds, updating the preferences
 * 
 * 
      rst:0x10 (RTCWDT_RTC_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
      configsip: 0, SPIWP:0x00
      clk_drv:0x00,q_drv:0x00,d_drv:0x00,cs0_drv:0x00,hd_drv:0x00,wp_drv:0x00
      mode:DIO, clock div:1
      load:0x3fff0008,len:8
      load:0x3fff0010,len:160
      load:0x40078000,len:10632
      load:0x40080000,len:252
      entry 0x40080034
      Connecting to SSID
      ......
      Connected to SSID
      Connecting to: bucket-name.s3.ap-south-1.amazonaws.com
      Fetching Bin: /StartCounter.ino.bin
      Got application/octet-stream payload.
      Got 357280 bytes from server
      contentLength : 357280, isValidContentType : 1
      Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!
      Written : 357280 successfully
      OTA done!
      Update successfully completed. Rebooting.
      ets Jun  8 2016 00:22:57
      
      rst:0x10 (RTCWDT_RTC_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
      configsip: 0, SPIWP:0x00
      clk_drv:0x00,q_drv:0x00,d_drv:0x00,cs0_drv:0x00,hd_drv:0x00,wp_drv:0x00
      mode:DIO, clock div:1
      load:0x3fff0008,len:8
      load:0x3fff0010,len:160
      load:0x40078000,len:10632
      load:0x40080000,len:252
      entry 0x40080034
      
      OTA Update succeeded!! This is an example sketch : Preferences > StartCounter
      Current counter value: 1
      Restarting in 10 seconds...
      E (102534) wifi: esp_wifi_stop 802 wifi is not init
      ets Jun  8 2016 00:22:57
      
      rst:0x10 (RTCWDT_RTC_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
      configsip: 0, SPIWP:0x00
      clk_drv:0x00,q_drv:0x00,d_drv:0x00,cs0_drv:0x00,hd_drv:0x00,wp_drv:0x00
      mode:DIO, clock div:1
      load:0x3fff0008,len:8
      load:0x3fff0010,len:160
      load:0x40078000,len:10632
      load:0x40080000,len:252
      entry 0x40080034
      
      OTA Update succeeded!! This is an example sketch : Preferences > StartCounter
      Current counter value: 2
      Restarting in 10 seconds...
      ....
 * 
 */