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

#include "Misc.h"

#define ESPWS_LOG(...) Serial.printf(__VA_ARGS__)
#define ESPWS_DEBUG_LEVEL 3

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

//#define STRICT_PROTOCOL

#define HANDLE_REQUEST_CONTENT
#define HANDLE_REQUEST_CONTENT_SIMPLEFORM
#define HANDLE_REQUEST_CONTENT_MULTIPARTFORM

#define HANDLE_AUTHENTICATION

#define HANDLE_WEBDAV

#define REQUEST_PARAM_MEMCACHE    1024
#define REQUEST_PARAM_KEYMAX      128

#define DEFAULT_IDLE_TIMEOUT      10        // Unit s
#define DEFAULT_ACK_TIMEOUT       10 * 1000 // Unit ms
#define DEFAULT_CACHE_CTRL        "public, no-cache"
#define DEFAULT_INDEX_FILE        "index.htm"

#ifdef HANDLE_AUTHENTICATION
#define DEFAULT_REALM             "ESP8266"
#define DEFAULT_NONCE_LIFE        300
#define DEFAULT_NONCE_RENWEAL     30

#include "ESPEasyAuth.h"
#endif

typedef enum {
  HTTP_NONE      = 0b0000000000000000,
  HTTP_GET       = 0b0000000000000001,
  HTTP_PUT       = 0b0000000000000010,
  HTTP_POST      = 0b0000000000000100,
  HTTP_HEAD      = 0b0000000000001000,
  HTTP_DELETE    = 0b0000000000010000,
  HTTP_PATCH     = 0b0000000000100000,
  HTTP_OPTIONS   = 0b0000000001000000,
#ifdef HANDLE_WEBDAV
  HTTP_COPY      = 0b0000000010000000,
  HTTP_MOVE      = 0b0000000100000000,
  HTTP_MKCOL     = 0b0000001000000000,
  HTTP_LOCK      = 0b0000010000000000,
  HTTP_UNLOCK    = 0b0000100000000000,
  HTTP_PROPFIND  = 0b0001000000000000,
  HTTP_PROPPATCH = 0b0010000000000000,
#endif
  HTTP_UNKNOWN   = 0b1000000000000000,
} WebRequestMethod;

typedef uint16_t WebRequestMethodComposite;
extern WebRequestMethodComposite const HTTP_ANY;
extern WebRequestMethodComposite const HTTP_ANY_READ;
extern WebRequestMethodComposite const HTTP_ANY_WRITE;
extern WebRequestMethodComposite const HTTP_STANDARD;
extern WebRequestMethodComposite const HTTP_STANDARD_READ;
extern WebRequestMethodComposite const HTTP_STANDARD_WRITE;
extern WebRequestMethodComposite const HTTP_BASIC;
extern WebRequestMethodComposite const HTTP_BASIC_READ;
extern WebRequestMethodComposite const HTTP_BASIC_WRITE;
#ifdef HANDLE_WEBDAV
extern WebRequestMethodComposite const HTTP_WEBDAV;
extern WebRequestMethodComposite const HTTP_WEBDAV_READ;
extern WebRequestMethodComposite const HTTP_WEBDAV_WRITE;
#endif
/*
 * HEADER :: Hold a header and its values
 * */

class AsyncWebHeader {
  public:
    String name;
    StringArray values;

    AsyncWebHeader(const String& n, const String& v): name(n) { values.append(v); }
#ifdef __GXX_EXPERIMENTAL_CXX0X__
    AsyncWebHeader(String&& n, String&& v): name(std::move(n)) { values.append(std::move(v)); }
#endif
};

/*
 * QUERY :: Hold a query key and value
 * */

class AsyncWebQuery {
  public:
    String name;
    String value;

    AsyncWebQuery(const String& n, const String& v): name(n), value(v) {}
#ifdef __GXX_EXPERIMENTAL_CXX0X__
    AsyncWebQuery(String&& n, String&& v): name(std::move(n)), value(std::move(v)) {}
#endif
};

#ifdef HANDLE_REQUEST_CONTENT

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
class AsyncWebParam {
  public:
    String name;
    String value;

    AsyncWebParam(const String& n, const String& v): name(n), value(v) {}
#ifdef __GXX_EXPERIMENTAL_CXX0X__
    AsyncWebParam(String&& n, String&& v): name(std::move(n)), value(std::move(v)) {}
#endif
};
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
class AsyncWebUpload : public AsyncWebParam {
  public:
    String contentType;
    size_t contentLength;

    AsyncWebUpload(const String& n, const String& v): AsyncWebParam(n, v) {}
#ifdef __GXX_EXPERIMENTAL_CXX0X__
    AsyncWebUpload(String&& n, String&& v): AsyncWebParam(std::move(n), std::move(v)) {}
#endif
};
#endif

#endif

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
  REQUEST_ERROR,
  REQUEST_FINALIZE
} WebServerRequestState;

String urlDecode(char const *buf, size_t len);
String urlEncode(char const *buf, size_t len);

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

    bool _keepAlive;
    uint8_t _version;
    WebRequestMethod _method;
    String _url;
    String _oUrl;
    String _oQuery;

    String _host;
    String _contentType;
    size_t _contentLength;

#ifdef HANDLE_AUTHENTICATION
    AuthSession* _session;
#endif

    LinkedList<AsyncWebHeader> _headers;
    LinkedList<AsyncWebQuery> _queries;

#ifdef HANDLE_REQUEST_CONTENT

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
    LinkedList<AsyncWebParam> _params;
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
    LinkedList<AsyncWebUpload> _uploads;
#endif

#endif

    void _onAck(size_t len, uint32_t time);
    void _onError(int8_t error);
    void _onTimeout(uint32_t time);
    void _onDisconnect(void);
    void _onData(void *buf, size_t len);

    void _setUrl(String const& url) { _setUrl(std::move(String(url))); }
    void _setUrl(String && url);
    void _parseQueries(char *buf);

#ifdef HANDLE_AUTHENTICATION
    bool _setSession(AuthSession *session);
#endif

    template<typename T>
    T& _addUniqueNameVal(LinkedList<T>& storage, String &name, String &value) {
      T* qPtr = storage.get_if([&](T const &v) {
        return name.equals(v.name);
      });
      if (qPtr) {
        ESPWS_DEBUG("[%s] WARNING: Override value '%s' of duplicate key '%s'\n", _remoteIdent.c_str(),
                    qPtr->value.c_str(), qPtr->name.c_str());
        qPtr->value = std::move(value);
        return *qPtr;
      } else {
        storage.append(T(std::move(name), std::move(value)));
        return storage.back();
      }
    }

    void _recycleClient(void);
    ESPWS_DEBUGDO(const char* _stateToString(void) const);

  public:
    AsyncClient& _client;
    AsyncWebServer const &_server;
    ESPWS_DEBUGDO(String const _remoteIdent);

    AsyncWebRequest(AsyncWebServer const &server, AsyncClient &client);
    ~AsyncWebRequest(void);

    bool _makeProgress(size_t resShare, bool timer);

    uint8_t version(void) const { return _version; }
    WebRequestMethod method(void) const { return _method; }
    const char * methodToString(void) const;
    const String& url(void) const { return _url; }
    const String& oUrl(void) const { return _oUrl; }
    const String& oQuery(void) const { return _oQuery; }

    const String& host(void) const { return _host; }
    bool keepAlive(void) const { return _keepAlive; }

    const String& contentType(void) const { return _contentType; }
    bool contentType(const String& type) const { return _contentType.equalsIgnoreCase(type); }
    size_t contentLength(void) const { return _contentLength; }

#ifdef HANDLE_AUTHENTICATION
    AuthSession* session(void) const { return _session; }
#endif

    size_t headers(void) const { return _headers.length(); }
    bool hasHeader(const String& name) const;
    AsyncWebHeader const* getHeader(const String& name) const;

    void enumHeaders(LinkedList<AsyncWebHeader>::Predicate const& Pred)
    { _headers.get_if(Pred); }

    size_t queries(void) const { return _queries.length(); }
    bool hasQuery(const String& name) const;
    AsyncWebQuery const* getQuery(const String& name) const;

    void enumQueries(LinkedList<AsyncWebQuery>::Predicate const& Pred)
    { _queries.get_if(Pred); }

#ifdef HANDLE_REQUEST_CONTENT

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
    size_t params(void) const { return _params.length(); }
    bool hasParam(const String& name) const;
    AsyncWebParam const* getParam(const String& name) const;

    void enumParams(LinkedList<AsyncWebParam>::Predicate const& Pred)
    { _params.get_if(Pred); }
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
    size_t uploads(void) const { return _uploads.length(); }
    bool hasUpload(const String& name) const;
    AsyncWebUpload const* getUpload(const String& name) const;

    void enumUploads(LinkedList<AsyncWebUpload>::Predicate const& Pred)
    { _uploads.get_if(Pred); }
#endif

#endif

    void send(AsyncWebResponse *response);
    void noKeepAlive(void) { _keepAlive = false; }

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
    AsyncWebResponse(int code);

  public:
    virtual ~AsyncWebResponse() {}

    virtual void setCode(int code);
    virtual void addHeader(const char *name, const char *value) = 0;

    virtual void _respond(AsyncWebRequest &request);
    virtual void _ack(size_t len, uint32_t time) = 0;
    virtual size_t _process(size_t resShare) = 0;

    inline void MUSTNOTSTART(void) const { ESPWS_DEBUGDO(while (_started()) panic()); }
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

typedef std::function<bool(AsyncWebRequest const&)> ArRequestFilterFunction;

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
typedef std::function<void(AsyncWebRequest&)> ArRequestHandlerFunction;
#ifdef HANDLE_REQUEST_CONTENT
  typedef std::function<bool(AsyncWebRequest&,
                             size_t, void*, size_t)> ArBodyHandlerFunction;

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
  typedef std::function<bool(AsyncWebRequest&, String const&,
                             size_t, void*, size_t)> ArParamDataHandlerFunction;
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
  typedef std::function<bool(AsyncWebRequest&, String const&, String const&, String const&,
                             size_t, void*, size_t)> ArUploadDataHandlerFunction;
#endif

#endif

class AsyncWebHandler : public AsyncWebFilterable {
  public:
    virtual bool _isInterestingHeader(String const& key) { return false; }
    virtual bool _canHandle(AsyncWebRequest const &request) { return false; }
    virtual bool _checkContinue(AsyncWebRequest &request, bool continueHeader);

    virtual void _handleRequest(AsyncWebRequest &request) = 0;
#ifdef HANDLE_REQUEST_CONTENT
    virtual bool _handleBody(AsyncWebRequest &request,
                             size_t offset, void *buf, size_t size) = 0;

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
    virtual bool _handleParamData(AsyncWebRequest &request, String const& name,
                                  size_t offset, void *buf, size_t size) = 0;
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
    virtual bool _handleUploadData(AsyncWebRequest &request, String const& name, String const& filename,
                                   String const& contentType, size_t offset, void *buf, size_t size) = 0;
#endif

#endif
};

class AsyncCallbackWebHandler;
class AsyncStaticWebHandler;

#ifdef HANDLE_AUTHENTICATION
typedef enum {
  AUTHHEADER_ANONYMOUS,
  AUTHHEADER_MALFORMED,
  AUTHHEADER_NORECORD,
  AUTHHEADER_UNACCEPT,
  AUTHHEADER_EXPIRED,
  AUTHHEADER_PREAUTH,
} WebAuthHeaderState;

typedef enum {
  AUTH_NONE   = 0b00000001,
  AUTH_BASIC  = 0b00000010,
  AUTH_DIGEST = 0b00000100,
  AUTH_OTHER  = 0b10000000,
} WebAuthType;

typedef uint8_t WebAuthTypeComposite;
extern WebAuthTypeComposite const AUTH_ANY;
extern WebAuthTypeComposite const AUTH_REQUIRE;
extern WebAuthTypeComposite const AUTH_SECURE;

struct AsyncWebAuth {
  WebAuthHeaderState State;
  WebAuthType Type;
  String UserName;
  String Secret;

  AsyncWebAuth(WebAuthHeaderState state, WebAuthType type): State(state), Type(type) {}
  ESPWS_DEBUGDO(const char* _stateToString(void) const);
  ESPWS_DEBUGDO(const char* _typeToString(void) const);
};
#endif

/*
 * SERVER :: One instance
 * */

class AsyncWebServer {
  protected:
    AsyncServer _server;
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

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
        ArParamDataHandlerFunction onParamData;
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
        ArUploadDataHandlerFunction onUploadData;
#endif

#endif

        virtual bool _isInterestingHeader(String const& key) override { return true; }
        virtual void _handleRequest(AsyncWebRequest &request) override
        { if (onRequest) onRequest(request); }

#ifdef HANDLE_REQUEST_CONTENT
        virtual bool _handleBody(AsyncWebRequest &request, size_t offset, void *buf, size_t size) override
        { return onBody? onBody(request, offset, buf, size) : true; }

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
        virtual bool _handleParamData(AsyncWebRequest &request, String const& name,
                                      size_t offset, void *buf, size_t size) override
        { return onParamData? onParamData(request, name, offset, buf, size) : true; }
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
        virtual bool _handleUploadData(AsyncWebRequest &request, String const& name, String const& filename,
                                       String const& contentType, size_t offset, void *buf, size_t size) override
        { return onUploadData? onUploadData(request, name, filename, contentType, offset, buf, size) : true; }
#endif

#endif
    } _catchAllHandler;

#if ASYNC_TCP_SSL_ENABLED
    int _loadSSLCert(const char *filename, uint8_t **buf);
#endif

    void _handleClient(AsyncClient* c);

#ifdef HANDLE_AUTHENTICATION
    SessionAuthority *_Auth;

    WebAuthTypeComposite _AuthAcc;
    String _Realm;
    String _Secret;
    time_t _NonceLife;

    struct NONCEREC {
      String const NONCE;
      time_t const EXPIRY;
      String NC;

      NONCEREC(String const &nonce, time_t expiry): NONCE(nonce), EXPIRY(expiry) {}
#ifdef __GXX_EXPERIMENTAL_CXX0X__
      NONCEREC(String &&nonce, time_t expiry): NONCE(std::move(nonce)), EXPIRY(expiry) {}
#endif
    };
    LinkedList<NONCEREC> _DAuthRecs;

    struct HTTPACL {
      String PATH;
      WebRequestMethodComposite METHODS;
      LinkedList<Identity*> IDENTS;

      HTTPACL(String const &p): PATH(p), METHODS(HTTP_NONE), IDENTS(NULL) {}
#ifdef __GXX_EXPERIMENTAL_CXX0X__
      HTTPACL(String &&p): PATH(std::move(p)), METHODS(HTTP_NONE), IDENTS(NULL) {}
#endif
    };
    LinkedList<HTTPACL> _ACLs;
    void loadACL(Stream &source);
#endif

  public:
    static char const *VERTOKEN;

    AsyncWebServer(uint16_t port);
    ~AsyncWebServer() {}

#ifdef HANDLE_AUTHENTICATION
    void configAuthority(SessionAuthority &Auth, Stream &ACLStream);
    void configRealm(String const &realm, String const &secret,
                     WebAuthTypeComposite authAccept = AUTH_ANY,
                     time_t nonceLife = DEFAULT_NONCE_LIFE);
#endif

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

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
    AsyncCallbackWebHandler& on(const char* uri,
                                WebRequestMethodComposite method,
                                ArRequestHandlerFunction const& onRequest,
                                ArBodyHandlerFunction const& onBody,
                                ArParamDataHandlerFunction const& onParamData);
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
    AsyncCallbackWebHandler& on(const char* uri,
                                WebRequestMethodComposite method,
                                ArRequestHandlerFunction const& onRequest,
                                ArBodyHandlerFunction const& onBody,
                                ArParamDataHandlerFunction const& onParamData,
                                ArUploadDataHandlerFunction const& onUploadData);
#endif

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

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
    void catchAll(ArParamDataHandlerFunction const& onParamData)
    { _catchAllHandler.onParamData = onParamData; }
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
    void catchAll(ArUploadDataHandlerFunction const& onUploadData)
    { _catchAllHandler.onUploadData = onUploadData; }
#endif

#endif

    void reset() {
      //remove all writers and handlers, including catch-all handlers
      _catchAllHandler.onRequest = NULL;
#ifdef HANDLE_REQUEST_CONTENT
      _catchAllHandler.onBody = NULL;

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
      _catchAllHandler.onParamData = NULL;
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
      _catchAllHandler.onUploadData = NULL;
#endif

#endif
    }

    void _rewriteRequest(AsyncWebRequest &request) const;
    void _attachHandler(AsyncWebRequest &request) const;

#ifdef HANDLE_AUTHENTICATION
    AsyncWebAuth _parseAuthHeader(String &authHeader, AsyncWebRequest const &request) const;
    AuthSession* _authSession(AsyncWebAuth &authInfo, AsyncWebRequest const &request) const;
    void _genAuthHeader(AsyncWebResponse &response, AsyncWebRequest const &request, bool renew) const;
    bool _checkACL(AsyncWebRequest const &request, AuthSession* session) const;
#endif

    static WebRequestMethod parseMethod(char const *Str);
    static WebRequestMethodComposite parseMethods(char *Str);
    static const char* mapMethod(WebRequestMethod method);
    static String mapMethods(WebRequestMethodComposite methods);
};

#include "WebResponseImpl.h"
#include "WebHandlerImpl.h"

#endif /* _AsyncWebServer_H_ */
