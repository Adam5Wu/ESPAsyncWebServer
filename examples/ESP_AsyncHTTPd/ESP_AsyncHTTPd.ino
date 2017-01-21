// This demo requires a modified ESP8266 Arduino, found here:
// https://github.com/Adam5Wu/Arduino

// This demo requires ESPVFATFS library, found here:
// https://github.com/Adam5Wu/ESPVFATFS

#define NO_GLOBAL_SPIFFS

#include <sys/time.h>
#include <ESP8266WiFi.h>

#include <FS.h>
#include <vfatfs_api.h>

#include <ESPAsyncWebServer.h>

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

  wwwSrv.on("/all", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"heap\":"+String(ESP.getFreeHeap());
    json += ", \"analog\":"+String(analogRead(A0));
    json += ", \"gpio\":"+String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
    json += "}";
    request->send(200, json, "text/json");
    json = String();
  });

  wwwSrv.serveStatic("/", VFATFS.openDir("/"));

  wwwSrv.catchAll([](AsyncWebServerRequest *request){
    Serial.print("NO_HANDLER: ");
    if(request->method() == HTTP_GET)
      Serial.print("GET");
    else if(request->method() == HTTP_POST)
      Serial.print("POST");
    else if(request->method() == HTTP_DELETE)
      Serial.print("DELETE");
    else if(request->method() == HTTP_PUT)
      Serial.print("PUT");
    else if(request->method() == HTTP_PATCH)
      Serial.print("PATCH");
    else if(request->method() == HTTP_HEAD)
      Serial.print("HEAD");
    else if(request->method() == HTTP_OPTIONS)
      Serial.print("OPTIONS");
    else
      Serial.print("UNKNOWN");
    Serial.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if(request->contentLength()){
      Serial.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      Serial.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for(i=0;i<headers;i++){
      AsyncWebHeader* h = request->getHeader(i);
      Serial.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for(i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isFile()){
        Serial.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      } else if(p->isPost()){
        Serial.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      } else {
        Serial.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }
    request->send(404);
  });

  wwwSrv.begin();
}

void loop() {
  delay(10);
}
