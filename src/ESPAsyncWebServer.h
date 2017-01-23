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
#ifndef _ESPAsyncWebServer_H_
#define _ESPAsyncWebServer_H_

#include "Arduino.h"

#include <functional>
#include <ESPAsyncTCP.h>
#include "FS.h"

#include "StringArray.h"

#if defined(ESP31B)
#include <ESP31BWiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#error Platform not supported
#endif

#define ESPWS_LOG(...) Serial.printf(__VA_ARGS__)
#define ESPWS_DEBUG_LEVEL 0

#if ESPWS_DEBUG_LEVEL < 1
  #define ESPWS_DEBUGDO(...)
  #define ESPWS_DEBUG(...)
#else
  #define ESPWS_DEBUGDO(...) __VA_ARGS__
  #define ESPWS_DEBUG(...) Serial.printf(__VA_ARGS__)
#endif

#if ESPWS_DEBUG_LEVEL < 2
  #define ESPWS_DEBUGVDO(...)
  #define ESPWS_DEBUGV(...)
#else
  #define ESPWS_DEBUGVDO(...) __VA_ARGS__
  #define ESPWS_DEBUGV(...) Serial.printf(__VA_ARGS__)
#endif

class AsyncWebServer;
class AsyncWebServerRequest;
class AsyncWebServerResponse;
class AsyncWebHeader;
class AsyncWebParameter;
class AsyncWebRewrite;
class AsyncWebHandler;
class AsyncStaticWebHandler;
class AsyncCallbackWebHandler;
class AsyncPrintResponse;

typedef enum {
  HTTP_GET     = 0b00000001,
  HTTP_POST    = 0b00000010,
  HTTP_DELETE  = 0b00000100,
  HTTP_PUT     = 0b00001000,
  HTTP_PATCH   = 0b00010000,
  HTTP_HEAD    = 0b00100000,
  HTTP_OPTIONS = 0b01000000,
  HTTP_ANY     = 0b01111111,
} WebRequestMethod;
typedef uint8_t WebRequestMethodComposite;

/*
 * PARAMETER :: Chainable object to hold GET/POST and FILE parameters
 * */

class AsyncWebParameter {
  private:
    String _name;
    String _value;
    size_t _size;
    bool _isForm;
    bool _isFile;

  public:

    AsyncWebParameter(const String& name, const String& value, bool form=false, bool file=false, size_t size=0)
      : _name(name), _value(value), _size(size), _isForm(form), _isFile(file){}
#ifdef __GXX_EXPERIMENTAL_CXX0X__
    AsyncWebParameter(String&& name, String&& value, bool form=false, bool file=false, size_t size=0)
    : _name(std::move(name)), _value(std::move(value)), _size(size), _isForm(form), _isFile(file){}
#endif
    const String& name() const { return _name; }
    const String& value() const { return _value; }
    size_t size() const { return _size; }
    bool isPost() const { return _isForm; }
    bool isFile() const { return _isFile; }
};

/*
 * HEADER :: Chainable object to hold the headers
 * */

class AsyncWebHeader {
  private:
    String _name;
    String _value;

  public:
    AsyncWebHeader(const String& name, const String& value): _name(name), _value(value){}
#ifdef __GXX_EXPERIMENTAL_CXX0X__
    AsyncWebHeader(String&& name, String&& value): _name(std::move(name)), _value(std::move(value)){}
#endif
    AsyncWebHeader(const String& data): _name(), _value(){
      if(!data) return;
      int index = data.indexOf(':');
      if (index < 0) return;
      _name = data.substring(0, index);
      _value = data.substring(index + 2);
    }
    ~AsyncWebHeader(){}
    const String& name() const { return _name; }
    const String& value() const { return _value; }
    String toString() const { return String(_name+": "+_value+"\r\n"); }
};

/*
 * REQUEST :: Each incoming Client is wrapped inside a Request and both live together until disconnect
 * */

typedef std::function<size_t(uint8_t*, size_t, size_t)> AwsResponseFiller;

class AsyncWebServerRequest {
  friend class AsyncWebServer;
  private:
    AsyncClient* _client;
    AsyncWebServer* _server;
    AsyncWebHandler* _handler;
    AsyncWebServerResponse* _response;
    StringArray _interestingHeaders;

    String _temp;
    uint8_t _parseState;

    uint8_t _version;
    WebRequestMethodComposite _method;
    String _url;
    String _host;
    String _contentType;
    String _boundary;
    String _authorization;
    bool _isDigest;
    bool _isMultipart;
    bool _isPlainPost;
    bool _expectingContinue;
    size_t _contentLength;
    size_t _parsedLength;

    LinkedList<AsyncWebHeader *> _headers;
    LinkedList<AsyncWebParameter *> _params;

    uint8_t _multiParseState;
    uint8_t _boundaryPosition;
    size_t _itemStartIndex;
    size_t _itemSize;
    String _itemName;
    String _itemFilename;
    String _itemType;
    String _itemValue;
    uint8_t *_itemBuffer;
    size_t _itemBufferIndex;
    bool _itemIsFile;

    void _onPoll();
    void _onAck(size_t len, uint32_t time);
    void _onError(int8_t error);
    void _onTimeout(uint32_t time);
    void _onDisconnect();
    void _onData(void *buf, size_t len);

    void _addParam(AsyncWebParameter*);

    bool _parseReqHead();
    bool _parseReqHeader();
    void _parseLine();
    void _parsePlainPostChar(uint8_t data);
    void _parseMultipartPostByte(uint8_t data, bool last);
    void _addGetParams(const String& params);

    void _handleUploadStart();
    void _handleUploadByte(uint8_t data, bool last);
    void _handleUploadEnd();

  public:
    ESPWS_DEBUGDO(String const _remoteIdent);

    File _tempFile;
    Dir _tempDir;
    String _tempPath;

    AsyncWebServerRequest(AsyncWebServer*, AsyncClient*);
    ~AsyncWebServerRequest();

    AsyncClient* client(){ return _client; }
    uint8_t version() const { return _version; }
    WebRequestMethodComposite method() const { return _method; }
    const String& url() const { return _url; }
    const String& host() const { return _host; }
    const String& contentType() const { return _contentType; }
    size_t contentLength() const { return _contentLength; }
    bool multipart() const { return _isMultipart; }
    const char * methodToString() const;


    //hash is the string representation of:
    // base64(user:pass) for basic or
    // user:realm:md5(user:realm:pass) for digest
    bool authenticate(const char * hash);
    bool authenticate(const char * username, const char * password, const char * realm = NULL, bool passwordIsHash = false);
    void requestAuthentication(const char * realm = NULL, bool isDigest = true);

    void setHandler(AsyncWebHandler *handler){ _handler = handler; }
    void addInterestingHeader(const String& name);

    void redirect(const String& url);

    AsyncPrintResponse *beginPrintResponse(int code, const String& contentType);

    AsyncWebServerResponse *beginResponse(int code, const String& content=String(), const String& contentType=String());
    AsyncWebServerResponse *beginResponse(FS &fs, const String& path, const String& contentType=String(),
                                          bool download=false);
    AsyncWebServerResponse *beginResponse(File content, const String& path, const String& contentType=String(),
                                          bool download=false);
    AsyncWebServerResponse *beginResponse(int code, Stream &content, const String& contentType, size_t len);
    AsyncWebServerResponse *beginResponse(int code,  AwsResponseFiller callback, const String& contentType, size_t len);
    AsyncWebServerResponse *beginChunkedResponse(int code,  AwsResponseFiller callback, const String& contentType);
    AsyncWebServerResponse *beginResponse_P(int code, const uint8_t * content, const String& contentType, size_t len);
    AsyncWebServerResponse *beginResponse_P(int code, PGM_P content, const String& contentType);

    void send(AsyncWebServerResponse *response);

    inline void send(int code, const String& content=String(), const String& contentType=String())
    { send(beginResponse(code, content, contentType)); }

    inline void send(FS &fs, const String& path, const String& contentType=String(), bool download=false)
    { send(beginResponse(fs, path, contentType, download)); }

    inline void send(File content, const String& path, const String& contentType=String(), bool download=false)
    { send(beginResponse(content, path, contentType, download)); }

    inline void send(int code, Stream &content, const String& contentType, size_t len)
    { send(beginResponse(code, content, contentType, len)); }

    inline void send(int code, AwsResponseFiller callback, const String& contentType, size_t len)
    { send(beginResponse(code, callback, contentType, len)); }

    inline void sendChunked(int code, AwsResponseFiller callback, const String& contentType)
    { send(beginChunkedResponse(code, callback, contentType)); }

    inline void send_P(int code, const uint8_t * content, const String& contentType, size_t len)
    { send(beginResponse_P(code, content, contentType,  len)); }

    inline void send_P(int code, PGM_P content, const String& contentType)
    { send(beginResponse_P(code, content, contentType)); }

    size_t headers() const;                     // get header count
    bool hasHeader(const String& name) const;   // check if header exists
    AsyncWebHeader* getHeader(const String& name) const;
    AsyncWebHeader* getHeader(size_t num) const;

    size_t params() const;                      // get arguments count
    bool hasParam(const String& name, bool post=false, bool file=false) const;
    AsyncWebParameter* getParam(const String& name, bool post=false, bool file=false) const;
    AsyncWebParameter* getParam(size_t num) const;

    size_t args() const { return params(); }     // get arguments count
    const String& arg(const String& name) const; // get request argument value by name
    const String& arg(size_t i) const;           // get request argument value by number
    const String& argName(size_t i) const;       // get request argument name by number
    bool hasArg(const char* name) const;         // check if argument exists

    const String& header(const char* name) const;// get request header value by name
    const String& header(size_t i) const;        // get request header value by number
    const String& headerName(size_t i) const;    // get request header name by number
    String urlDecode(const String& text) const;
};

/*
 * FILTER :: Callback to filter AsyncWebRewrite and AsyncWebHandler (done by the Server)
 * */

typedef std::function<bool(AsyncWebServerRequest *request)> ArRequestFilterFunction;

bool ON_STA_FILTER(AsyncWebServerRequest *request);

bool ON_AP_FILTER(AsyncWebServerRequest *request);

/*
 * REWRITE :: One instance can be handle any Request (done by the Server)
 * */

class AsyncWebRewrite {
  protected:
    String _from;
    String _toUrl;
    String _params;
    ArRequestFilterFunction _filter;
  public:
    AsyncWebRewrite(const char* from, const char* to): _from(from), _toUrl(to), _params(String()), _filter(NULL){
      int index = _toUrl.indexOf('?');
      if (index > 0) {
        _params = _toUrl.substring(index +1);
        //_toUrl = _toUrl.substring(0, index);
        _toUrl.remove(index);
      }
    }
    AsyncWebRewrite& setFilter(ArRequestFilterFunction fn) { _filter = fn; return *this; }
    bool filter(AsyncWebServerRequest *request) const { return _filter == NULL || _filter(request); }
    const String& from(void) const { return _from; }
    const String& toUrl(void) const { return _toUrl; }
    const String& params(void) const { return _params; }
};

/*
 * HANDLER :: One instance can be attached to any Request (done by the Server)
 * */

class AsyncWebHandler {
  protected:
    ArRequestFilterFunction _filter;
  public:
    AsyncWebHandler(){}
    AsyncWebHandler& setFilter(ArRequestFilterFunction fn) { _filter = fn; return *this; }
    bool filter(AsyncWebServerRequest *request){ return _filter == NULL || _filter(request); }
    virtual ~AsyncWebHandler(){}
    virtual bool canHandle(AsyncWebServerRequest *request __attribute__((unused))){
      return false;
    }
    virtual void handleRequest(AsyncWebServerRequest *request __attribute__((unused))){}
    virtual void handleUpload(AsyncWebServerRequest *request  __attribute__((unused)),
                              const String& filename __attribute__((unused)),
                              size_t index __attribute__((unused)),
                              uint8_t *data __attribute__((unused)),
                              size_t len __attribute__((unused)),
                              bool final  __attribute__((unused))){}
    virtual void handleBody(AsyncWebServerRequest *request __attribute__((unused)),
                            uint8_t *data __attribute__((unused)),
                            size_t len __attribute__((unused)),
                            size_t index __attribute__((unused)),
                            size_t total __attribute__((unused))){}
};

/*
 * RESPONSE :: One instance is created for each Request (attached by the Handler)
 * */

typedef enum {
  RESPONSE_SETUP, RESPONSE_HEADERS, RESPONSE_CONTENT, RESPONSE_WAIT_ACK, RESPONSE_END, RESPONSE_FAILED
} WebResponseState;

class AsyncWebServerResponse {
  protected:
    int _code;
    WebResponseState _state;
    ESPWS_DEBUGVDO(uint32_t const _ID);

    static const char* _responseCodeToString(int code);

  public:
    AsyncWebServerResponse(int code);
    virtual ~AsyncWebServerResponse() {}

    virtual void setCode(int code);
    virtual void addHeader(const String& name, const String& value) = 0;

    virtual void _respond(AsyncWebServerRequest *request) = 0;
    virtual size_t _ack(AsyncWebServerRequest *request, size_t len, uint32_t time) = 0;

    inline void MUSTNOTSTART(void) const { while (_started()) panic(); }
    inline bool _started(void) const { return _state > RESPONSE_SETUP; }
    inline bool _sending(void) const { return _started() && _state < RESPONSE_WAIT_ACK; }
    inline bool _waitack(void) const { return _state == RESPONSE_WAIT_ACK; }
    inline bool _finished(void) const { return _state > RESPONSE_WAIT_ACK; }
    inline bool _failed(void) const { return _state == RESPONSE_FAILED; }

    ESPWS_DEBUGDO(const char* stateToString(void) const);
};

/*
 * SERVER :: One instance
 * */

typedef std::function<void(AsyncWebServerRequest *request)> ArRequestHandlerFunction;
typedef std::function<void(AsyncWebServerRequest *request, const String& filename, size_t index,
                           uint8_t *data, size_t len, bool final)> ArUploadHandlerFunction;
typedef std::function<void(AsyncWebServerRequest *request, uint8_t *data, size_t len,
                           size_t index, size_t total)> ArBodyHandlerFunction;

#define DEFAULT_CACHE_CTRL "public, no-cache"
#define DEFAULT_INDEX_FILE "index.htm"

class AsyncWebServer {
  protected:
    AsyncServer _server;
    LinkedList<AsyncWebRewrite*> _rewrites;
    LinkedList<AsyncWebHandler*> _handlers;
    AsyncWebHandler* _catchAllHandler;

  public:
    AsyncWebServer(uint16_t port);
    ~AsyncWebServer();

    void begin();

#if ASYNC_TCP_SSL_ENABLED
    void onSslFileRequest(AcSSlFileHandler cb, void* arg);
    void beginSecure(const char *cert, const char *private_key_file, const char *password);
#endif

    AsyncWebRewrite& addRewrite(AsyncWebRewrite* rewrite);
    bool removeRewrite(AsyncWebRewrite* rewrite);
    AsyncWebRewrite& rewrite(const char* from, const char* to);

    AsyncWebHandler& addHandler(AsyncWebHandler* handler);
    bool removeHandler(AsyncWebHandler* handler);

    AsyncCallbackWebHandler& on(const char* uri, ArRequestHandlerFunction const& onRequest);
    AsyncCallbackWebHandler& on(const char* uri, WebRequestMethodComposite method,
                                ArRequestHandlerFunction const& onRequest);
    AsyncCallbackWebHandler& on(const char* uri, WebRequestMethodComposite method,
                                ArRequestHandlerFunction const& onRequest,
                                ArUploadHandlerFunction const& onUpload);
    AsyncCallbackWebHandler& on(const char* uri, WebRequestMethodComposite method,
                                ArRequestHandlerFunction const& onRequest,
                                ArUploadHandlerFunction const& onUpload, ArBodyHandlerFunction const& onBody);

    AsyncStaticWebHandler& serveStatic(const char* uri, Dir const& dir,
                                       const char* indexFile = DEFAULT_INDEX_FILE,
                                       const char* cache_control = DEFAULT_CACHE_CTRL);

    // Called when handler is not assigned
    void catchAll(ArRequestHandlerFunction const& onRequest);
    void catchAll(ArUploadHandlerFunction const& onUpload);
    void catchAll(ArBodyHandlerFunction const& onBody);

    void reset(); //remove all writers and handlers, including catch-all handlers

    void _handleDisconnect(AsyncWebServerRequest *request);
    void _attachHandler(AsyncWebServerRequest *request);
    void _rewriteRequest(AsyncWebServerRequest *request);
};

#include "WebResponseImpl.h"
#include "WebHandlerImpl.h"

#include "AsyncWebSocket.h"
#include "AsyncEventSource.h"

#endif /* _AsyncWebServer_H_ */
