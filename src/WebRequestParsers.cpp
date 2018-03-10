/*
	Asynchronous WebServer library for Espressif MCUs

	Copyright (c) 2017 Zhenyu Wu <Adam_5Wu@hotmail.com>

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

#include "WebRequestParsers.h"

extern "C" {
	#include <errno.h>
	#include "lwip/opt.h"
}

ESPWS_DEBUGDO(PGM_P AsyncRequestHeadParser::_stateToString(void) const {
	switch (_state) {
		case H_PARSER_ACCU: return PSTR_C("Accumulating");
		case H_PARSER_LINE: return PSTR_C("HandleLine");
		default: return PSTR_C("???");
	}
})

bool AsyncRequestHeadParser::_parseLine(void) {
	switch (__reqState()) {
		case REQUEST_START:
			if(_temp && _parseReqStart()) {
				// Perform request rewrite now
				_request._server._rewriteRequest(_request);
				// Defer handler lookup until host is known
				//_request._server._attachHandler(_request);

				__reqState(REQUEST_HEADERS);
			} else {
				__reqState(REQUEST_ERROR);
				return false;
			}
			break;

		case REQUEST_HEADERS:
			if(_temp) {
				// More headers
				if (!_parseReqHeader()) {
					if (__reqState() == REQUEST_HEADERS)
						__reqState(REQUEST_ERROR);
					return false;
				}
				break;
			}

			// If any non-standard header has been encountered,
			//   a handler should have been already attached;
			// Otherwise, we attach handler now
			if (!_handlerAttached) {
				_handlerAttached = true;
				_request._server._attachHandler(_request);
			}
			// End of headers
			if(!__reqHandler()) {
				// No handler available
				_request.send(501);
				__reqState(REQUEST_RESPONSE);
			} else {
#ifdef STRICT_PROTOCOL
				// According to RFC, HTTP/1.1 requires host header
				if (_request.version() && !_request.host()) {
					_request.send(400);
					__reqState(REQUEST_RESPONSE);
				} else
#endif
				{
#ifdef HANDLE_AUTHENTICATION
					// Handle authentication
					auto _session = _handleAuth();
					if (_session) {
						ESPWS_DEBUGV("[%s] Session %s\n",
							_request._remoteIdent.c_str(), _session->toString().c_str());
						if (!_session->isAuthorized()) {
							ESPWS_DEBUGV("[%s] Retry authentication\n", _request._remoteIdent.c_str());
							_requestAuth(false, _session->NRec);
							if (_session->NRec) {
								// Google Chrome seems to disregard NC when authentication was unsuccessful
								_session->NRec->NC = 0;
							}
							delete _session;
							return false;
						} else {
							switch (__setSession(_session)) {
								case ACL_ALLOWED: break;
								case ACL_NOTALLOWED: {
									ESPWS_DEBUGV("[%s] Decline access by ACL\n", _request._remoteIdent.c_str());
									_rejectAuth(_session);
									delete _session;
									return false;
								} break;
								default: {
									ESPWS_DEBUGV("[%s] Decline access due to lack of ACL\n",
										_request._remoteIdent.c_str());
									delete _session;
									_session = nullptr;
								}
							}
						}
					} else {
						ESPWS_DEBUGV("[%s] No session\n", _request._remoteIdent.c_str());
					}
					if (!_session) {
						if (__reqState() == REQUEST_HEADERS)
							_rejectAuth(nullptr);
						return false;
					}
#endif
					// Check if we can continue
					if (__reqHandler()->_checkContinue(_request, _expectingContinue)) {
#ifdef HANDLE_REQUEST_CONTENT
						size_t bodyLength = _request.contentLength();
						if (bodyLength != -1 && bodyLength) {
							// Switch parser
							AsyncWebParser* newParser = nullptr;
							for (auto& item : BodyParserRegistry) {
								if (newParser = item(_request)) break;
							}
							ESPWS_DEBUGVVDO({
								if (newParser) {
									ESPWS_LOG("[%s] Using registered body parser\n",
										_request._remoteIdent.c_str());
								} else {
									ESPWS_LOG("[%s] Using generic body parser\n",
										_request._remoteIdent.c_str());
								}
							});
							__reqParser(newParser? newParser
								: new AsyncRequestPassthroughContentParser(_request));
							__reqState(REQUEST_BODY);
						} else
#endif
						{
							__reqState(REQUEST_RECEIVED);
							__reqParser(nullptr);
						}
						// We are done!
						delete this;
					} else __reqState(REQUEST_RESPONSE);
				}
			}
			return false;

		default:
			ESPWS_DEBUG("[%s] Unexpected request status [%s]",
				_request._remoteIdent.c_str(), SFPSTR(__strState()));
			__reqState(REQUEST_ERROR);
			return false;
	}
	return true;
}

bool AsyncRequestHeadParser::_parseReqStart(void) {
	ESPWS_DEBUGVV("[%s] > %s\n", _request._remoteIdent.c_str(), _temp.c_str());
	// Split the head into method, url and version
	int indexUrl = _temp.indexOf(' ');
	if (indexUrl <= 0) return false;
	_temp[indexUrl] = '\0';

	int indexVer = _temp.indexOf(' ', indexUrl+1);
	if (indexVer <= 0) return false;
	_temp[indexVer] = '\0';

	__setMethod(AsyncWebServer::parseMethod(_temp.begin()));
	__setVersion(memcmp(&_temp[indexVer+1], "HTTP/1.0", 8)? 1 : 0);
	__setUrl(&_temp[indexUrl+1]);

	// Per RFC, HTTP 1.1 connections are persistent by default
	if (_request.version()) __setKeepAlive(true);

	ESPWS_DEBUGV("[%s] HTTP/1.%d %s %s\n", _request._remoteIdent.c_str(),
		_request.version(), SFPSTR(_request.methodToString()), _request.url().c_str());
	return true;
}

bool AsyncRequestHeadParser::_parseReqHeader(void) {
	ESPWS_DEBUGVV("[%s] > %s\n", _request._remoteIdent.c_str(), _temp.c_str());

	// Split the header into key and value
	int keyEnd = _temp.indexOf(':');
	if (keyEnd <= 0) return false;
	int indexValue = keyEnd;
	while (_temp[++indexValue] == ' ');
	String value = _temp.substring(indexValue);
	_temp.remove(keyEnd);

	if (_temp.equalsIgnoreCase(FC("Host"))) {
		__setHost(value);
		ESPWS_DEBUGV("[%s] + Host: '%s'\n",
			_request._remoteIdent.c_str(), _request.host().c_str());
	} else if (_temp.equalsIgnoreCase(FC("Accept"))) {
		__setAccept(value);
		ESPWS_DEBUGV("[%s] + Accept: '%s'\n",
		_request._remoteIdent.c_str(), _request.accept().c_str());
	} else if (_temp.equalsIgnoreCase(FC("Accept-Encoding"))) {
		__setAcceptEncoding(value);
		ESPWS_DEBUGV("[%s] + Accept-Encoding: '%s'\n",
		_request._remoteIdent.c_str(), _request.acceptEncoding().c_str());
	} else if (_temp.equalsIgnoreCase(FC("Accept-Language"))) {
#ifdef REQUEST_ACCEPTLANG
		__setAcceptLanguage(value);
		ESPWS_DEBUGV("[%s] + Accept-Language: '%s'\n",
			_request._remoteIdent.c_str(), _request.acceptLanguage().c_str());
#else
		ESPWS_DEBUGV("[%s] - Accept-Language: '%s'\n",
			_request._remoteIdent.c_str(), value.c_str());
#endif
	} else if (_temp.equalsIgnoreCase(FC("User-Agent"))) {
#ifdef REQUEST_USERAGENT
		__setUserAgent(value);
		ESPWS_DEBUGV("[%s] + User-Agent: '%s'\n",
			_request._remoteIdent.c_str(), _request.userAgent().c_str());
#else
		ESPWS_DEBUGV("[%s] - User-Agent: '%s'\n",
			_request._remoteIdent.c_str(), value.c_str());
#endif
	} else if (_temp.equalsIgnoreCase(FC("Referer"))) {
#ifdef REQUEST_REFERER
		__setReferer(value);
		ESPWS_DEBUGV("[%s] + Referer: '%s'\n",
			_request._remoteIdent.c_str(), _request.referer().c_str());
#else
		ESPWS_DEBUGV("[%s] - Referer: '%s'\n",
			_request._remoteIdent.c_str(), value.c_str());
#endif
#ifdef HANDLE_WEBDAV
	} else if (_temp.equalsIgnoreCase(FC("Translate"))) {
#ifdef STRICT_PROTOCOL
		if (value.length() != 1 ||
			((value[0] != 't') && (value[0] == 'f') && (value[0] == 'F'))) {
			_request.send(400);
			__reqState(REQUEST_RESPONSE);
			return false;
		}
#endif
		__setTranslate((value.length() == 1) && (value[0] == 't'));
		ESPWS_DEBUGV("[%s] + Translate: %s\n",
			_request._remoteIdent.c_str(), _request.translate()? "True": "False");
#endif
	} else if (_temp.equalsIgnoreCase(FC("Connection"))) {
		ESPWS_DEBUGV("[%s] + Connection: %s\n",
			_request._remoteIdent.c_str(), value.c_str());
		if (value.equalsIgnoreCase(FC("keep-alive"))) {
			__setKeepAlive(true);
		} else if (value.equalsIgnoreCase(FC("close"))) {
			__setKeepAlive(false);
		} else {
#ifdef STRICT_PROTOCOL
			_request.send(400);
			__reqState(REQUEST_RESPONSE);
			return false;
#else
			ESPWS_DEBUG("[%s] ? Unrecognised connection header content: '%s'\n",
				_request._remoteIdent.c_str(), value.c_str());
#endif
		}
	} else if (_temp.equalsIgnoreCase(FC("Content-Type"))) {
		__setContentType(value);
		ESPWS_DEBUGV("[%s] + Content-Type: '%s'\n",
			_request._remoteIdent.c_str(), _request.contentType().c_str());
	} else if (_temp.equalsIgnoreCase(FC("Content-Length"))) {
		size_t contentLength = value.toInt();
		if (!contentLength && errno) return false;
		__setContentLength(contentLength);
		ESPWS_DEBUGV("[%s] + Content-Length: %d\n",
			_request._remoteIdent.c_str(), _request.contentLength());
	} else if (_temp.equalsIgnoreCase(FC("Expect"))) {
		ESPWS_DEBUGV("[%s] + Expect: '%s'\n", _request._remoteIdent.c_str(), value.c_str());
		if (value.equalsIgnoreCase(FC("100-continue"))) {
			_expectingContinue = true;
		} else {
#ifdef STRICT_PROTOCOL
			// According to RFC, unrecognised expect should be rejected with error
			_request.send(417);
			__reqState(REQUEST_RESPONSE);
			return false;
#else
			ESPWS_DEBUG("[%s] ? Unrecognised expect header content: '%s'\n",
				_request._remoteIdent.c_str(), value.c_str());
#endif
		}
#ifdef HANDLE_AUTHENTICATION
	} else if (_temp.equalsIgnoreCase(FC("Authorization"))) {
		_authorization = std::move(value);
		ESPWS_DEBUGV("[%s] + Authorization: '%s'\n",
			_request._remoteIdent.c_str(), _authorization.c_str());
#endif
	} else {
		if (!_handlerAttached) {
			_handlerAttached = true;
			_request._server._attachHandler(_request);
		}
		if (__reqHandler() && __reqHandler()->_isInterestingHeader(_request, _temp)) {
			ESPWS_DEBUGV("[%s] ! %s: '%s'\n",
				_request._remoteIdent.c_str(), _temp.begin(), value.begin());
			__addHeader(_temp, value);
		}
	}
	return true;
}

void AsyncRequestHeadParser::_parse(void *&buf, size_t &len) {
	char *str = (char*)buf;
	while (len) {
		// Find new line in buf
		size_t i = 0;
		while (str[i] != '\n') {
			if (++i >= len) {
				// No new line, just add the buffer in _temp
				_state = H_PARSER_ACCU;
				_temp.concat(str, len);
				len = 0;
				return;
			}
		}
		// Found new line - extract it and parse
		_temp.concat(str, i-1);
		_temp.trim();

		len-= i+1;
		buf = str+= i+1;
		_state = H_PARSER_LINE;
		if (!_parseLine()) break;
		_temp.clear();
	}
}

#ifdef HANDLE_AUTHENTICATION

WebAuthSession* AsyncRequestHeadParser::_handleAuth(void) {
	AsyncWebAuth AuthInfo = _request._server._parseAuthHeader(_authorization, _request);
	// Debug code, force all auth to anonymous
	//return new WebAuthSession(AuthSession(IdentityProvider::ANONYMOUS, nullptr), AuthInfo);
	switch (AuthInfo.State) {
		case AUTHHEADER_ANONYMOUS:
		case AUTHHEADER_PREAUTH:
			return _request._server._authSession(AuthInfo, _request);

		case AUTHHEADER_EXPIRED:
		case AUTHHEADER_NORECORD:
			_requestAuth(true);
			break;

		case AUTHHEADER_UNACCEPT:
		case AUTHHEADER_MALFORMED:
			break;

		default:
			ESPWS_DEBUG("[%s] WARNING: Unrecognised authorization header parsing state '%s'\n",
				_request._remoteIdent.c_str(), SFPSTR(AuthInfo._stateToString()));
	}
	return nullptr;
}

void AsyncRequestHeadParser::_requestAuth(bool renew, NONCEREC const *NRec) {
	AsyncWebResponse *response = _request.beginResponse(401);
	_request._server._genAuthHeader(*response, _request, renew, NRec);
	_request.send(response);
	__reqState(REQUEST_RESPONSE);
}

void AsyncRequestHeadParser::_rejectAuth(AuthSession *session) {
	if (!session || (session->IDENT != IdentityProvider::ANONYMOUS &&
		session->IDENT != IdentityProvider::UNKNOWN)) {
		_request.send(403);
		__reqState(REQUEST_RESPONSE);
	} else _requestAuth(false);
}

#endif

#ifdef HANDLE_REQUEST_CONTENT

void AsyncRequestPassthroughContentParser::_parse(void *&buf, size_t &len) {
	// Simply track the upload progress and invoke handler
	if (!__reqHandler()->_handleBody(_request, _curOfs, buf, len)) {
		ESPWS_DEBUG("[%s] Request body handling terminated abnormally\n",
			_request._remoteIdent.c_str());
		__reqState(REQUEST_ERROR);
	} else {
		_curOfs+= len;
		len = 0;

		if (_curOfs >= _request.contentLength()) {
			__reqState(REQUEST_RECEIVED);
			__reqParser(nullptr);
			// We are done!
			delete this;
		}
	}
}

#ifdef HANDLE_REQUEST_CONTENT_SIMPLEFORM

PGM_P SIMPLEFORM_MIME SPROGMEM_S = "application/x-www-form-urlencoded";

typedef enum {
	SF_PARSER_KEY,
	SF_PARSER_VALUE
} SimpleFormParserState;

class AsyncSimpleFormContentParser: public AsyncWebParser {
	protected:
		SimpleFormParserState _state;
		size_t _curOfs;
		size_t _valOfs;
		size_t _memCached;
		String _temp;
		String _key;

		bool _needFlush(void) { return _temp.length() > REQUEST_PARAM_MEMCACHE; }
		bool _memCacheFull(void) { return _memCached > REQUEST_PARAM_MEMCACHE; }

		bool _checkReachEnd(std::function<void(void)> callback) {
			if (_curOfs >= _request.contentLength()) {
				ESPWS_DEBUG_S(L,"[%s] Finished body parsing\n", _request._remoteIdent.c_str());
				if (callback) callback();
				if (__reqState() == REQUEST_BODY) {
					__reqState(REQUEST_RECEIVED);
					__reqParser(nullptr);
					// We are done!
					delete this;
				}
				return true;
			}
			return false;
		}

		ESPWS_DEBUGDO(PGM_P _stateToString(void) const override {
			switch (_state) {
				case SF_PARSER_KEY: return PSTR_L("Key");
				case SF_PARSER_VALUE: return PSTR_L("Value");
				default: return PSTR_L("???");
			}
		})

		bool _pushKeyVal(String &&value, bool _flush) {
			if (_state == SF_PARSER_VALUE) {
				bool HandlerCallback = _flush || _memCacheFull();
				if (HandlerCallback) {
					ESPWS_DEBUGVV_S(L,"[%s] * [%s]@%0.4X = '%s'\n",
						_request._remoteIdent.c_str(), _key.c_str(), _valOfs, value.c_str());
					__reqHandler()->_handleParamData(_request, _key, _valOfs,
						value.begin(), value.length());
					_valOfs+= value.length();
				} else {
					ESPWS_DEBUGVV_S(L,"[%s] + [%s] = '%s'\n",
						_request._remoteIdent.c_str(), _key.c_str(), value.c_str());
					_memCached+= _key.length();
					_memCached+= value.length();
					__addParam(_key, value);
				}
				return true;
			}

			ESPWS_DEBUG_S(L,"[%s] Invalid request parameter state '%s'\n",
				_request._remoteIdent.c_str(), SFPSTR(_stateToString()));
			__reqState(REQUEST_ERROR);
			return false;
		}

		String _decodePartial(void) {
			// ASSUMPTION: Buffer size always > 2
			// Check if the last two char in buffer contains encoder preamble
			String __temp;
			if (_temp.end()[-2] == '%') {
				__temp.concat(&_temp.end()[-2],2);
				_temp.remove(_temp.length()-2);
			} else if (_temp.end()[-1] == '%') {
				__temp.concat('%');
				_temp.remove(_temp.length()-1);
			}
			String _ret = urlDecode(_temp.begin(), _temp.length());
			_temp = std::move(__temp);
			return _ret;
		}

	public:
		AsyncSimpleFormContentParser(AsyncWebRequest &request)
		: AsyncWebParser(request), _state(SF_PARSER_KEY),
		_curOfs(0), _valOfs(0), _memCached(0) {
			// Nothing
		}

		virtual void _parse(void *&buf, size_t &len) override {
			char *str = (char*)buf;
			while (len) {
				char delim = '\0';
				size_t limit = 0;
				switch (_state) {
					case SF_PARSER_KEY:
						delim = '=';
						limit = REQUEST_PARAM_KEYMAX;
						break;
					case SF_PARSER_VALUE:
						delim = '&';
						limit = -1;
						break;
				}

				// Find param delimiter in buf
				size_t i = 0;
				while (str[i] != delim) {
					if (i >= REQUEST_PARAM_MEMCACHE/2 || ++i >= len) {
						// No delim, just add the buffer in _temp

						// Guard maximum token length
						if (_temp.length()+i > limit) {
							ESPWS_DEBUG_S(L,"[%s] Request parameter token exceeds length limit!\n",
								_request._remoteIdent.c_str());
							__reqState(REQUEST_ERROR);
							return;
						}
						_temp.concat(str, i);
						len-= i;
						buf = str+= i;
						_curOfs+= i;

						// Have we reached the end?
						if (_checkReachEnd([&]{
							_pushKeyVal(urlDecode(_temp.begin(),_temp.length()), _valOfs);
						})) return;

						// Flush buffer if it is too large
						if (_needFlush())
							_pushKeyVal(_decodePartial(), true);
						i = 0;
					}
				}

				// Found new param token - extract it and parse
				_temp.concat(str, i++);
				len-= i;
				buf = str+= i;
				_curOfs+= i;

				if (_temp) {
					String item = urlDecode(_temp.begin(),_temp.length());
					switch (_state) {
						case SF_PARSER_KEY:
							_key = std::move(item);
							_state = SF_PARSER_VALUE;
							break;
						case SF_PARSER_VALUE:
							_pushKeyVal(std::move(item), _valOfs);
							_state = SF_PARSER_KEY;
							_valOfs = 0;
							break;
					}
				}
				if (_checkReachEnd(nullptr)) return;
				_temp.clear();
			}
		}
};

#endif


#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM

PGM_P MULTIPARTFORM_MIMEPFX SPROGMEM_S = "multipart/form-data;";

typedef enum {
	MP_PARSER_STARTUP,
	MP_PARSER_BOUNDARY,
	MP_PARSER_HEADER,
	MP_PARSER_VALUE,
	MP_PARSER_CONTENT,
	MP_PARSER_TERMINATE
} MultipartFormParserState;

class AsyncRequestMultipartFormContentParser: public AsyncWebParser {
	protected:
		MultipartFormParserState _state;
		bool _filepart;
		size_t _boundaryLen;
		size_t _curOfs;
		size_t _valOfs;
		size_t _parseOfs;
		size_t _memCached;
		String _temp;
		String _boundary;
		String _key;
		String _filename;
		String _contentType;

		bool _needFlush(void) { return _temp.length() > REQUEST_PARAM_MEMCACHE; }
		bool _memCacheFull(void) { return _memCached > REQUEST_PARAM_MEMCACHE; }

		bool _checkReachEnd(std::function<void(void)> callback) {
			if (_curOfs >= _request.contentLength()) {
				ESPWS_DEBUGVV_S(L,"[%s] Finished body parsing\n", _request._remoteIdent.c_str());
				if (callback) callback();
				if (__reqState() == REQUEST_BODY) {
					__reqState(REQUEST_RECEIVED);
					__reqParser(nullptr);
					// We are done!
					delete this;
				}
				return true;
			}
			ESPWS_DEBUGVV_S(L,"[%s] Body parsed %d/%d\n",
				_request._remoteIdent.c_str(), _curOfs, _request.contentLength());
			return false;
		}

		ESPWS_DEBUGDO(PGM_P _stateToString(void) const override {
			switch (_state) {
				case MP_PARSER_STARTUP: return PSTR_L("Startup");
				case MP_PARSER_BOUNDARY: return PSTR_L("Boundary");
				case MP_PARSER_HEADER: return PSTR_L("Header");
				case MP_PARSER_VALUE: return PSTR_L("Value");
				case MP_PARSER_CONTENT: return PSTR_L("Content");
				case MP_PARSER_TERMINATE: return PSTR_L("Terminate");
				default: return PSTR_L("???");
			}
		})

		bool _pushKeyVal(String &&value, bool _flush) {
			if (_state == MP_PARSER_VALUE) {
				bool HandlerCallback = _flush || _memCacheFull();
				if (HandlerCallback) {
					ESPWS_DEBUGVV_S(L,"[%s] * [%s]@%0.4X = '%s'\n",
						_request._remoteIdent.c_str(), _key.c_str(), _valOfs, value.c_str());
					__reqHandler()->_handleParamData(_request, _key, _valOfs,
						value.begin(), value.length());
					_valOfs+= value.length();
				} else {
					ESPWS_DEBUGVV_S(L,"[%s] + [%s] = '%s'\n",
						_request._remoteIdent.c_str(), _key.c_str(), value.c_str());
					_memCached+= _key.length();
					_memCached+= value.length();
					__addParam(_key, value);
				}
				return true;
			}

			ESPWS_DEBUG_S(L,"[%s] Invalid request parameter state '%s'\n",
				_request._remoteIdent.c_str(), SFPSTR(_stateToString()));
			return false;
		}

	public:
		AsyncRequestMultipartFormContentParser(AsyncWebRequest &request)
		: AsyncWebParser(request), _state(MP_PARSER_STARTUP), _filepart(false),
			_curOfs(0), _valOfs(0), _parseOfs(0), _memCached(0) {
			int indexBoundary = _request.contentType().indexOf(FL("boundary="), 20);
			if (indexBoundary < 0) {
				ESPWS_DEBUG_S(L,"[%s] Missing boundary specification\n",
					_request._remoteIdent.c_str());
				__reqState(REQUEST_ERROR);
				return;
			}
			const char* valStart = &_request.contentType().begin()[indexBoundary+9];
			_boundary = getQuotedToken(valStart);
			ESPWS_DEBUGVV_S(L,"[%s] Part boundary: '%s'\n",
				_request._remoteIdent.c_str(), _boundary.c_str());
			_boundaryLen = _boundary.length();
			__setContentType(String(_request.contentType().begin(),19));
		}

		bool _handleHeader(String &line) {
			ESPWS_DEBUGVV_S(L,"[%s] # %s\n", _request._remoteIdent.c_str(), line.c_str());

			// Split the header into key and value
			int keyEnd = line.indexOf(':');
			if (keyEnd <= 0) return false;
			int indexValue = keyEnd;
			while (line[++indexValue] == ' ');
			String value = line.substring(indexValue);
			line.remove(keyEnd);

			if (line.equalsIgnoreCase(FL("Content-Disposition"))) {
				if (!value.startsWith(FL("form-data;"), 10, 0, true)) {
					ESPWS_DEBUG_S(L,"[%s] Unrecognised disposition type '%s'\n",
						_request._remoteIdent.c_str(), value.c_str());
					return false;
				}
				int indexName = value.indexOf(FL("name="),10);
				if (indexName < 0) return false;
				const char* valStart = &value[indexName+5];
				_key = getQuotedToken(valStart);
				int indexFName = value.indexOf(FL("filename="),10);
				if (indexFName > 0) {
					_filepart = true;
					valStart = &value[indexFName+9];
					_filename = getQuotedToken(valStart);
					ESPWS_DEBUGV_S(L,"[%s] * Part [%s], file '%s'\n",
						_request._remoteIdent.c_str(), _key.c_str(), _filename.c_str());
				} else {
					ESPWS_DEBUGV_S(L,"[%s] * Part [%s]\n",
						_request._remoteIdent.c_str(), _key.c_str());
				}
			} else if (line.equalsIgnoreCase(FL("Content-Type"))) {
				_contentType = std::move(value);
				ESPWS_DEBUGV_S(L,"[%s] * Content-Type: '%s'\n",
					_request._remoteIdent.c_str(), _contentType.c_str());
			} else {
				ESPWS_DEBUG_S(L,"[%s] Unexpected part header '%s'\n",
					_request._remoteIdent.c_str(), line.c_str());
				return false;
			}
			return true;
		}

		bool _handlePartBoundary(void *buf, size_t len) {
			switch (_state) {
				case MP_PARSER_STARTUP:
					if (len) {
						ESPWS_DEBUG_S(L,"[%s] WARNING: Ignoring startup data (%d bytes)\n",
							_request._remoteIdent.c_str(), len);
					}
					return true;

				case MP_PARSER_VALUE:
					return _pushKeyVal(len? String((char*)buf,len) : String(), false);

				case MP_PARSER_CONTENT:
					if (__reqHandler()->_handleUploadData(_request, _key, _filename,
						_contentType, _valOfs, buf, len)) {
						__addUpload(_key,_filename,_contentType,_valOfs+len);
						return true;
					} break;

				default:
					ESPWS_DEBUG_S(L,"[%s] WARNING: Unrecognised parameter state '%s'\n",
						_request._remoteIdent.c_str(), SFPSTR(_stateToString()));
			}
			return false;
		}

		bool _handlePartMiddle(void *buf, size_t len) {
			switch (_state) {
				case MP_PARSER_STARTUP:
					if (len) {
						ESPWS_DEBUG_S(L,"[%s] WARNING: Ignoring startup data (%d bytes)\n",
							_request._remoteIdent.c_str(), len);
					}
					return true;

				case MP_PARSER_VALUE:
					return _pushKeyVal(len? String((char*)buf,len) : String(), true);

				case MP_PARSER_CONTENT: {
					size_t __valOfs = _valOfs;
					_valOfs += len;
					return __reqHandler()->_handleUploadData(_request, _key, _filename,
						_contentType, _valOfs, buf, len);
				}

				default:
					ESPWS_DEBUG_S(L,"[%s] WARNING: Unrecognised parameter state '%s'\n",
						_request._remoteIdent.c_str(), SFPSTR(_stateToString()));
			}
			return false;
		}

		virtual void _parse(void *&buf, size_t &len) override {
			char *str = (char*)buf;
			if (_temp) {
				_temp.concat(str, len);
				str = _temp.begin();
				len = _temp.length();
			}
			_curOfs += len;
			while (len && buf) {
				switch (_state) {
					case MP_PARSER_STARTUP:
					case MP_PARSER_VALUE:
					case MP_PARSER_CONTENT: {
						size_t i = _parseOfs;
						bool partBoundary = false;
						while (i < REQUEST_PARAM_MEMCACHE && i < len) {
							if (_state == MP_PARSER_STARTUP) {
								if (str[i++] == '-') {
									if (i+1+_boundaryLen <= len) {
										if (str[i] == '-' &&
											_boundary.startsWith(&str[i+1], _boundaryLen, 0, false)) {
											partBoundary = true;
											break;
										}
									} else break;
								}
							}
							if (str[i++] == '\r') {
								if (i+3+_boundaryLen <= len) {
									if (str[i] == '\n' && str[i+1] == '-' && str[i+2] == '-' &&
											_boundary.startsWith(&str[i+3], _boundaryLen, 0, false)) {
										partBoundary = true;
										break;
									}
								} else break;
							}
						}
						if (partBoundary) {
							ESPWS_DEBUGVV_S(L,"[%s] Boundary detected @%d\n",
								_request._remoteIdent.c_str(), i);
							if (!_handlePartBoundary(str,i-1)) {
								__reqState(REQUEST_ERROR);
								return;
							}
							i+= (str[i] == '-')? 1 : 3;
							i+= _boundaryLen;
							str+= i;
							len-= i;
							_state = MP_PARSER_BOUNDARY;
							_key.clear();
							_filepart = false;
							_filename.clear();
							_contentType.clear();
							_parseOfs = 0;
						} else {
							if (i >= REQUEST_PARAM_MEMCACHE) {
								if (!_handlePartMiddle(str,i)) {
									__reqState(REQUEST_ERROR);
									return;
								}
								str+= i;
								len-= i;
							} else {
								_parseOfs = i;
								buf = nullptr;
							}
						}
					} break;

					case MP_PARSER_BOUNDARY:
					case MP_PARSER_HEADER: {
						// Find new line in buf
						size_t i = _parseOfs;
						while (str[i] != '\n') {
							if (++i >= len) {
								// No new line, wait for next buffer
								_parseOfs = i;
								buf = nullptr;
								break;
							}
						}
						if (buf) {
							// Found new line - extract it and parse
							if (i > 1) {
								String line = String(str, i-1);
								line.trim();
								if (_state == MP_PARSER_HEADER) {
									if (!_handleHeader(line)) {
										__reqState(REQUEST_ERROR);
										return;
									}
								} else {
									if (line != FL("--")) {
										ESPWS_DEBUG_S(L,"[%s] Unrecognised part boundary preamble '%s'\n",
											_request._remoteIdent.c_str(), line.c_str());
										__reqState(REQUEST_ERROR);
										return;
									} else {
										ESPWS_DEBUGVV_S(L,"[%s] Part Start\n", _request._remoteIdent.c_str());
										_state = MP_PARSER_TERMINATE;
									}
								}
							} else {
								if (_state == MP_PARSER_HEADER) {
									if (_filepart) {
										_state = MP_PARSER_CONTENT;
										if (!_filename) {
											ESPWS_DEBUG_S(L,"[%s] WARNING: Empty file name\n",
												_request._remoteIdent.c_str());
										}
										if (!_contentType) {
											ESPWS_DEBUG_S(L,"[%s] WARNING: No content type specified\n",
												_request._remoteIdent.c_str());
											_contentType = FL("text/plain");
										}
									} else _state = MP_PARSER_VALUE;
								} else {
									ESPWS_DEBUGVV_S(L,"[%s] Part End\n", _request._remoteIdent.c_str());
									_state = MP_PARSER_HEADER;
								}
							}
							_parseOfs = 0;
							str+= i+1;
							len-= i+1;
						}
					} break;

					case MP_PARSER_TERMINATE:
						ESPWS_DEBUG_S(L,"[%s] WARNING: Ignoring terminated data (%d bytes)\n",
							_request._remoteIdent.c_str(), len);
						len = 0;
						break;

					default:
						ESPWS_DEBUG_S(L,"[%s] Invalid request parameter state '%s'\n",
							_request._remoteIdent.c_str(), SFPSTR(_stateToString()));
						__reqState(REQUEST_ERROR);
				}
			}
			if (len) _temp = String(str, len);
			_checkReachEnd([&]{
				if (_state != MP_PARSER_TERMINATE) {
					ESPWS_DEBUG_S(L,"[%s] Excessive data at end of body\n",
						_request._remoteIdent.c_str());
					__reqState(REQUEST_ERROR);
				}
			});
		}
};

#endif

LinkedList<ArBodyParserMaker> BodyParserRegistry(nullptr, {
#ifdef HANDLE_REQUEST_CONTENT_SIMPLEFORM
	[](AsyncWebRequest &request) {
		return request.contentType(FPSTR(SIMPLEFORM_MIME)) ?
			new AsyncSimpleFormContentParser(request): nullptr;
	},
#endif
#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
	[](AsyncWebRequest &request) {
		return request.contentType()
			.startsWith(FPSTR(MULTIPARTFORM_MIMEPFX), 0, true) ?
			new AsyncRequestMultipartFormContentParser(request): nullptr;
	},
#endif
});

#endif