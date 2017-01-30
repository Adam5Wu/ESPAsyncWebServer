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
#ifndef ASYNCWEBREQUESTPARSER_H_
#define ASYNCWEBREQUESTPARSER_H_

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

/*
 * PARSER :: Handles different stages of request parsing, boxes internals avoid pollute public API
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
    void __setUrl(String &&newUrl) { _request._setUrl(std::move(newUrl)); }
    void __setHost(String &newHost) {
      if (!newHost.empty()) _request._host = std::move(newHost);
      else { _request._host.reserve(0); _request._host.clear(); }
    }
    void __setContentType(String &newContentType)
    { _request._contentType = std::move(newContentType); }
    void __setContentLength(size_t newContentLength)
    { _request._contentLength = newContentLength; }
    void __setAuthType(WebServerRequestAuth newAuthType) { _request._authType = newAuthType; }
    void __setAuthorization(String &newAuthorization)
    { _request._authorization = std::move(newAuthorization); }
    void __setAuthorization(const char *buf) { _request._authorization = buf; }
    void __addHeader(String const &key, String const &value) {
      AsyncWebHeader* Header = _request._headers.get_if([&](AsyncWebHeader const &h){
        return key.equalsIgnoreCase(h.name);
      });
      if (Header) Header->values.add(std::move(value));
      else _request._headers.add(AsyncWebHeader(std::move(key), std::move(value)));
    }

    ESPWS_DEBUGDO(const char* __strState(void) { return _request._stateToString(); })

  public:
    AsyncWebParser(AsyncWebRequest &request) : _request(request) {}
    virtual ~AsyncWebParser() {}

    virtual void _parse(void *&buf, size_t &len) = 0;
};

/*
 * HEAD PARSER :: Handles parsing the head part of the request
 * */
class AsyncRequestHeadParser: public AsyncWebParser {
  private:
    String _temp;
    bool _expectingContinue = false;

    bool _parseLine(void);
    bool _parseReqStart(void);
    bool _parseReqHeader(void);

  public:
    AsyncRequestHeadParser(AsyncWebRequest &request) : AsyncWebParser(request) {}

    virtual void _parse(void *&buf, size_t &len) override;
};

#ifdef HANDLE_REQUEST_CONTENT
/*
 * UNIBODY CONTENT PARSER :: Handles parsing of monolithic request body content
 * */
class AsyncRequestUnibodyContentParser: public AsyncWebParser {
  public:
    virtual void _parse(void *&buf, size_t &len) override;
};

/*
* MULTIPART CONTENT PARSER :: Handles parsing of multipart request body content
* */
class AsyncRequestMultipartContentParser: public AsyncWebParser {
  private:
    String _boundary;

  public:
    virtual void _parse(void *&buf, size_t &len) override;
 /*
   if (strncmp(value.begin(), "multipart/", 10) == 0) {
   int typeEnd = value.indexOf(';', 10);
   if (typeEnd <= 0) return false;
   int indexBoundary = value.indexOf('=', typeEnd + 8);
   if (indexBoundary <= 0) return false;
   _boundary = &value[indexBoundary+1];
   value.remove(typeEnd);

   _request._contentType = std::move(value);
   ESPWS_DEBUGV("[%s] + Content-Type: '%s', boundary='%s'\n", _request._remoteIdent.c_str(),
   _request._contentType.c_str(), _boundary.c_str());
 } else {
  */
};

#endif

#endif /* ASYNCWEBREQUESTPARSER_H_ */
