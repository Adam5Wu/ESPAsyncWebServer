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

#ifndef ESPWS_DEBUG_LEVEL
#define ESPWS_DEBUG_LEVEL ESPZW_DEBUG_LEVEL
#endif

#ifndef ESPWS_LOG
#define ESPWS_LOG(...) ESPZW_LOG(__VA_ARGS__)
#define ESPWS_LOG_S(...) ESPZW_LOG_S(__VA_ARGS__)
#endif

#if ESPWS_DEBUG_LEVEL < 1
	#define ESPWS_DEBUGDO(...)
	#define ESPWS_DEBUG(...)
	#define ESPWS_DEBUG_S(...)
#else
	#define ESPWS_DEBUGDO(...) __VA_ARGS__
	#define ESPWS_DEBUG(...) ESPWS_LOG(__VA_ARGS__)
	#define ESPWS_DEBUG_S(...) ESPWS_LOG_S(__VA_ARGS__)
#endif

#if ESPWS_DEBUG_LEVEL < 2
	#define ESPWS_DEBUGVDO(...)
	#define ESPWS_DEBUGV(...)
	#define ESPWS_DEBUGV_S(...)
#else
	#define ESPWS_DEBUGVDO(...) __VA_ARGS__
	#define ESPWS_DEBUGV(...) ESPWS_LOG(__VA_ARGS__)
	#define ESPWS_DEBUGV_S(...) ESPWS_LOG_S(__VA_ARGS__)
#endif

#if ESPWS_DEBUG_LEVEL < 3
	#define ESPWS_DEBUGVVDO(...)
	#define ESPWS_DEBUGVV(...)
	#define ESPWS_DEBUGVV_S(...)
#else
	#define ESPWS_DEBUGVVDO(...) __VA_ARGS__
	#define ESPWS_DEBUGVV(...) ESPWS_LOG(__VA_ARGS__)
	#define ESPWS_DEBUGVV_S(...) ESPWS_LOG_S(__VA_ARGS__)
#endif

#define PURGE_TIMEWAIT

#define STRICT_PROTOCOL

#define HANDLE_REQUEST_CONTENT
#define HANDLE_REQUEST_CONTENT_SIMPLEFORM
#define HANDLE_REQUEST_CONTENT_MULTIPARTFORM

#define HANDLE_AUTHENTICATION
#define AUTHENTICATION_DISABLE_BASIC
#define AUTHENTICATION_HA1_CACHE
//#define AUTHENTICATION_ENABLE_SESS
//#define AUTHENTICATION_ENABLE_SESS_BUGCOMPAT

#define ADVANCED_STATIC_WEBHANDLER

#define HANDLE_WEBDAV

//#define ADVERTISE_ACCEPTRANGES

//#define PLATFORM_SIGNATURE

//#define REQUEST_USERAGENT
//#define REQUEST_ACCEPTLANG
//#define REQUEST_REFERER

//#define SUPPORT_CGI // Provision for CGI support (not implemented)

#define REQUEST_PARAM_MEMCACHE    1024
#define REQUEST_PARAM_KEYMAX      128

#define DEFAULT_IDLE_TIMEOUT      10        // Unit s
#define DEFAULT_ACK_TIMEOUT       10 * 1000 // Unit ms
#define DEFAULT_CACHE_CTRL        "private, no-cache"
#define DEFAULT_INDEX_FILE        "index.htm"

#ifdef HANDLE_AUTHENTICATION
#define DEFAULT_REALM             "ESPAsyncWeb"
#define DEFAULT_NONCE_LIFE        120
#define DEFAULT_NONCE_RENEWAL     30
#define DEFAULT_NONCE_MAXIMUM     10

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
extern WebRequestMethodComposite const HTTP_BASIC;
extern WebRequestMethodComposite const HTTP_BASIC_READ;
extern WebRequestMethodComposite const HTTP_BASIC_WRITE;
extern WebRequestMethodComposite const HTTP_EXT;
extern WebRequestMethodComposite const HTTP_EXT_READ;
extern WebRequestMethodComposite const HTTP_EXT_WRITE;
extern WebRequestMethodComposite const HTTP_STANDARD;
extern WebRequestMethodComposite const HTTP_STANDARD_READ;
extern WebRequestMethodComposite const HTTP_STANDARD_WRITE;
#ifdef HANDLE_WEBDAV
extern WebRequestMethodComposite const HTTP_DAVEXT;
extern WebRequestMethodComposite const HTTP_DAVEXT_READ;
extern WebRequestMethodComposite const HTTP_DAVEXT_WRITE;
extern WebRequestMethodComposite const HTTP_DAVEXT_CONTROL;
extern WebRequestMethodComposite const HTTP_WEBDAV;
extern WebRequestMethodComposite const HTTP_WEBDAV_READ;
extern WebRequestMethodComposite const HTTP_WEBDAV_WRITE;
#endif
extern WebRequestMethodComposite const HTTP_ANY;
extern WebRequestMethodComposite const HTTP_ANY_READ;
extern WebRequestMethodComposite const HTTP_ANY_WRITE;

/*
 * HEADER :: Hold a header and its values
 * */

class AsyncWebHeader {
	public:
		String name;
		StringArray values;

		AsyncWebHeader(String const &n, String const &v): name(n)
		{ values.append(v); }
		AsyncWebHeader(String &&n, String &&v): name(std::move(n))
		{ values.append(std::move(v)); }
};

/*
 * QUERY :: Hold a query key and value
 * */

class AsyncWebQuery {
	public:
		String name;
		String value;

		AsyncWebQuery(String const &n, String const &v): name(n), value(v) {}
		AsyncWebQuery(String &&n, String &&v): name(std::move(n)), value(std::move(v)) {}
};

#ifdef HANDLE_REQUEST_CONTENT

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
class AsyncWebParam {
	public:
		String name;
		String value;

		AsyncWebParam(String const &n, String const &v): name(n), value(v) {}
		AsyncWebParam(String &&n, String &&v): name(std::move(n)), value(std::move(v)) {}
};
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
class AsyncWebUpload : public AsyncWebParam {
	public:
		String contentType;
		size_t contentLength;

		AsyncWebUpload(String const &n, String const &v): AsyncWebParam(n, v) {}
		AsyncWebUpload(String &&n, String &&v): AsyncWebParam(std::move(n), std::move(v)) {}
};
#endif

#endif

class AsyncWebServer;
class AsyncWebParser;
class AsyncWebRewrite;
class AsyncWebHandler;
class AsyncWebResponse;
class AsyncPrintResponse;

#ifdef HANDLE_AUTHENTICATION
class WebAuthSession;
#endif

typedef enum {
	REQUEST_SETUP,
	REQUEST_START,
	REQUEST_HEADERS,
	REQUEST_BODY,
	REQUEST_RECEIVED,
	REQUEST_RESPONSE,
	REQUEST_ERROR,
	REQUEST_HALT,
	REQUEST_FINALIZE
} WebServerRequestState;

String urlDecode(char const *buf, size_t len);
String urlEncode(char const *buf, size_t len);

/*
 * REQUEST :: Each incoming Client is wrapped inside a Request and both live together until disconnect
 * */

typedef std::function<size_t(uint8_t*, size_t, size_t)> AwsResponseFiller;

#ifdef HANDLE_AUTHENTICATION
typedef enum {
	ACL_NONE,
	ACL_NOTFOUND,
	ACL_NOTALLOWED,
	ACL_ALLOWED,
} WebACLMatchResult;
#endif

class AsyncWebRequest;
typedef std::function<void(AsyncWebRequest*)> ArTerminationNotify;

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
		WebRequestMethod _method;
		String _url;
		String _oUrl;
		String _oQuery;

		String _host;
		String _accept;
		String _acceptEncoding;
#ifdef REQUEST_ACCEPTLANG
		String _acceptLanguage;
#endif
#ifdef REQUEST_USERAGENT
		String _userAgent;
#endif
#ifdef REQUEST_REFERER
		String _Referer;
#endif
		String _contentType;
		size_t _contentLength;

		bool _keepAlive;
#ifdef HANDLE_WEBDAV
		bool _translate;
#endif

#ifdef HANDLE_AUTHENTICATION
		WebAuthSession* _session;
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

		void _setUrl(String const& url) { _setUrl(String(url)); }
		void _setUrl(String &&url);
		void _parseQueries(char *buf);

#ifdef HANDLE_AUTHENTICATION
		WebACLMatchResult _setSession(WebAuthSession *session);
#endif

		template<typename T>
		T& _addUniqueNameVal(LinkedList<T>& storage, String &name, String &value) {
			T* qPtr = storage.get_if([&](T const &v) {
				return name == v.name;
			});
			if (qPtr) {
				ESPWS_DEBUG_S(T,"[%s] WARNING: Override value '%s' of duplicate key '%s'\n",
					_remoteIdent.c_str(), qPtr->value.c_str(), qPtr->name.c_str());
				qPtr->value = std::move(value);
				return *qPtr;
			} else {
				storage.append(T(std::move(name), std::move(value)));
				return storage.back();
			}
		}

		void _recycleClient(void);
		ESPWS_DEBUGDO(PGM_P _stateToString(void) const);

	protected:
		ArTerminationNotify _termNotify;
		AsyncWebRequest(AsyncWebServer const &server, AsyncClient &client,
			ArTerminationNotify const &termNotify);

	public:
		AsyncClient &_client;
		AsyncWebServer const &_server;
		ESPWS_DEBUGDO(String const _remoteIdent);

		~AsyncWebRequest(void);
		bool _makeProgress(size_t resShare, bool timer);

		uint8_t version(void) const { return _version; }
		WebRequestMethod method(void) const { return _method; }
		PGM_P methodToString(void) const;
		String const &url(void) const { return _url; }
		String const &oUrl(void) const { return _oUrl; }
		String const &oQuery(void) const { return _oQuery; }

		String const &host(void) const { return _host; }
		String const &accept(void) const { return _accept; }
		String const &acceptEncoding(void) const { return _acceptEncoding; }

#ifdef REQUEST_ACCEPTLANG
		String const &acceptLanguage(void) const { return _acceptLanguage; }
#endif
#ifdef REQUEST_USERAGENT
		String const &userAgent(void) const { return _userAgent; }
#endif
#ifdef REQUEST_REFERER
		String const &referer(void) const { return _referer; }
#endif

		bool keepAlive(void) const { return _keepAlive; }
#ifdef HANDLE_WEBDAV
		bool translate(void) const { return _translate; }
#endif

		String const &contentType(void) const { return _contentType; }
		bool contentType(String const &type) const
		{ return _contentType.equalsIgnoreCase(type); }
		size_t contentLength(void) const { return _contentLength; }

#ifdef HANDLE_AUTHENTICATION
		WebAuthSession* session(void) const { return _session; }
#endif

		size_t headers(void) const { return _headers.length(); }
		bool hasHeader(String const &name) const;
		AsyncWebHeader const* getHeader(String const &name) const;

		void enumHeaders(LinkedList<AsyncWebHeader>::Predicate const& Pred)
		{ _headers.get_if(Pred); }

		size_t queries(void) const { return _queries.length(); }
		bool hasQuery(String const &name) const;
		AsyncWebQuery const* getQuery(String const &name) const;

		void enumQueries(LinkedList<AsyncWebQuery>::Predicate const& Pred)
		{ _queries.get_if(Pred); }

#ifdef HANDLE_REQUEST_CONTENT

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
		size_t params(void) const { return _params.length(); }
		bool hasParam(String const &name) const;
		AsyncWebParam const* getParam(String const &name) const;

		void enumParams(LinkedList<AsyncWebParam>::Predicate const& Pred)
		{ _params.get_if(Pred); }
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
		size_t uploads(void) const { return _uploads.length(); }
		bool hasUpload(String const &name) const;
		AsyncWebUpload const* getUpload(String const &name) const;

		void enumUploads(LinkedList<AsyncWebUpload>::Predicate const& Pred)
		{ _uploads.get_if(Pred); }
#endif

#endif

		void send(AsyncWebResponse *response);
		void noKeepAlive(void) { _keepAlive = false; }

		// Response short-hands
		void redirect(String const &url);

		AsyncPrintResponse *beginPrintResponse(int code, String const &contentType);

		AsyncWebResponse *beginResponse(int code, String const &content=String::EMPTY,
			String const &contentType=String::EMPTY);
		AsyncWebResponse *beginResponse(int code, String &&content,
			String const &contentType=String::EMPTY);
		AsyncWebResponse *beginResponse(FS &fs, String const &path,
			String const &contentType=String::EMPTY, int code = 200, bool download=false);
		AsyncWebResponse *beginResponse(File content, String const &path,
			String const &contentType=String::EMPTY, int code = 200, bool download=false);
		AsyncWebResponse *beginResponse(int code, Stream &content,
			String const &contentType, size_t len);
		AsyncWebResponse *beginResponse(int code, AwsResponseFiller callback,
			String const &contentType, size_t len);
		AsyncWebResponse *beginChunkedResponse(int code,  AwsResponseFiller callback,
			String const &contentType);
		AsyncWebResponse *beginResponse_P(int code, PGM_P content,
			String const &contentType, size_t len=-1);

		inline void send(int code, String const &content=String::EMPTY,
			String const &contentType=String::EMPTY)
		{ send(beginResponse(code, content, contentType)); }

		inline void send(int code, String &&content, String const &contentType=String::EMPTY)
		{ send(beginResponse(code, std::move(content), contentType)); }

		inline void send(FS &fs, String const &path, String const &contentType=String::EMPTY,
			int code=200, bool download=false)
		{ send(beginResponse(fs, path, contentType, code, download)); }

		inline void send(File content, String const &path, String const &contentType=String::EMPTY,
			int code=200, bool download=false)
		{ send(beginResponse(content, path, contentType, code, download)); }

		inline void send(int code, Stream &content, String const &contentType, size_t len)
		{ send(beginResponse(code, content, contentType, len)); }

		inline void send(int code, AwsResponseFiller callback, String const &contentType, size_t len)
		{ send(beginResponse(code, callback, contentType, len)); }

		inline void sendChunked(int code, AwsResponseFiller callback, String const &contentType)
		{ send(beginChunkedResponse(code, callback, contentType)); }

		inline void send_P(int code, PGM_P content, String const &contentType, size_t len=-1)
		{ send(beginResponse_P(code, content, contentType, len)); }

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

		PGM_P _responseCodeToString(void);
		AsyncWebResponse(int code);

	public:
		virtual ~AsyncWebResponse(void);

		virtual void setCode(int code);
		virtual void addHeader(String const &name, String const &value) = 0;

		virtual void _respond(AsyncWebRequest &request);
		virtual void _ack(size_t len, uint32_t time) = 0;
		virtual size_t _process(size_t resShare) = 0;

		inline bool _started(void) const { return _state > RESPONSE_SETUP; }
		inline bool _sending(void) const { return _started() && _state < RESPONSE_WAIT_ACK; }
		inline bool _waitack(void) const { return _state == RESPONSE_WAIT_ACK; }
		inline bool _finished(void) const { return _state > RESPONSE_WAIT_ACK; }
		inline bool _failed(void) const { return _state == RESPONSE_FAILED; }

		ESPWS_DEBUGDO(PGM_P _stateToString(void) const);
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
		AsyncWebFilterable(void) : _filters(nullptr) {}
		virtual ~AsyncWebFilterable() {}

		void addFilter(ArRequestFilterFunction const &fn) { _filters.append(fn); }
		bool _filter(AsyncWebRequest &request) const {
			return _filters.get_if([&](ArRequestFilterFunction const& f){
				return !f(request);
			}) == nullptr;
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
typedef std::function<bool(AsyncWebRequest&, size_t, void*, size_t)> ArBodyHandlerFunction;

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
		virtual bool _isInterestingHeader(AsyncWebRequest const &request, String const& key)
		{ return false; }
		virtual bool _canHandle(AsyncWebRequest const &request) { return false; }
		virtual bool _checkContinue(AsyncWebRequest &request, bool continueHeader);
		virtual void _terminateRequest(AsyncWebRequest &request) { }

		virtual void _handleRequest(AsyncWebRequest &request) = 0;
#ifdef HANDLE_REQUEST_CONTENT
		virtual bool _handleBody(AsyncWebRequest &request,
			size_t offset, void *buf, size_t size) = 0;

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
		virtual bool _handleParamData(AsyncWebRequest &request, String const& name,
			size_t offset, void *buf, size_t size) = 0;
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
		virtual bool _handleUploadData(AsyncWebRequest &request, String const& name,
			String const& filename, String const& contentType,
			size_t offset, void *buf, size_t size) = 0;
#endif

#endif
};

class AsyncPassthroughWebHandler : public AsyncWebHandler {
	public:
		// Dummy implementation, should never reach
		virtual void _handleRequest(AsyncWebRequest &request) override {}
#ifdef HANDLE_REQUEST_CONTENT
		virtual bool _handleBody(AsyncWebRequest &request,
			size_t offset, void *buf, size_t size) override { return false; }

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
		virtual bool _handleParamData(AsyncWebRequest &request, String const& name,
			size_t offset, void *buf, size_t size) override { return false; }
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
		virtual bool _handleUploadData(AsyncWebRequest &request, String const& name,
			String const& filename, String const& contentType,
			size_t offset, void *buf, size_t size) override { return false; }
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

struct NONCEREC {
	String const NONCE;
#ifdef AUTHENTICATION_HA1_CACHE
		String HA1;
#endif
#ifdef AUTHENTICATION_ENABLE_SESS
#ifdef AUTHENTICATION_ENABLE_SESS_BUGCOMPAT
	String CNONCE;
#endif
#endif
	time_t const EXPIRY;
	uint32_t NC = 0;

	NONCEREC(String const &nonce, time_t expiry): NONCE(nonce), EXPIRY(expiry) {}
	NONCEREC(String &&nonce, time_t expiry): NONCE(std::move(nonce)), EXPIRY(expiry) {}
};

struct AsyncWebAuth {
	WebAuthHeaderState State;
	WebAuthType Type;
	NONCEREC *NRec;
	String UserName;
	String Secret;

	AsyncWebAuth(WebAuthHeaderState state, WebAuthType type)
		: State(state), Type(type), NRec(nullptr) {}
	ESPWS_DEBUGDO(PGM_P _stateToString(void) const);
	ESPWS_DEBUGDO(PGM_P _typeToString(void) const);
};

class WebAuthSession : public AuthSession {
	public:
		WebAuthType const Type;
		NONCEREC *const NRec;

		WebAuthSession(Identity &ident, Authorizer *auth, AsyncWebAuth &authInfo)
			: AuthSession(ident, auth), Type(authInfo.Type), NRec(authInfo.NRec) {}

		WebAuthSession(AuthSession &&session, AsyncWebAuth &authInfo)
			: AuthSession(std::move(session)), Type(authInfo.Type), NRec(authInfo.NRec) {}
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

		AsyncCallbackWebHandler *_catchAllHandler;

#if ASYNC_TCP_SSL_ENABLED
		int _loadSSLCert(const char *filename, uint8_t **buf);
#endif

		void _handleClient(AsyncClient* c);

		LinkedList<AsyncWebRequest*> _requests;
		void _requestFinish(AsyncWebRequest *r);

#ifdef HANDLE_AUTHENTICATION
		SessionAuthority *_Auth;

		WebAuthTypeComposite _AuthAcc;
		String _Realm;
		String _Secret;

		LinkedList<NONCEREC> _DAuthRecs;

		struct HTTPACL {
			String PATH;
			WebRequestMethodComposite METHODS;
			LinkedList<Identity*> IDENTS;

			HTTPACL(String const &p): PATH(p), METHODS(HTTP_NONE), IDENTS(nullptr) {}
			HTTPACL(String &&p): PATH(std::move(p)), METHODS(HTTP_NONE), IDENTS(nullptr) {}
			HTTPACL(String &&p, WebRequestMethodComposite m, LinkedList<Identity*> &&i)
				: PATH(std::move(p)), METHODS(m), IDENTS(std::move(i)) {}
		};
		LinkedList<HTTPACL> _ACLs;
		void loadACL(Stream &source);
#endif

	public:
		static PGM_P VERTOKEN;

		AsyncWebServer(uint16_t port);
		~AsyncWebServer(void);

#ifdef HANDLE_AUTHENTICATION
		void configAuthority(SessionAuthority &Auth, Stream &ACLStream);
		void configRealm(String const &realm, String const &secret = String::EMPTY,
			WebAuthTypeComposite authAccept = AUTH_ANY);
#endif

		void begin() { _server.setNoDelay(true); _server.begin(); }
#if ASYNC_TCP_SSL_ENABLED
		void beginSecure(const char *cert, const char *private_key_file, const char *password);
#endif

		void end();
		bool hasFinished() { return !_server.status() && _requests.isEmpty(); }

		AsyncWebRewrite& addRewrite(AsyncWebRewrite* rewrite) {
			return _rewrites.append(rewrite), *rewrite;
		}
		bool removeRewrite(AsyncWebRewrite* rewrite) { return _rewrites.remove(rewrite); }
		AsyncWebRewrite& rewrite(const char* from, const char* to)
		{ return addRewrite(new AsyncWebSimpleRewrite(from, to)); }

		AsyncWebHandler& addHandler(AsyncWebHandler* handler) {
			return _handlers.append(handler), *handler;
		}
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
			const char* cache_control = DEFAULT_CACHE_CTRL
#ifdef ADVANCED_STATIC_WEBHANDLER
			, bool write_support = true
#ifdef HANDLE_WEBDAV
			, bool dav_support = true
#endif
#endif
		);

		// Called when handler is not assigned
		void catchAll(ArRequestHandlerFunction const& onRequest);

#ifdef HANDLE_REQUEST_CONTENT
		void catchAll(ArBodyHandlerFunction const& onBody);

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
		void catchAll(ArParamDataHandlerFunction const& onParamData);
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
		void catchAll(ArUploadDataHandlerFunction const& onUploadData);
#endif

#endif

		void reset(void);

		void _rewriteRequest(AsyncWebRequest &request) const;
		void _attachHandler(AsyncWebRequest &request) const;

#ifdef HANDLE_AUTHENTICATION
		void _authMaintenance(void) const;
		AsyncWebAuth _parseAuthHeader(String &authHeader, AsyncWebRequest const &request) const;
		WebAuthSession* _authSession(AsyncWebAuth &authInfo, AsyncWebRequest const &request) const;
		void _genAuthHeader(AsyncWebResponse &response, AsyncWebRequest const &request,
			bool renew, NONCEREC const *NRec) const;
		WebACLMatchResult _checkACL(WebRequestMethod method, String const &Url,
			AuthSession* session) const;
		size_t _prependACL(String &&url, WebRequestMethodComposite methods,
			LinkedList<Identity*> &&idents)
		{ _ACLs.prepend({std::move(url), methods, std::move(idents)}); }
#endif

		static WebRequestMethod parseMethod(char const *Str);
		static WebRequestMethodComposite parseMethods(char *Str);
		static PGM_P mapMethod(WebRequestMethod method);
		static String mapMethods(WebRequestMethodComposite methods);
};

#include "WebResponseImpl.h"
#include "WebHandlerImpl.h"

#endif /* _AsyncWebServer_H_ */
