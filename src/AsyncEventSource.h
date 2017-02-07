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
#ifndef ASYNCEVENTSOURCE_H_
#define ASYNCEVENTSOURCE_H_

#include <Arduino.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

class AsyncEventSource;
class AsyncEventSourceResponse;
class AsyncEventSourceClient;
typedef std::function<void(AsyncEventSourceClient *client)> ArEventHandlerFunction;

class AsyncEventSourceClient {
  private:
    uint32_t _lastId;

  public:
    AsyncClient &_client;
    AsyncEventSource &_server;

    AsyncEventSourceClient(AsyncWebRequest *request, AsyncEventSource &server);
    ~AsyncEventSourceClient();

    void close();
    void write(const char * message, size_t len);
    void send(const char *message, const char *event=NULL, uint32_t id=0, uint32_t reconnect=0);
    bool connected() const { return _client.connected(); }
    uint32_t lastId() const { return _lastId; }

    //system callbacks (do not call)
    void _onTimeout(uint32_t time);
    void _onDisconnect();
};

class AsyncEventSource: public AsyncWebHandler {
  private:
    LinkedList<AsyncEventSourceClient *> _clients;
    ArEventHandlerFunction _connectcb;
  public:
    String const _url;
    AsyncEventSource(const String& url);
    ~AsyncEventSource();

    void close();
    void onConnect(ArEventHandlerFunction cb);
    void send(const char *message, const char *event=NULL, uint32_t id=0, uint32_t reconnect=0);
    size_t count() const; //number clinets connected

    //system callbacks (do not call)
    void _addClient(AsyncEventSourceClient * client);
    void _handleDisconnect(AsyncEventSourceClient * client);

    virtual bool _isInterestingHeader(String const& key) override final;
    virtual bool _canHandle(AsyncWebRequest const &request) override final;
    virtual void _handleRequest(AsyncWebRequest &request) override final;
};

class AsyncEventSourceResponse: public AsyncBasicResponse {
  private:
    AsyncEventSource &_server;
  protected:
    virtual void _requestComplete(void) override;
  public:
    AsyncEventSourceResponse(AsyncEventSource &server);
};


#endif /* ASYNCEVENTSOURCE_H_ */