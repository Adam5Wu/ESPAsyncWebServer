/*
  Asynchronous WebServer library for Espressif MCUs

  Copyright (c) 2016 Hristo Gochkov. All rights reserved.
  Modified by Zhenyu Wu <Adam_5Wu@hotmail.com> for VFATFS, 2017.02

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include "ESPAsyncWebServer.h"
#include "WebHandlerImpl.h"

#if defined(ESP31B)
#include <ESP31BWiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#error Platform not supported
#endif

#if ASYNC_TCP_SSL_ENABLED && USE_VFATFS
#include "vfatfs_api.h"
#endif

String const EMPTY_STRING;
uint8_t const HexLookup[] =
{ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

bool ON_STA_FILTER(AsyncWebRequest const &request) {
  return WiFi.localIP() == request._client.localIP();
}

bool ON_AP_FILTER(AsyncWebRequest const &request) {
  return WiFi.localIP() != request._client.localIP();
}

char const *AsyncWebServer::VERTOKEN = "ESPAsyncHTTPd/0.1";

AsyncWebServer::AsyncWebServer(uint16_t port)
  : _server(port)
  , _rewrites(LinkedList<AsyncWebRewrite*>([](AsyncWebRewrite* r){ delete r; }))
  , _handlers(LinkedList<AsyncWebHandler*>([](AsyncWebHandler* h){ delete h; }))
  //, _catchAllHandler()
{
  _server.onClient([](void* arg, AsyncClient* c){
    ((AsyncWebServer*)arg)->_handleClient(c);
  }, this);
}

void AsyncWebServer::_handleClient(AsyncClient* c) {
  if(c == NULL) return;
  AsyncWebRequest *r = new AsyncWebRequest(*this, *c);
  if(r == NULL) delete c;
}

#if ASYNC_TCP_SSL_ENABLED
int AsyncWebServer::_loadSSLCert(const char *filename, uint8_t **buf) {
#if USE_VFATFS
  File cert = VFATFS.open(filename, "r");
  if (cert) {
    size_t certsize = cert.size();
    uint8_t* certdata = (uint8_t*)malloc(cert.size());
    if (certdata) {
      *buf = certdata;
      while (certsize) {
        size_t readlen = cert.read(certdata, certsize);
        certdata+= readlen;
        certsize-= readlen;
      }
      return 1;
    }
  }
#endif
  return 0;
}

void AsyncWebServer::beginSecure(const char *cert, const char *key, const char *password){
  _server.onSslFileRequest([](void* arg, const char *filename, uint8_t **buf){
    return ((AsyncWebServer*)arg)->_loadSslCert(filename, buf);
  }, this);
  _server.beginSecure(cert, key, password);
}
#endif

AsyncCallbackWebHandler& AsyncWebServer::on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction const& onRequest){
  AsyncCallbackWebHandler* handler = new AsyncCallbackWebHandler(uri, method);
  handler->onRequest = onRequest;
  return addHandler(handler), *handler;
}

#ifdef HANDLE_REQUEST_CONTENT
AsyncCallbackWebHandler& AsyncWebServer::on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction const& onRequest,
                                            ArBodyHandlerFunction const& onBody){
  AsyncCallbackWebHandler& handler = on(uri,method,onRequest);
  handler.onBody = onBody;
  return handler;
}
#endif

AsyncStaticWebHandler& AsyncWebServer::serveStatic(const char* uri, Dir const& dir, const char* indexFile, const char* cache_control){
  AsyncStaticWebHandler* handler = new AsyncStaticWebHandler(uri, dir, cache_control);
  handler->setIndexFile(indexFile);
  return addHandler(handler), *handler;
}

void AsyncWebServer::_rewriteRequest(AsyncWebRequest &request) const {
  for (const auto& r: _rewrites) {
    if (r->_filter(request)) r->_perform(request);
  }
}

void AsyncWebServer::_attachHandler(AsyncWebRequest &request) const {
  AsyncWebHandler** handler = _handlers.get_if([&](AsyncWebHandler *h) {
    return h->_filter(request) && h->_canHandle(request);
  });
  if (handler) request._handler = *handler;
  else request._handler = (AsyncWebHandler*)&_catchAllHandler;
}
