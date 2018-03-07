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

WebRequestMethodComposite const HTTP_BASIC_READ     = HTTP_GET | HTTP_HEAD;
WebRequestMethodComposite const HTTP_BASIC_WRITE    = HTTP_PUT | HTTP_POST;
WebRequestMethodComposite const HTTP_BASIC          = HTTP_BASIC_READ | HTTP_BASIC_WRITE;

WebRequestMethodComposite const HTTP_EXT_READ       = HTTP_OPTIONS;
WebRequestMethodComposite const HTTP_EXT_WRITE      = HTTP_DELETE | HTTP_PATCH;
WebRequestMethodComposite const HTTP_EXT            = HTTP_EXT_READ | HTTP_EXT_WRITE;

WebRequestMethodComposite const HTTP_STANDARD_READ  = HTTP_BASIC_READ | HTTP_EXT_READ;
WebRequestMethodComposite const HTTP_STANDARD_WRITE = HTTP_BASIC_WRITE | HTTP_EXT_WRITE;
WebRequestMethodComposite const HTTP_STANDARD       = HTTP_BASIC | HTTP_EXT;

#ifdef HANDLE_WEBDAV
WebRequestMethodComposite const HTTP_DAVEXT_READ    = HTTP_PROPFIND;
WebRequestMethodComposite const HTTP_DAVEXT_WRITE   = HTTP_COPY | HTTP_MOVE | HTTP_MKCOL | HTTP_PROPPATCH;
WebRequestMethodComposite const HTTP_DAVEXT_CONTROL = HTTP_LOCK | HTTP_UNLOCK;
WebRequestMethodComposite const HTTP_DAVEXT         = HTTP_DAVEXT_READ | HTTP_DAVEXT_WRITE | HTTP_DAVEXT_CONTROL;

WebRequestMethodComposite const HTTP_WEBDAV_READ    = HTTP_STANDARD_READ | HTTP_DAVEXT_READ;
WebRequestMethodComposite const HTTP_WEBDAV_WRITE   = HTTP_STANDARD_WRITE | HTTP_DAVEXT_WRITE;
WebRequestMethodComposite const HTTP_WEBDAV         = HTTP_STANDARD | HTTP_DAVEXT;

WebRequestMethodComposite const HTTP_ANY            = HTTP_WEBDAV;
WebRequestMethodComposite const HTTP_ANY_READ       = HTTP_WEBDAV_READ;
WebRequestMethodComposite const HTTP_ANY_WRITE      = HTTP_WEBDAV_WRITE;
#else
WebRequestMethodComposite const HTTP_ANY            = HTTP_STANDARD;
WebRequestMethodComposite const HTTP_ANY_READ       = HTTP_STANDARD_READ;
WebRequestMethodComposite const HTTP_ANY_WRITE      = HTTP_STANDARD_WRITE;
#endif

bool ON_STA_FILTER(AsyncWebRequest const &request) {
	return WiFi.localIP() == request._client.localIP();
}

bool ON_AP_FILTER(AsyncWebRequest const &request) {
	return WiFi.localIP() != request._client.localIP();
}

#define SERVER_NAME "ESPAsyncHTTPD"
#define SERVER_VERSION "0.5"

PGM_P AsyncWebServer::VERTOKEN PROGMEM_L = SERVER_NAME "/" SERVER_VERSION;

#ifdef HANDLE_AUTHENTICATION

#include <StreamString.h>
#include <time.h>
#include <libb64/cdecode.h>
extern "C" {
	#include "user_interface.h"
}

class AnonymousAccountAuthority : public DummyIdentityProvider, public BasicAuthorizer {
	public:
		virtual Identity& getIdentity(String const& identName) const override
		{ return identName.equalsIgnoreCase(ANONYMOUS.ID)? ANONYMOUS : UNKNOWN; }
		virtual bool Authenticate(Credential& cred) override
		{ return cred.IDENT == ANONYMOUS; }
} ANONYMOUS_AUTH;

static SessionAuthority ANONYMOUS_SESSIONS(&ANONYMOUS_AUTH, &ANONYMOUS_AUTH);
#endif

class AsyncCatchAllCallbackWebHandler : public AsyncCallbackWebHandler {
	public:
		virtual bool _isInterestingHeader(AsyncWebRequest const &request,
			String const& key) override
		{ return _loaded(); }
};

AsyncWebServer::AsyncWebServer(uint16_t port)
	: _server(port)
	, _catchAllHandler(new AsyncCatchAllCallbackWebHandler)
	, _rewrites(LinkedList<AsyncWebRewrite*>([](AsyncWebRewrite* r){ delete r; }))
	, _handlers(LinkedList<AsyncWebHandler*>([](AsyncWebHandler* h){ delete h; }))
#ifdef HANDLE_AUTHENTICATION
	, _Auth(&ANONYMOUS_SESSIONS)
	, _AuthAcc(AUTH_ANY)
	, _Realm(DEFAULT_REALM)
	, _Secret(system_get_chip_id(), 16)
	, _DAuthRecs(nullptr)
	, _ACLs(nullptr)
#endif
	//, _catchAllHandler()
{
	_server.onClient([](void* arg, AsyncClient* c){
		((AsyncWebServer*)arg)->_handleClient(c);
	}, this);
#ifdef HANDLE_AUTHENTICATION
	HTTPACL ACL("/");
	ACL.METHODS = HTTP_BASIC_READ;
	ACL.IDENTS.append(&IdentityProvider::ANONYMOUS);
	_ACLs.append(std::move(ACL));
#endif
}

AsyncWebServer::~AsyncWebServer(void) {
	delete _catchAllHandler;
}

#ifdef HANDLE_AUTHENTICATION
void AsyncWebServer::configAuthority(SessionAuthority &Auth, Stream &ACLStream) {
	_Auth = &Auth;
	loadACL(ACLStream);
}

void AsyncWebServer::configRealm(String const &realm, String const &secret,
	WebAuthTypeComposite authAccept) {
	_Realm = realm;
	if (secret) _Secret = secret;
	_AuthAcc = authAccept;
}
#endif

WebRequestMethod AsyncWebServer::parseMethod(char const *Str) {
	if (strcmp(Str, "GET") == 0) {
		return HTTP_GET;
	} else if (strcmp(Str, "PUT") == 0) {
		return HTTP_PUT;
	} else if (strcmp(Str, "POST") == 0) {
		return HTTP_POST;
	} else if (strcmp(Str, "HEAD") == 0) {
		return HTTP_HEAD;
	} else if (strcmp(Str, "DELETE") == 0) {
		return HTTP_DELETE;
	} else if (strcmp(Str, "PATCH") == 0) {
		return HTTP_PATCH;
	} else if (strcmp(Str, "OPTIONS") == 0) {
		return HTTP_OPTIONS;
#ifdef HANDLE_WEBDAV
	} else if (strcmp(Str, "COPY") == 0) {
		return HTTP_COPY;
	} else if (strcmp(Str, "MOVE") == 0) {
		return HTTP_MOVE;
	} else if (strcmp(Str, "MKCOL") == 0) {
		return HTTP_MKCOL;
	} else if (strcmp(Str, "LOCK") == 0) {
		return HTTP_LOCK;
	} else if (strcmp(Str, "UNLOCK") == 0) {
		return HTTP_UNLOCK;
	} else if (strcmp(Str, "PROPFIND") == 0) {
		return HTTP_PROPFIND;
	} else if (strcmp(Str, "PROPPATCH") == 0) {
		return HTTP_PROPPATCH;
#endif
	}
	return HTTP_UNKNOWN;
}

WebRequestMethodComposite AsyncWebServer::parseMethods(char *Str) {
	WebRequestMethodComposite Ret = 0;
	while (Str) {
		char const* Ptr = Str;
		while (*Str && *Str !=',') Str++;
		if (*Str) *Str++ = '\0';
		else Str = nullptr;

		if (Ptr[0] == '$') {
			WebRequestMethodComposite Group = 0;
			if (!Ptr[1]) {
				// Short-hand for standard methods
				Group = HTTP_STANDARD;
			} else if (Ptr[1] == 'B') {
				if (!Ptr[2]) Group = HTTP_BASIC;
				else if (Ptr[2] == 'R' && !Ptr[3]) Group = HTTP_BASIC_READ;
				else if (Ptr[2] == 'W' && !Ptr[3]) Group = HTTP_BASIC_WRITE;
			} else if (Ptr[1] == 'S') {
				if (!Ptr[2]) Group = HTTP_STANDARD;
				else if (Ptr[2] == 'R' && !Ptr[3]) Group = HTTP_STANDARD_READ;
				else if (Ptr[2] == 'W' && !Ptr[3]) Group = HTTP_STANDARD_WRITE;
			} else if (Ptr[1] == 'A') {
				if (!Ptr[2]) Group = HTTP_ANY;
				else if (Ptr[2] == 'R' && !Ptr[3]) Group = HTTP_ANY_READ;
				else if (Ptr[2] == 'W' && !Ptr[3]) Group = HTTP_ANY_WRITE;
#ifdef HANDLE_WEBDAV
			} else if (Ptr[1] == 'D') {
				if (!Ptr[2]) Group = HTTP_DAVEXT;
				else if (Ptr[2] == 'R' && !Ptr[3]) Group = HTTP_DAVEXT_READ;
				else if (Ptr[2] == 'W' && !Ptr[3]) Group = HTTP_DAVEXT_WRITE;
#endif
			}
			Ret |= Group;
		} else
			Ret |= parseMethod(Ptr);
	}
	return Ret;
}

const char* AsyncWebServer::mapMethod(WebRequestMethod method) {
	switch (method) {
		case HTTP_NONE: return "<Unspecified>";
		case HTTP_GET: return "GET";
		case HTTP_PUT: return "PUT";
		case HTTP_POST: return "POST";
		case HTTP_HEAD: return "HEAD";
		case HTTP_DELETE: return "DELETE";
		case HTTP_PATCH: return "PATCH";
		case HTTP_OPTIONS: return "OPTIONS";
#ifdef HANDLE_WEBDAV
		case HTTP_COPY: return "COPY";
		case HTTP_MOVE: return "MOVE";
		case HTTP_MKCOL: return "MKCOL";
		case HTTP_LOCK: return "LOCK";
		case HTTP_UNLOCK: return "UNLOCK";
		case HTTP_PROPFIND: return "PROPFIND";
		case HTTP_PROPPATCH: return "PROPPATCH";
#endif
		case HTTP_UNKNOWN: return "UNKNOWN";
		default: return "(?Composite?)";
	}
}

String AsyncWebServer::mapMethods(WebRequestMethodComposite methods) {
	String Ret;
	WebRequestMethod pivot = (WebRequestMethod)1;
	while (methods) {
		if (methods && pivot) {
			if (Ret) Ret.concat(',');
			Ret.concat(mapMethod(pivot));
			methods&= ~pivot;
		}
		pivot = (WebRequestMethod)(pivot << 1);
	}
	return Ret;
}

#ifdef HANDLE_AUTHENTICATION
void AsyncWebServer::loadACL(Stream &source) {
	_ACLs.clear();
	while (source.available()) {
		String Line = source.readStringUntil('\n');
		Line.trim();
		if (!Line) continue;

		char const* Ptr = Line.begin();
		if (Ptr[0] == ':') {
			ESPWS_DEBUG("ACL comment: %s\n", Ptr+1);
			continue;
		}
		HTTPACL ACL(getQuotedToken(Ptr, ':'));
		ACL.METHODS = parseMethods(getQuotedToken(Ptr, ':').begin());
		ACL.IDENTS = _Auth->IDP->parseIdentities(Ptr);
		if (!ACL.METHODS) {
			ESPWS_DEBUG("WARNING: Ineffective ACL on '%s' with no method specified\n",
				ACL.PATH.c_str());
			continue;
		}
		if (!ACL.IDENTS.length()) {
			if (!*Ptr) ESPWS_DEBUG("WARNING: Blocking ACL on '%s'", ACL.PATH.c_str());
			ESPWS_DEBUG("WARNING: Blocking ACL on '%s' due to unrecognised identities '%s'\n",
				ACL.PATH.c_str(), Ptr);
		}
		size_t xcACLs = _ACLs.count_if([&](HTTPACL const &r) {
			return r.PATH.equals(ACL.PATH) && (r.METHODS == ACL.METHODS);
		});
		if (xcACLs) ESPWS_DEBUG("WARNING: ACL on '%s' completely overrides %d earlier ones\n",
			ACL.PATH.c_str(), xcACLs);
		size_t xpACLs = _ACLs.count_if([&](HTTPACL const &r) {
			return r.PATH.equals(ACL.PATH) && (r.METHODS & ACL.METHODS);
		}) - xcACLs;
		if (xpACLs) ESPWS_DEBUG("WARNING: ACL on '%s' partially overrides %d earlier ones\n",
			ACL.PATH.c_str(), xpACLs);
		if (ACL.PATH.end()[-1] == '/') {
			size_t xsACLs = _ACLs.count_if([&](HTTPACL const &r) {
				return r.PATH.startsWith(ACL.PATH);
			});
			if (xsACLs) ESPWS_DEBUG("WARNING: ACL on '%s' shadows %d earlier ones\n",
				ACL.PATH.c_str(), xsACLs);
		}
		_ACLs.prepend(std::move(ACL));
	}
	ESPWS_DEBUG("* ACL contains %d rules\n", _ACLs.length());
}
#endif

void AsyncWebServer::_handleClient(AsyncClient* c) {
	if(c == nullptr) return;
	AsyncWebRequest *r = new AsyncWebRequest(*this, *c);
	if(r == nullptr) delete c;
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

void AsyncWebServer::beginSecure(const char *cert, const char *private_key_file,
	const char *password){
#if ASYNC_TCP_SSL_AXTLS
	_server.onSslFileRequest([](void* arg, const char *filename, uint8_t **buf){
		return ((AsyncWebServer*)arg)->_loadSslCert(filename, buf);
	}, this);
	_server.beginSecure(cert, private_key_file, password);
#endif
#if ASYNC_TCP_SSL_BEARSSL
	// Not implemented!
	panic();
#endif
}
#endif

AsyncCallbackWebHandler& AsyncWebServer::on(const char* uri, WebRequestMethodComposite method,
	ArRequestHandlerFunction const& onRequest){
	AsyncCallbackWebHandler* handler = new AsyncPathURICallbackWebHandler(uri, method);
	handler->onRequest = onRequest;
	return addHandler(handler), *handler;
}

#ifdef HANDLE_REQUEST_CONTENT
AsyncCallbackWebHandler& AsyncWebServer::on(const char* uri, WebRequestMethodComposite method,
	ArRequestHandlerFunction const& onRequest,
	ArBodyHandlerFunction const& onBody){
	AsyncCallbackWebHandler& handler = on(uri,method,onRequest);
	handler.onBody = onBody;
	return handler;
}

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
AsyncCallbackWebHandler& AsyncWebServer::on(const char* uri, WebRequestMethodComposite method,
	ArRequestHandlerFunction const& onRequest,
	ArBodyHandlerFunction const& onBody,
	ArParamDataHandlerFunction const& onParamData){
	AsyncCallbackWebHandler& handler = on(uri,method,onRequest,onBody);
	handler.onParamData = onParamData;
	return handler;
}
#endif

#endif

AsyncStaticWebHandler& AsyncWebServer::serveStatic(const char* uri, Dir const& dir,
	const char* indexFile, const char* cache_control
#ifdef ADVANCED_STATIC_WEBHANDLER
	, bool write_support
#endif
	){
	AsyncStaticWebHandler* handler = new AsyncStaticWebHandler(uri, dir, cache_control
#ifdef ADVANCED_STATIC_WEBHANDLER
		, write_support
#endif
	);
	if (indexFile) handler->setGETIndexFile(indexFile);
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
	else request._handler = _catchAllHandler;
}

void AsyncWebServer::catchAll(ArRequestHandlerFunction const& onRequest)
{ _catchAllHandler->onRequest = onRequest; }

#ifdef HANDLE_REQUEST_CONTENT
void AsyncWebServer::catchAll(ArBodyHandlerFunction const& onBody)
{ _catchAllHandler->onBody = onBody; }

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
void AsyncWebServer::catchAll(ArParamDataHandlerFunction const& onParamData)
{ _catchAllHandler->onParamData = onParamData; }
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
void AsyncWebServer::catchAll(ArUploadDataHandlerFunction const& onUploadData)
{ _catchAllHandler->onUploadData = onUploadData; }
#endif

#endif

void AsyncWebServer::reset(void) {
	//remove all writers and handlers, including catch-all handlers
	_catchAllHandler->onRequest = nullptr;
#ifdef HANDLE_REQUEST_CONTENT
	_catchAllHandler->onBody = nullptr;

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
	_catchAllHandler->onParamData = nullptr;
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
	_catchAllHandler->onUploadData = nullptr;
#endif

#endif
}

#ifdef HANDLE_AUTHENTICATION
#ifdef AUTHENTICATION_DISABLE_BASIC
WebAuthTypeComposite const AUTH_REQUIRE = AUTH_DIGEST;
#else
WebAuthTypeComposite const AUTH_REQUIRE = AUTH_BASIC|AUTH_DIGEST;
#endif
WebAuthTypeComposite const AUTH_SECURE = AUTH_DIGEST;
WebAuthTypeComposite const AUTH_ANY = AUTH_REQUIRE|AUTH_NONE;

ESPWS_DEBUGDO(const char* AsyncWebAuth::_stateToString(void) const {
	switch (State) {
		case AUTHHEADER_ANONYMOUS: return "Anonymous";
		case AUTHHEADER_MALFORMED: return "Malformed";
		case AUTHHEADER_NORECORD: return "No Record";
		case AUTHHEADER_EXPIRED: return "Expired";
		case AUTHHEADER_PREAUTH: return "Pre-authorization";
		default: return "???";
	}
})

ESPWS_DEBUGDO(const char* AsyncWebAuth::_typeToString(void) const {
	switch (Type) {
		case AUTH_NONE: return "None";
		case AUTH_BASIC: return "Basic";
		case AUTH_DIGEST: return "Digest";
		case AUTH_OTHER: return "Other";
		default: return "???";
	}
})

String calcNonce(String const &IP, time_t TS, String const &Secret) {
	String NonceSrc;
	NonceSrc.concat(IP);
	NonceSrc.concat(':');
	NonceSrc.concat(TS,16);
	NonceSrc.concat(':');
	NonceSrc.concat(Secret);

	String Ret(' ',32);
	textMD5_LC((uint8_t*)NonceSrc.begin(),NonceSrc.length(),Ret.begin());
	return Ret;
}

void AsyncWebServer::_authMaintenance(void) const {
	time_t CurTS = time(nullptr);
	// Cleanup stale nonce records
	auto DAuthRecs = const_cast<LinkedList<NONCEREC>*>(&_DAuthRecs);
	while (DAuthRecs->remove_if([&](NONCEREC const&r){
		return r.EXPIRY+DEFAULT_NONCE_RENEWAL < CurTS;
	}));
}

AsyncWebAuth AsyncWebServer::_parseAuthHeader(String &authHeader,
	AsyncWebRequest const &request) const {
	AsyncWebAuth Ret(AUTHHEADER_ANONYMOUS, AUTH_NONE);
	while (authHeader) {
		Ret.State = AUTHHEADER_MALFORMED;
		int indexAttr = authHeader.indexOf(' ');
		if (indexAttr <= 0) {
			ESPWS_DEBUG("[%s] WARNING: Missing authorization type separator in '%s'\n",
				request._remoteIdent.c_str(), authHeader.c_str());
			break;
		}
		authHeader[indexAttr++] = '\0';

		String Type = &authHeader[0];
		if (Type.equalsIgnoreCase("Basic")) {
			Ret.Type = AUTH_BASIC;
#ifdef AUTHENTICATION_DISABLE_BASIC
			ESPWS_DEBUG("[%s] WARNING: %s authorization has been disabled!\n",
				request._remoteIdent.c_str(), Type.c_str());
			Ret.State = AUTHHEADER_UNACCEPT;
			break;
#else
			ESPWS_DEBUGVV("[%s] %s Authorization:\n",
				request._remoteIdent.c_str(), Ret._typeToString());
			// Base64(username:password)
			size_t srcLen = authHeader.length()-indexAttr;
			String Decoded(' ', base64_decode_expected_len(srcLen));
			size_t decLen = base64_decode_chars(&authHeader[indexAttr], srcLen, Decoded.begin());
			if (decLen != srcLen) {
				ESPWS_DEBUG("[%s] WARNING: Base64 decoding failed with %d trailing bytes '%s'\n",
					request._remoteIdent.c_str(), srcLen-decLen, &authHeader[indexAttr+decLen]);
#ifdef STRICT_PROTOCOL
				ESPWS_DEBUGV("[%s] Partial decode result: '%s'\n", request._remoteIdent.c_str(), Decoded.c_str());
				break;
#endif
			}
			int indexSecret = Decoded.indexOf(':');
			if (indexSecret <= 0) {
				ESPWS_DEBUG("[%s] WARNING: Missing password field separator in '%s'\n",
					request._remoteIdent.c_str(), Decoded.c_str());
#ifdef STRICT_PROTOCOL
				ESPWS_DEBUGV("[%s] Auth Token: '%s'\n", request._remoteIdent.c_str(), Decoded.c_str());
				break;
#endif
			} else {
				Ret.Secret = &Decoded[indexSecret+1];
				Decoded.remove(indexSecret);
			}
			Ret.UserName = std::move(Decoded);
			ESPWS_DEBUGVV("[%s] -> Username = '%s'\n",
				request._remoteIdent.c_str(), Ret.UserName.c_str());
			ESPWS_DEBUGVV("[%s] -> Password = '%s'\n",
				request._remoteIdent.c_str(), Ret.Secret.c_str());
#endif
		} else if (Type.equalsIgnoreCase("Digest")) {
			Ret.Type = AUTH_DIGEST;
			ESPWS_DEBUGVV("[%s] %s Authorization:\n",
				request._remoteIdent.c_str(), Ret._typeToString());
			char const *valStart, *valEnd;
			// username=...,
			{
				int indexUName = authHeader.indexOf("username=",indexAttr);
				if (indexUName < 0) {
					ESPWS_DEBUG("[%s] WARNING: Missing username field in '%s'\n",
						request._remoteIdent.c_str(), &authHeader[indexAttr]);
					break;
				}
				valStart = valEnd = &authHeader[indexUName+9];
				Ret.UserName = getQuotedToken(valEnd,',');
				ESPWS_DEBUGVV("[%s] -> Username = '%s'\n",
					request._remoteIdent.c_str(), Ret.UserName.c_str());
			}
			// response=...,
			{
				int indexResp = authHeader.indexOf("response=",indexAttr);
				if (indexResp < 0) {
					ESPWS_DEBUG("[%s] WARNING: Missing response field in '%s'\n",
						request._remoteIdent.c_str(), &authHeader[indexAttr]);
					break;
				}
				valStart = valEnd = &authHeader[indexResp+9];
				String Response = getQuotedToken(valEnd,',');
				ESPWS_DEBUGVV("[%s] -> Response = '%s'\n",
					request._remoteIdent.c_str(), Response.c_str());
				Ret.Secret.concat(valStart,valEnd-valStart-1);
			}
			// realm=...,
			{
				int indexRealm = authHeader.indexOf("realm=",indexAttr);
				if (indexRealm < 0) {
					ESPWS_DEBUG("[%s] WARNING: Missing realm field in '%s'\n",
						request._remoteIdent.c_str(), &authHeader[indexAttr]);
					break;
				}
				valStart = valEnd = &authHeader[indexRealm+6];
				String Realm = getQuotedToken(valEnd,',');
				ESPWS_DEBUGVV("[%s] -> Realm = '%s'\n",
					request._remoteIdent.c_str(), Realm.c_str());
				if (!Realm.equals(_Realm)) {
					ESPWS_DEBUG("[%s] WARNING: Authorization realm '%s' mismatch, expect '%s'\n",
						request._remoteIdent.c_str(), Realm.c_str(), _Realm.c_str());
					Ret.State = AUTHHEADER_NORECORD;
					break;
				}
				Ret.Secret.concat(';');
				Ret.Secret.concat(valStart,valEnd-valStart-1);
			}
			// nonce=...,
			NONCEREC* NRec;
			{
				int indexNonce = authHeader.indexOf("nonce=",indexAttr);
				if (indexNonce < 0) {
					ESPWS_DEBUG("[%s] WARNING: Missing nonce field in '%s'\n",
						request._remoteIdent.c_str(), &authHeader[indexAttr]);
					break;
				}
				valStart = valEnd = &authHeader[indexNonce+6];
				String Nonce = getQuotedToken(valEnd,',');
				ESPWS_DEBUGVV("[%s] -> Nonce = '%s'\n",
					request._remoteIdent.c_str(), Nonce.c_str());
				// Lookup alive records
				auto DAuthRecs = const_cast<LinkedList<NONCEREC>*>(&_DAuthRecs);
				NRec = DAuthRecs->get_if([&](NONCEREC const&r){ return r.NONCE.equals(Nonce); });
				if (!NRec) {
					ESPWS_DEBUGV("[%s] WARNING: No record found with given nonce '%s'\n",
						request._remoteIdent.c_str(), Nonce.c_str());
					Ret.State = AUTHHEADER_NORECORD;
					break;
				}
				Ret.NRec = NRec;
				// Check record expiration
				time_t CurTS = time(nullptr);
				if (NRec->EXPIRY < CurTS) {
					ESPWS_DEBUGV("[%s] WARNING: Expired record with given nonce '%s'\n",
						request._remoteIdent.c_str(), Nonce.c_str());
					Ret.State = AUTHHEADER_EXPIRED;
					break;
				}
				// Validate nonce
				String ValidNonce = calcNonce(request._client.remoteIP().toString(),
					NRec->EXPIRY, _Secret);
				if (!Nonce.equals(ValidNonce)) {
					ESPWS_DEBUG("[%s] WARNING: Unmatched nonce '%s', expect '%s'\n",
						request._remoteIdent.c_str(), Nonce.c_str(), ValidNonce.c_str());
					Ret.State = AUTHHEADER_UNACCEPT;
					break;
				}
				Ret.Secret.concat(';');
				Ret.Secret.concat(valStart,valEnd-valStart-1);
			}
			// qop=...,
			int QoPLevel = 0;
			{
				int indexQoP = authHeader.indexOf("qop=",indexAttr);
				if (indexQoP >= 0) {
					valStart = valEnd = &authHeader[indexQoP+4];
					String QoP = getQuotedToken(valEnd,',');
					ESPWS_DEBUGVV("[%s] -> QoP = '%s'\n",
						request._remoteIdent.c_str(), QoP.c_str());
					if (QoP.equals("auth")) QoPLevel = 1;
					else if (QoP.equals("auth-int")) QoPLevel = 2;
					else {
						ESPWS_DEBUG("[%s] WARNING: Unrecognised QoP specifier '%s'\n",
							request._remoteIdent.c_str(), QoP.c_str());
						break;
					}
					Ret.Secret.concat(';');
					Ret.Secret.concat(valStart,valEnd-valStart-1);
				} else {
					ESPWS_DEBUGVV("[%s] -> QoP X\n", request._remoteIdent.c_str());
					Ret.Secret.concat(';');
				}
			}
			// cnonce=...,
			{
				int indexCNonce = authHeader.indexOf("cnonce=",indexAttr);
				if (indexCNonce >= 0) {
					valStart = valEnd = &authHeader[indexCNonce+7];
					String CNonce = getQuotedToken(valEnd,',');
					ESPWS_DEBUGVV("[%s] -> CNonce = '%s'\n",
						request._remoteIdent.c_str(), CNonce.c_str());
					Ret.Secret.concat(';');
					Ret.Secret.concat(valStart,valEnd-valStart-1);
				} else {
					if (QoPLevel > 0) {
						ESPWS_DEBUG("[%s] WARNING: Missing cnonce field in '%s'\n",
							request._remoteIdent.c_str(), &authHeader[indexAttr]);
						break;
					} else {
						ESPWS_DEBUGVV("[%s] -> CNonce X\n", request._remoteIdent.c_str());
						Ret.Secret.concat(';');
					}
				}
			}
			// nc=...,
			{
				int indexNC = authHeader.indexOf("nc=",indexAttr);
				if (indexNC >= 0) {
					valStart = valEnd = &authHeader[indexNC+3];
					String NC = getQuotedToken(valEnd,',');
					ESPWS_DEBUGVV("[%s] -> NonceCount = '%s'\n",
						request._remoteIdent.c_str(), NC.c_str());
#ifdef STRICT_PROTOCOL
					if (NC.length() != 8) {
						ESPWS_DEBUG("[%s] WARNING: Invalid nonce-count field '%s'\n",
							request._remoteIdent.c_str(), NC.c_str());
						break;
					}
#endif
					if (NC <= NRec->NC) {
						ESPWS_DEBUG("[%s] WARNING: Detected nonce-count reversal, '%s' <= '%s'\n",
							request._remoteIdent.c_str(), NC.c_str(), NRec->NC.c_str());
						Ret.State = AUTHHEADER_UNACCEPT;
						break;
					}
					NRec->NC = std::move(NC);
					Ret.Secret.concat(';');
					Ret.Secret.concat(valStart,valEnd-valStart-1);
				} else {
					if (QoPLevel > 0) {
						ESPWS_DEBUG("[%s] WARNING: Missing nonce-count field in '%s'\n",
							request._remoteIdent.c_str(), &authHeader[indexAttr]);
						break;
					} else {
						ESPWS_DEBUGVV("[%s] -> NonceCount X\n", request._remoteIdent.c_str());
						Ret.Secret.concat(';');
					}
				}
			}
			putQuotedToken(request.methodToString(), Ret.Secret, ';');
			// uri=...,
			{
				int indexURI = authHeader.indexOf("uri=",indexAttr);
				if (indexURI < 0) {
					ESPWS_DEBUG("[%s] WARNING: Missing uri field in '%s'\n",
						request._remoteIdent.c_str(), &authHeader[indexAttr]);
					break;
				}
				valStart = valEnd = &authHeader[indexURI+4];
				String URI = getQuotedToken(valEnd,',');
				ESPWS_DEBUGVV("[%s] -> URI = '%s'\n", request._remoteIdent.c_str(), URI.c_str());
				String reqUri = request.oUrl()+request.oQuery();
				if (!URI.equals(reqUri)) {
					ESPWS_DEBUG("[%s] WARNING: Authorizing against URI '%s', expect '%s'\n",
						request._remoteIdent.c_str(), URI.c_str(), reqUri.c_str());
					Ret.State = AUTHHEADER_UNACCEPT;
					break;
				}
				Ret.Secret.concat(';');
				Ret.Secret.concat(valStart,valEnd-valStart-1);
			}
		} else {
			Ret.Type = AUTH_OTHER;
			Ret.Secret = &authHeader[indexAttr];
		}
		Ret.State = AUTHHEADER_PREAUTH;
		break;
	}

	if ((Ret.State == AUTHHEADER_PREAUTH) && (Ret.Type & _AuthAcc == 0))
		Ret.State = AUTHHEADER_UNACCEPT;
	return Ret;
}

WebAuthSession* AsyncWebServer::_authSession(AsyncWebAuth &authInfo, AsyncWebRequest const &request) const {
	switch (authInfo.Type) {
		case AUTH_NONE:
			ESPWS_DEBUGVV("[%s] Authorizing anonymous session...\n",
				request._remoteIdent.c_str());
			return new WebAuthSession(_Auth->getSession(
				Credential(IdentityProvider::ANONYMOUS, EA_SECRET_NONE, String())), authInfo);

		case AUTH_BASIC:
			ESPWS_DEBUGVV("[%s] Authorizing basic session...\n",
				request._remoteIdent.c_str());
			return new WebAuthSession(_Auth->getSession(authInfo.UserName,
				EA_SECRET_PLAINTEXT, std::move(authInfo.Secret)), authInfo);

		case AUTH_DIGEST:
			ESPWS_DEBUGVV("[%s] Authorizing digest session...\n",
				request._remoteIdent.c_str());
			return new WebAuthSession(_Auth->getSession(authInfo.UserName,
#ifdef AUTHENTICATION_ENABLE_SESS
				EA_SECRET_HTTPDIGESTAUTH_MD5SESS,
#else
				EA_SECRET_HTTPDIGESTAUTH_MD5,
#endif
				std::move(authInfo.Secret)), authInfo);

		default:
			ESPWS_DEBUG("[%s] ERROR: Unrecognised authorization type '%s'\n",
				request._remoteIdent.c_str(), authInfo._typeToString());
	}
	return nullptr;
}

void AsyncWebServer::_genAuthHeader(AsyncWebResponse &response, AsyncWebRequest const &request,
	bool renew, NONCEREC const *NRec) const {
	if (_AuthAcc & AUTH_REQUIRE != 0) {
		if (_AuthAcc & AUTH_BASIC) {
#ifndef AUTHENTICATION_DISABLE_BASIC
			String Message(F("Basic realm="));
			putQuotedToken(_Realm, Message, ',', false, true);
			response.addHeader(F("WWW-Authenticate"), Message);
#endif
		}

		if (_AuthAcc & AUTH_DIGEST) {
			time_t ExpTS = time(nullptr) + DEFAULT_NONCE_LIFE;
			String Message(F("Digest realm="));
			putQuotedToken(_Realm, Message, ',', false, true);
			Message.concat(F(",qop="));
			putQuotedToken("auth", Message, ',', false, true);
			Message.concat(F(",nonce="));
			if (NRec) {
				putQuotedToken(NRec->NONCE, Message, ',', false, true);
			} else {
				String NewNonce = calcNonce(request._client.remoteIP().toString(), ExpTS, _Secret);
				putQuotedToken(NewNonce, Message, ',', false, true);
				auto DAuthRecs = const_cast<LinkedList<NONCEREC>*>(&_DAuthRecs);
				if (DAuthRecs->append(NONCEREC(std::move(NewNonce), ExpTS)) > DEFAULT_NONCE_MAXIMUM) {
					ESPWS_DEBUG("[%s] WARNING: Nonce buffer overflow, retiring oldest nonce...\n",
						request._remoteIdent.c_str());
					DAuthRecs->remove_nth(0);
				}
			}
#ifdef AUTHENTICATION_ENABLE_SESS
			Message.concat(F(",algorithm=MD5-sess"));
#endif
			if (renew) Message.concat(F(",stale=true"));
			response.addHeader(F("WWW-Authenticate"), Message);
		}
	} else {
		// No known authentication required
		// If we reach here, no further action would make differences
		response.setCode(403);
	}
}

WebACLMatchResult AsyncWebServer::_checkACL(AsyncWebRequest const &request, AuthSession* session) const {
	HTTPACL* eACL = _ACLs.get_if([&](HTTPACL const &r) {
		if (r.METHODS & request.method() == 0) return false;
		return (r.PATH.end()[-1] == '/')? request.url().startsWith(r.PATH)
			: request.url().equals(r.PATH);
	});
	if (!eACL) return ACL_NOTFOUND;
	return eACL->IDENTS.get_if([&](Identity * const &r){
			return *r == session->IDENT;
		})? ACL_ALLOWED : ACL_NOTALLOWED;
}

#endif
