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
#ifndef AsyncWebRequestParser_H_
#define AsyncWebRequestParser_H_

#include <ESPAsyncWebServer.h>

/*
 * PARSER :: Handles different stages of request parsing,
 * boxes internals avoid pollute public API
 * */

class AsyncWebParser {
	protected:
		AsyncWebRequest &_request;

		// Provide accessors to request object
		WebServerRequestState __reqState(void) { return _request._state; }
		void __reqState(WebServerRequestState newState) { _request._state = newState; }

		void __reqParser(AsyncWebParser* newParser) { _request._parser = newParser; }
		AsyncWebHandler* __reqHandler(void) { return _request._handler; }
		void __setMethod(WebRequestMethod newMethod) { _request._method = newMethod; }
		void __setVersion(uint8_t newVersion) { _request._version = newVersion; }
		void __setUrl(char const *newUrl) { _request._setUrl(newUrl); }
		void __setHost(String &newHost) {
			if (newHost) _request._host = std::move(newHost);
			else { _request._host.clear(true); }
		}
		void __setUserAgent(String &newUserAgent) {
			if (newUserAgent) _request._userAgent = std::move(newUserAgent);
			else { _request._userAgent.clear(true); }
		}
		void __setAccept(String &newAccept) {
			if (newAccept) _request._accept = std::move(newAccept);
			else { _request._accept.clear(true); }
		}
		void __setKeepAlive(bool state) { _request._keepAlive = state; }
		void __setContentType(String &newContentType)
		{ _request._contentType = std::move(newContentType); }
		void __setContentType(String &&newContentType)
		{ _request._contentType = std::move(newContentType); }
		void __setContentLength(size_t newContentLength)
		{ _request._contentLength = newContentLength; }

#ifdef HANDLE_AUTHENTICATION
		bool __setSession(AuthSession* session) { return _request._setSession(session); }
#endif

		void __addHeader(String const &key, String const &value) {
			AsyncWebHeader* Header = _request._headers.get_if([&](AsyncWebHeader const &h){
				return key.equalsIgnoreCase(h.name);
			});
			if (Header) Header->values.append(std::move(value));
			else _request._headers.append(AsyncWebHeader(std::move(key), std::move(value)));
		}
#ifdef HANDLE_REQUEST_CONTENT

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
		AsyncWebParam& __addParam(String &key, String &value) {
			if (key.endsWith("[]",2,0,false)) {
				_request._params.append(AsyncWebParam(std::move(key), std::move(value)));
				return _request._params.back();
			} else return _request._addUniqueNameVal(_request._params, key, value);
		}

		AsyncWebUpload& __addUpload(String &key, String &filename, String &contentType,
			size_t contentLength) {
			_request._uploads.append(AsyncWebUpload(std::move(key), std::move(filename)));
			auto& Item = _request._uploads.back();
			Item.contentType = std::move(contentType);
			Item.contentLength = contentLength;
			return Item;
		}
#endif

#endif

		ESPWS_DEBUGDO(const char* __strState(void) { return _request._stateToString(); })

	public:
		AsyncWebParser(AsyncWebRequest &request) : _request(request) {}
		virtual ~AsyncWebParser() {}

		virtual void _parse(void *&buf, size_t &len) = 0;
		ESPWS_DEBUGDO(virtual const char* _stateToString(void) const = 0);
};

/*
 * HEAD PARSER :: Handles parsing the head part of the request
 * */
typedef enum {
	H_PARSER_ACCU,
	H_PARSER_LINE
} HeaderParserState;

class AsyncRequestHeadParser: public AsyncWebParser {
	private:
		HeaderParserState _state;
		String _temp;

		bool _handlerAttached = false;
		bool _expectingContinue = false;
#ifdef HANDLE_AUTHENTICATION
		String _authorization;
#endif

		bool _parseLine(void);
		bool _parseReqStart(void);
		bool _parseReqHeader(void);

	public:
		AsyncRequestHeadParser(AsyncWebRequest &request)
		: AsyncWebParser(request), _state(H_PARSER_ACCU)
#ifdef HANDLE_AUTHENTICATION
		//, _authorization()
#endif
		{}

		virtual void _parse(void *&buf, size_t &len) override;

#ifdef HANDLE_AUTHENTICATION
		AuthSession* _handleAuth(void);
		void _requestAuth(bool renew = false);
		void _rejectAuth(AuthSession *session);
#endif

		ESPWS_DEBUGDO(const char* _stateToString(void) const override);
};

#ifdef HANDLE_REQUEST_CONTENT

/*
 * PASS-THROUGH CONTENT PARSER :: Pass-through parsing of request body content to handler
 * */
class AsyncRequestPassthroughContentParser: public AsyncWebParser {
	protected:
		size_t _curOfs;

	public:
		AsyncRequestPassthroughContentParser(AsyncWebRequest &request)
		: AsyncWebParser(request), _curOfs(0) {}

		virtual void _parse(void *&buf, size_t &len) override;

		ESPWS_DEBUGDO(const char* _stateToString(void) const override {
			return "Pass-through";
		})
};

#include "LinkedList.h"

typedef std::function<AsyncWebParser*(AsyncWebRequest &request)> ArBodyParserMaker;
extern LinkedList<ArBodyParserMaker> BodyParserRegistry;

#endif

#endif /* AsyncWebRequestParser_H_ */
