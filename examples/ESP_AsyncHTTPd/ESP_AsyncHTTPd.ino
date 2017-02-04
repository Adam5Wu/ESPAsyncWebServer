// This demo requires a modified ESP8266 Arduino, found here:
// https://github.com/Adam5Wu/Arduino

// This demo requires ESPVFATFS library, found here:
// https://github.com/Adam5Wu/ESPVFATFS

#define NO_GLOBAL_SPIFFS

#include <vfatfs_api.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>

#include <sys/time.h>

extern "C" {
  #include "lwip/inet.h"
  #include "lwip/err.h"
  #include "lwip/sntp.h"
  #include "lwip/app/espconn.h"
}

#define TIMEZONE    -(5*3600)
#define DSTOFFSET   0 // 3600
#define NTPSERVER   "pool.ntp.org"

AsyncWebServer wwwSrv(80);

const char* ssid = "Guest";
const char* password = "Welcome";

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    WiFi.disconnect(false);
    Serial.println("STA: Failed to connect, retrying...");
    delay(5000);
    WiFi.begin(ssid, password);
  }
  Serial.printf("Connected to %s\n", ssid);
  Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
  Serial.printf("Name Server: %s\n", WiFi.dnsIP().toString().c_str());

  espconn_tcp_set_max_con(10);

  delay(500);
  configTime(TIMEZONE,DSTOFFSET,NTPSERVER);
  delay(1000);
  while (!sntp_get_current_timestamp()) {
    Serial.println("Waiting for NTP time...");
    delay(2000);
  }

  {
    timeval curtime;
    gettimeofday(&curtime,NULL);
    Serial.printf("Current Time: %s", sntp_get_real_time(curtime.tv_sec));
  }

  while (!VFATFS.begin()) panic();

  FSInfo info;
  VFATFS.info(info);
  Serial.printf("FATFS: %d total, %d used (%.1f%%), block size %d\n",
                info.totalBytes, info.usedBytes, info.usedBytes*100.0/info.totalBytes, info.blockSize);

  wwwSrv.on("/all", HTTP_GET, [](AsyncWebRequest &request){
    String json = "{";
    json += "\"heap\":"+String(ESP.getFreeHeap());
    json += ", \"analog\":"+String(analogRead(A0));
    json += ", \"gpio\":"+String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
    json += "}";
    request.send(200, json, "text/json");
    json = String();
  });

  wwwSrv.serveStatic("/test", VFATFS.openDir("/"));

  wwwSrv.catchAll([](AsyncWebRequest &request){
    Serial.print("NO_HANDLER: ");
    Serial.print(request.methodToString());
    Serial.printf(" %s %s\n", request.host().c_str(), request.url().c_str());

    if(request.contentLength()){
      Serial.printf("_CONTENT_TYPE: %s\n", request.contentType().c_str());
      Serial.printf("_CONTENT_LENGTH: %u\n", request.contentLength());
    }

    request.enumQueries([&](AsyncWebQuery const& q){
      Serial.printf("_QUERY[%s]: %s\n", q.name.c_str(), q.value.c_str());
      return false;
    });

    request.enumHeaders([&](AsyncWebHeader const& h){
      Serial.printf("_HEADER[%s]:",h.name.c_str());
      String values;
      for (auto v : h.values) {
        values.concat(' ');
        values.concat(v);
        values.concat(',');
      }
      values.remove(values.length()-1);
      Serial.println(values);
      return false;
    });

    request.send(404);
  });

  wwwSrv.begin();
}

void loop() {
  delay(10);
}
