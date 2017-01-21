/*
  Asynchronous WebServer library for Espressif MCUs

  Copyright (c) 2016 Hristo Gochkov. All rights reserved.
  Modified by Zhenyu Wu <Adam_5Wu@hotmail.com> for VFATFS, 2017.01

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

bool ON_STA_FILTER(AsyncWebServerRequest *request) {
  return WiFi.localIP() == request->client()->localIP();
}

bool ON_AP_FILTER(AsyncWebServerRequest *request) {
  return WiFi.localIP() != request->client()->localIP();
}


AsyncWebServer::AsyncWebServer(uint16_t port)
  : _server(port)
  , _rewrites(LinkedList<AsyncWebRewrite*>([](AsyncWebRewrite* r){ delete r; }))
  , _handlers(LinkedList<AsyncWebHandler*>([](AsyncWebHandler* h){ delete h; }))
{
  _catchAllHandler = new AsyncCallbackWebHandler();
  if(_catchAllHandler == NULL)
    return;
  _server.onClient([](void *s, AsyncClient* c){
    if(c == NULL)
      return;
    c->setRxTimeout(3);
    AsyncWebServerRequest *r = new AsyncWebServerRequest((AsyncWebServer*)s, c);
    if(r == NULL){
      c->close(true);
      c->free();
      delete c;
    }
  }, this);
}

AsyncWebServer::~AsyncWebServer(){
  reset();
  delete _catchAllHandler;
}

AsyncWebRewrite& AsyncWebServer::addRewrite(AsyncWebRewrite* rewrite){
  _rewrites.add(rewrite);
  return *rewrite;
}

bool AsyncWebServer::removeRewrite(AsyncWebRewrite *rewrite){
  return _rewrites.remove(rewrite);
}

AsyncWebRewrite& AsyncWebServer::rewrite(const char* from, const char* to){
  return addRewrite(new AsyncWebRewrite(from, to));
}

AsyncWebHandler& AsyncWebServer::addHandler(AsyncWebHandler* handler){
  _handlers.add(handler);
  return *handler;
}

bool AsyncWebServer::removeHandler(AsyncWebHandler *handler){
  return _handlers.remove(handler);
}

void AsyncWebServer::begin(){
  _server.begin();
}

#if ASYNC_TCP_SSL_ENABLED
void AsyncWebServer::onSslFileRequest(AcSSlFileHandler cb, void* arg){
  _server.onSslFileRequest(cb, arg);
}

void AsyncWebServer::beginSecure(const char *cert, const char *key, const char *password){
  _server.beginSecure(cert, key, password);
}
#endif

void AsyncWebServer::_handleDisconnect(AsyncWebServerRequest *request){
  delete request;
}

void AsyncWebServer::_rewriteRequest(AsyncWebServerRequest *request){
  for(const auto& r: _rewrites){
    if (r->from() == request->_url && r->filter(request)){
      request->_url = r->toUrl();
      request->_addGetParams(r->params());
    }
  }
}

void AsyncWebServer::_attachHandler(AsyncWebServerRequest *request){
  for(const auto& h: _handlers){
    if (h->filter(request) && h->canHandle(request)){
      request->setHandler(h);
      return;
    }
  }

  request->addInterestingHeader("ANY");
  request->setHandler(_catchAllHandler);
}

AsyncCallbackWebHandler& AsyncWebServer::on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction const& onRequest,
                                            ArUploadHandlerFunction const& onUpload, ArBodyHandlerFunction const& onBody){
  AsyncCallbackWebHandler& handler = on(uri,method,onRequest,onUpload);
  handler.onBody(onBody);
  return handler;
}

AsyncCallbackWebHandler& AsyncWebServer::on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction const& onRequest,
                                            ArUploadHandlerFunction const& onUpload){
  AsyncCallbackWebHandler& handler = on(uri,method,onRequest);
  handler.onUpload(onUpload);
  return handler;
}

AsyncCallbackWebHandler& AsyncWebServer::on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction const& onRequest){
  AsyncCallbackWebHandler& handler = on(uri,onRequest);
  handler.setMethod(method);
  return handler;
}

AsyncCallbackWebHandler& AsyncWebServer::on(const char* uri, ArRequestHandlerFunction const& onRequest){
  AsyncCallbackWebHandler* handler = new AsyncCallbackWebHandler();
  handler->setUri(uri);
  handler->onRequest(onRequest);
  addHandler(handler);
  return *handler;
}

AsyncStaticWebHandler& AsyncWebServer::serveStatic(const char* uri, Dir const& dir, const char* indexFile, const char* cache_control){
  AsyncStaticWebHandler* handler = new AsyncStaticWebHandler(uri, dir, cache_control);
  handler->setIndexFile(indexFile);
  addHandler(handler);
  return *handler;
}

void AsyncWebServer::catchAll(ArRequestHandlerFunction const& fn){
  ((AsyncCallbackWebHandler*)_catchAllHandler)->onRequest(fn);
}

void AsyncWebServer::catchAll(ArUploadHandlerFunction const& fn){
  ((AsyncCallbackWebHandler*)_catchAllHandler)->onUpload(fn);
}

void AsyncWebServer::catchAll(ArBodyHandlerFunction const& fn){
  ((AsyncCallbackWebHandler*)_catchAllHandler)->onBody(fn);
}

void AsyncWebServer::reset(){
  _rewrites.free();
  _handlers.free();

  ((AsyncCallbackWebHandler*)_catchAllHandler)->onRequest(NULL);
  ((AsyncCallbackWebHandler*)_catchAllHandler)->onUpload(NULL);
  ((AsyncCallbackWebHandler*)_catchAllHandler)->onBody(NULL);
}
