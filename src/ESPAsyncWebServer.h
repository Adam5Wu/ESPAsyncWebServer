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
#define ESPWS_DEBUG_LEVEL 1

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

#if ESPWS_DEBUG_LEVEL < 3
#define ESPWS_DEBUGVVDO(...)
#define ESPWS_DEBUGVV(...)
#else
#define ESPWS_DEBUGVVDO(...) __VA_ARGS__
#define ESPWS_DEBUGVV(...) Serial.printf(__VA_ARGS__)
#endif

#define DEFAULT_REQIDLE_TIMEOUT   5
#define DEFAULT_REALM             "ESP8266"
#define DEFAULT_CACHE_CTRL        "public, no-cache"
#define DEFAULT_INDEX_FILE        "index.htm"

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
 * HEADER :: Hold a header and its values
 * */

class AsyncWebHeader {
  public:
    String name;
    StringArray values;

    AsyncWebHeader(const String& n, const String& v)
    : name(n) { values.append(v); }
#ifdef __GXX_EXPERIMENTAL_CXX0X__
    AsyncWebHeader(String&& n, String&& v)
    : name(std::move(n)) { values.append(std::move(v)); }
#endif
};

/*
 * QUERY :: Hold a query key and value
 * */

class AsyncWebQuery {
  public:
    String name;
    String value;

    AsyncWebQuery(const String& n, const String& v)
    : name(n), value(v) {}
#ifdef __GXX_EXPERIMENTAL_CXX0X__
    AsyncWebQuery(String&& n, String&& v)
    : name(std::move(n)), value(std::move(v)) {}
#endif
};

class AsyncWebServer;
class AsyncWebParser;
class AsyncWebRewrite;
class AsyncWebHandler;
class AsyncWebResponse;
class AsyncPrintResponse;

typedef enum {
  REQUEST_SETUP,
  REQUEST_START,
  REQUEST_HEADERS,
  REQUEST_BODY,
  REQUEST_RECEIVED,
  REQUEST_RESPONSE,
  REQUEST_REPLYING,
  REQUEST_ERROR
} WebServerRequestState;

typedef enum {
  AUTH_NONE, AUTH_BASIC, AUTH_DIGEST, AUTH_OTHER
} WebServerRequestAuth;

extern String const EMPTY_STRING;
String urlDecode(char *buf);

/*
 * REQUEST :: Each incoming Client is wrapped inside a Request and both live together until disconnect
 * */

typedef std::function<size_t(uint8_t*, size_t, size_t)> AwsResponseFiller;

class AsyncWebRequest {
  friend class AsyncWebServer;
  friend class AsyncWebParser;
  friend class AsyncWebRewrite;

  private:
    AsyncWebHandler* _handler;
    AsyncWebResponse* _response;
    AsyncWebParser* _parser;

    WebServerRequestState _state;

    uint8_t _version;
    WebRequestMethodComposite _method;
    String _url;

    String _host;

    String _contentType;
    size_t _contentLength;

    WebServerRequestAuth _authType;
    String _authorization;

    LinkedList<AsyncWebHeader> _headers;
    LinkedList<AsyncWebQuery> _queries;

    void _onAck(size_t len, uint32_t time);
    void _onError(int8_t error);
    void _onTimeout(uint32_t time);
    void _onDisconnect();
    void _onData(void *buf, size_t len);

    void _setUrl(String const& url) { _setUrl(std::move(String(url))); }
    void _setUrl(String && url);
    void _parseQueries(char *buf);

    ESPWS_DEBUGDO(const char* _stateToString(void) const);

  public:
    AsyncClient& _client;
    AsyncWebServer const &_server;
    ESPWS_DEBUGDO(String const _remoteIdent);

    AsyncWebRequest(AsyncWebServer const &server, AsyncClient &client);
    ~AsyncWebRequest();

    bool _onSched(size_t resShare);

    uint8_t version() const { return _version; }
    WebRequestMethodComposite method() const { return _method; }
    const char * methodToString() const;
    const String& url() const { return _url; }

    const String& host() const { return _host; }

    const String& contentType() const { return _contentType; }
    size_t contentLength() const { return _contentLength; }

    WebServerRequestAuth authType(void) const { return _authType; }
    const String& authorization() const { return _authorization; }

    size_t headers() const { return _headers.length(); }
    bool hasHeader(const String& name) const;
    AsyncWebHeader const* getHeader(const String& name) const;

    void enumHeaders(LinkedList<AsyncWebHeader>::Predicate const& Pred)
    { _headers.get_if(Pred); }

    size_t queries() const { return _queries.length(); }
    bool hasQuery(const String& name) const;
    AsyncWebQuery const* getQuery(const String& name) const;

    void enumQueries(LinkedList<AsyncWebQuery>::Predicate const& Pred)
    { _queries.get_if(Pred); }

    void send(AsyncWebResponse *response);

    // Response short-hands
    void redirect(const String& url);

    AsyncPrintResponse *beginPrintResponse(int code, const String& contentType);

    AsyncWebResponse *beginResponse(int code, const String& content=EMPTY_STRING, const String& contentType=EMPTY_STRING);
    AsyncWebResponse *beginResponse(FS &fs, const String& path, const String& contentType=EMPTY_STRING, bool download=false);
    AsyncWebResponse *beginResponse(File content, const String& path, const String& contentType=EMPTY_STRING, bool download=false);
    AsyncWebResponse *beginResponse(int code, Stream &content, const String& contentType, size_t len);
    AsyncWebResponse *beginResponse(int code,  AwsResponseFiller callback, const String& contentType, size_t len);
    AsyncWebResponse *beginChunkedResponse(int code,  AwsResponseFiller callback, const String& contentType);
    AsyncWebResponse *beginResponse_P(int code, const uint8_t * content, const String& contentType, size_t len);
    AsyncWebResponse *beginResponse_P(int code, PGM_P content, const String& contentType);

    inline void send(int code, const String& content=EMPTY_STRING, const String& contentType=EMPTY_STRING)
    { send(beginResponse(code, content, contentType)); }

    inline void send(FS &fs, const String& path, const String& contentType=EMPTY_STRING, bool download=false)
    { send(beginResponse(fs, path, contentType, download)); }

    inline void send(File content, const String& path, const String& contentType=EMPTY_STRING, bool download=false)
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
};

/*
 * RESPONSE :: One instance is created for each Request (attached by the Handler)
 * */

typedef enum {
  RESPONSE_SETUP,
  RESPONSE_STATUS,
  RESPONSE_HEADERS,
  RESPONSE_CONTENT,
  RESPONSE_WAIT_ACK,
  RESPONSE_END,
  RESPONSE_FAILED
} WebResponseState;

class AsyncWebResponse {
  protected:
    int _code;
    WebResponseState _state;
    AsyncWebRequest *_request;

    const char* _responseCodeToString(void);

  public:
    AsyncWebResponse(int code);
    virtual ~AsyncWebResponse() {}

    virtual void setCode(int code);
    virtual void addHeader(const char *name, const char *value) = 0;

    virtual void _respond(AsyncWebRequest &request);
    virtual void _ack(size_t len, uint32_t time) = 0;
    virtual size_t _process(size_t resShare) = 0;

    inline void MUSTNOTSTART(void) const { while (_started()) panic(); }
    inline bool _started(void) const { return _state > RESPONSE_SETUP; }
    inline bool _sending(void) const { return _started() && _state < RESPONSE_WAIT_ACK; }
    inline bool _waitack(void) const { return _state == RESPONSE_WAIT_ACK; }
    inline bool _finished(void) const { return _state > RESPONSE_WAIT_ACK; }
    inline bool _failed(void) const { return _state == RESPONSE_FAILED; }

    ESPWS_DEBUGDO(const char* _stateToString(void) const);
};

/*
 * FILTER :: Callback to filter AsyncWebRewrite and AsyncWebHandler (done by the Server)
 * */

typedef std::function<bool(AsyncWebRequest const &request)> ArRequestFilterFunction;

bool ON_STA_FILTER(AsyncWebRequest const &request);
bool ON_AP_FILTER(AsyncWebRequest const &request);

class AsyncWebFilterable {
  protected:
    LinkedList<ArRequestFilterFunction> _filters;

  public:
    AsyncWebFilterable(void) : _filters(NULL) {}
    virtual ~AsyncWebFilterable() {}

    void addFilter(ArRequestFilterFunction const &fn) { _filters.append(fn); }
    bool _filter(AsyncWebRequest &request) const {
      return _filters.get_if([&](ArRequestFilterFunction const& f){
        return !f(request);
      }) == NULL;
    }
};

/*
 * REWRITE :: One instance can be handle any Request (done by the Server)
 * */

class AsyncWebRewrite : public AsyncWebFilterable {
  protected:
    // Provide accessors to request object
    static void __setUrl(AsyncWebRequest &request, String const &newUrl)
    { request._setUrl(newUrl); }
  public:
    virtual void _perform(AsyncWebRequest &request) = 0;
};

/*
 * HANDLER :: One instance can be attached to any Request (done by the Server)
 * */
typedef std::function<void(AsyncWebRequest &request)> ArRequestHandlerFunction;
#ifdef HANDLE_REQUEST_CONTENT
  typedef std::function<void(AsyncWebRequest &request, size_t index, uint8_t *buf,
                             size_t size, size_t total)> ArBodyHandlerFunction;
#endif

class AsyncWebHandler : public AsyncWebFilterable {
  public:
    virtual bool _isInterestingHeader(String const& key) { return false; }
    virtual bool _canHandle(AsyncWebRequest const &request) { return false; }
    virtual bool _checkContinue(AsyncWebRequest &request, bool continueHeader);

    virtual void _handleRequest(AsyncWebRequest &request) = 0;
#ifdef HANDLE_REQUEST_CONTENT
    virtual void _handleBody(AsyncWebRequest &request, size_t index, uint8_t *buf,
                             size_t size, size_t total) = 0;
#endif
};

class AsyncCallbackWebHandler;
class AsyncStaticWebHandler;

/*
 * SERVER :: One instance
 * */

class AsyncWebServer {
  protected:
    AsyncServer _server;
    uint32_t _reqIdleTimeout;
    LinkedList<AsyncWebRewrite*> _rewrites;
    LinkedList<AsyncWebHandler*> _handlers;

    class AsyncWebSimpleRewrite : public AsyncWebRewrite {
      public:
        String const from;
        String const to;

        AsyncWebSimpleRewrite(const char* src, const char* dst)
        : from(src), to(dst) {
          addFilter([&](AsyncWebRequest const& request){
            return from == request.url();
          });
        }

        virtual void _perform(AsyncWebRequest &request) override
        { __setUrl(request, to); }
    };

    class AsyncWebCatchAllHandler : public AsyncWebHandler {
      public:
        ArRequestHandlerFunction onRequest;
#ifdef HANDLE_REQUEST_CONTENT
        ArBodyHandlerFunction onBody;
#endif

        virtual bool _isInterestingHeader(String const& key) override { return true; }
        virtual void _handleRequest(AsyncWebRequest &request) override
        { if (onRequest) onRequest(request); }

#ifdef HANDLE_REQUEST_CONTENT
        virtual void _handleBody(AsyncWebRequest &request, size_t index, uint8_t *buf,
                                 size_t size, size_t total)
        { if (onBody) onBody(request, index, buf, size, total); }
#endif
    } _catchAllHandler;

#if ASYNC_TCP_SSL_ENABLED
    int _loadSSLCert(const char *filename, uint8_t **buf);
#endif

    void _handleClient(AsyncClient* c);

  public:
    static char const *VERTOKEN;

    AsyncWebServer(uint16_t port, uint32_t reqIdleTimeout = DEFAULT_REQIDLE_TIMEOUT);
    ~AsyncWebServer() { }

    void begin() { _server.begin(); }
#if ASYNC_TCP_SSL_ENABLED
    void beginSecure(const char *cert, const char *private_key_file, const char *password);
#endif

    AsyncWebRewrite* addRewrite(AsyncWebRewrite* rewrite) { return _rewrites.append(rewrite), rewrite; }
    bool removeRewrite(AsyncWebRewrite* rewrite) { return _rewrites.remove(rewrite); }
    AsyncWebRewrite* rewrite(const char* from, const char* to)
    { return addRewrite(new AsyncWebSimpleRewrite(from, to)); }

    AsyncWebHandler* addHandler(AsyncWebHandler* handler) { return _handlers.append(handler), handler; }
    bool removeHandler(AsyncWebHandler* handler) { return _handlers.remove(handler); }

    AsyncCallbackWebHandler& on(const char* uri, ArRequestHandlerFunction const& onRequest)
    { return on(uri, HTTP_GET, onRequest); }

    AsyncCallbackWebHandler& on(const char* uri,
                                WebRequestMethodComposite method,
                                ArRequestHandlerFunction const& onRequest);
#ifdef HANDLE_REQUEST_CONTENT
    AsyncCallbackWebHandler& on(const char* uri,
                                WebRequestMethodComposite method,
                                ArRequestHandlerFunction const& onRequest,
                                ArBodyHandlerFunction const& onBody);
#endif

    AsyncStaticWebHandler& serveStatic(const char* uri, Dir const& dir,
                                       const char* indexFile = DEFAULT_INDEX_FILE,
                                       const char* cache_control = DEFAULT_CACHE_CTRL);

    // Called when handler is not assigned
    void catchAll(ArRequestHandlerFunction const& onRequest)
    { _catchAllHandler.onRequest = onRequest; }

#ifdef HANDLE_REQUEST_CONTENT
    void catchAll(ArBodyHandlerFunction const& onBody)
    { _catchAllHandler.onBody = onBody; }
#endif

    void reset() {
      //remove all writers and handlers, including catch-all handlers
      _catchAllHandler.onRequest = NULL;
#ifdef HANDLE_REQUEST_CONTENT
        _catchAllHandler.onBody = NULL;
#endif
    }

    void _handleDisconnect(AsyncWebRequest *request) const { delete request; }
    void _rewriteRequest(AsyncWebRequest &request) const;
    void _attachHandler(AsyncWebRequest &request) const;
};

#include "WebResponseImpl.h"
#include "WebHandlerImpl.h"

#include "AsyncWebSocket.h"
#include "AsyncEventSource.h"

#endif /* _AsyncWebServer_H_ */
