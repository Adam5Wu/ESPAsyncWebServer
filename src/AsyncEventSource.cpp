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
#include "Arduino.h"
#include "AsyncEventSource.h"

static String generateEventMessage(const char *message, const char *event, uint32_t id, uint32_t reconnect){
  String ev;

  if(reconnect){
    ev += "retry: ";
    ev += String(reconnect);
    ev += '\r';
    ev += '\n';
  }

  if(id){
    ev += "id: ";
    ev += String(id);
    ev += '\r';
    ev += '\n';
  }

  if(event != NULL){
    ev += "event: ";
    ev += String(event);
    ev += '\r';
    ev += '\n';
  }

  if(message != NULL){
    size_t messageLen = strlen(message);
    char * lineStart = (char *)message;
    char * lineEnd;
    do {
      char * nextN = strchr(lineStart, '\n');
      char * nextR = strchr(lineStart, '\r');
      if(nextN == NULL && nextR == NULL){
        size_t llen = ((char *)message + messageLen) - lineStart;
        char * ldata = (char *)malloc(llen+1);
        if(ldata != NULL){
          memcpy(ldata, lineStart, llen);
          ldata[llen] = 0;
          ev += "data: ";
          ev += ldata;
          ev += "\r\n\r\n";
          free(ldata);
        }
        lineStart = (char *)message + messageLen;
      } else {
        char * nextLine = NULL;
        if(nextN != NULL && nextR != NULL){
          if(nextR < nextN){
            lineEnd = nextR;
            if(nextN == (nextR + 1))
              nextLine = nextN + 1;
            else
              nextLine = nextR + 1;
          } else {
            lineEnd = nextN;
            if(nextR == (nextN + 1))
              nextLine = nextR + 1;
            else
              nextLine = nextN + 1;
          }
        } else if(nextN != NULL){
          lineEnd = nextN;
          nextLine = nextN + 1;
        } else {
          lineEnd = nextR;
          nextLine = nextR + 1;
        }

        size_t llen = lineEnd - lineStart;
        char * ldata = (char *)malloc(llen+1);
        if(ldata != NULL){
          memcpy(ldata, lineStart, llen);
          ldata[llen] = 0;
          ev += "data: ";
          ev += ldata;
          ev += '\r';
          ev += '\n';
          free(ldata);
        }
        lineStart = nextLine;
        if(lineStart == ((char *)message + messageLen)){
          ev += '\r';
          ev += '\n';
        }
      }
    } while(lineStart < ((char *)message + messageLen));
  }

  return ev;
}

// Client

AsyncEventSourceClient::AsyncEventSourceClient(AsyncWebRequest *request, AsyncEventSource &server)
  : _server(server)
  , _client(request->_client)
{
  _lastId = 0;
  if(request->hasHeader("Last-Event-ID"))
    _lastId = request->getHeader("Last-Event-ID")->values.first()->toInt();

  _client.onError(NULL, NULL);
  _client.onAck(NULL, NULL);
  _client.onPoll(NULL, NULL);
  _client.onData(NULL, NULL);
  _client.onTimeout([](void *r, AsyncClient* c __attribute__((unused)), uint32_t time){ ((AsyncEventSourceClient*)(r))->_onTimeout(time); }, this);
  _client.onDisconnect([](void *r, AsyncClient* c){ ((AsyncEventSourceClient*)(r))->_onDisconnect(); delete c; }, this);
  _server._addClient(this);
  delete request;
}

AsyncEventSourceClient::~AsyncEventSourceClient(){
  close();
}

void AsyncEventSourceClient::_onTimeout(uint32_t time __attribute__((unused))){
  _client.close(true);
}

void AsyncEventSourceClient::_onDisconnect(){
  _server._handleDisconnect(this);
}

void AsyncEventSourceClient::close(){
  _client.close();
}

void AsyncEventSourceClient::write(const char * message, size_t len){
  if(!_client.canSend()){
    return;
  }
  if(_client.space() < len){
    return;
  }
  _client.write(message, len);
}

void AsyncEventSourceClient::send(const char *message, const char *event, uint32_t id, uint32_t reconnect){
  String ev = generateEventMessage(message, event, id, reconnect);
  write(ev.c_str(), ev.length());
}


// Handler

AsyncEventSource::AsyncEventSource(const String& url)
  : _url(url)
  , _clients(LinkedList<AsyncEventSourceClient *>([](AsyncEventSourceClient *c){ delete c; }))
  , _connectcb(NULL)
{}

AsyncEventSource::~AsyncEventSource(){
  close();
}

void AsyncEventSource::onConnect(ArEventHandlerFunction cb){
  _connectcb = cb;
}

void AsyncEventSource::_addClient(AsyncEventSourceClient * client){
  _clients.append(client);
  if(_connectcb)
    _connectcb(client);
}

void AsyncEventSource::_handleDisconnect(AsyncEventSourceClient * client){
  _clients.remove(client);
}

void AsyncEventSource::close(){
  for(const auto &c: _clients){
    if(c->connected()) c->close();
  }
}

void AsyncEventSource::send(const char *message, const char *event, uint32_t id, uint32_t reconnect){
  if(_clients.isEmpty())
    return;

  String ev = generateEventMessage(message, event, id, reconnect);
  for(const auto &c: _clients){
    if(c->connected())
      c->write(ev.c_str(), ev.length());
  }
}

size_t AsyncEventSource::count() const {
  return _clients.count_if([](AsyncEventSourceClient *c){
    return c->connected();
  });
}

bool AsyncEventSource::_canHandle(AsyncWebRequest const &request){
  if (request.method() != HTTP_GET || !request.url().equals(_url))
    return false;
  return true;
}

bool AsyncEventSource::_isInterestingHeader(String const& key) {
  return key.equalsIgnoreCase("Last-Event-ID");
}

void AsyncEventSource::_handleRequest(AsyncWebRequest &request){
  request.send(new AsyncEventSourceResponse(*this));
}

// Response

AsyncEventSourceResponse::AsyncEventSourceResponse(AsyncEventSource &server)
  : AsyncBasicResponse(200, "text/event-stream")
  , _server(server)
{
  addHeader("Cache-Control", "no-cache");
  addHeader("Connection", "keep-alive");
}

void AsyncEventSourceResponse::_requestCleanup(void) {
  if (_state == RESPONSE_END)
    new AsyncEventSourceClient(_request, _server);
  else
    AsyncSimpleResponse::_requestCleanup();
}
