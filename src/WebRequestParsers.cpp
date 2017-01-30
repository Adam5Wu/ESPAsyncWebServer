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

bool AsyncRequestHeadParser::_parseLine(void) {
  switch (__reqState()) {
    case REQUEST_START:
      if(!_temp.empty() && _parseReqStart()) {
        // Perform rewrite and handler lookup
        _request._server._rewriteRequest(_request);
        _request._server._attachHandler(_request);

        __reqState(REQUEST_HEADERS);
      } else {
        __reqState(REQUEST_ERROR);
        return false;
      }
      break;

    case REQUEST_HEADERS:
      if(!_temp.empty()) {
        // More headers
        if (!_parseReqHeader()) {
          __reqState(REQUEST_ERROR);
          return false;
        }
        break;
      }

      // End of headers
      if(!__reqHandler()) {
        // No handler available
        _request.send(501);
        __reqState(REQUEST_RESPONSE);
      } else {
#ifdef STRICT_PROTOCOL
        // According to RFC, HTTP/1.1 requires host header
        if (_request._version && !_request._host) {
          _request.send(400);
          __reqState(REQUEST_RESPONSE);
        } else
#endif
        // Check if we can continue
        if (__reqHandler()->_checkContinue(_request, _expectingContinue)) {
#ifdef HANDLE_REQUEST_CONTENT
            if (_request._contentLength) {
            // Switch parser
            __reqState(RESPONSE_CONTENT);
            __reqParser(new AsyncRequestContentParser(_request));
          } else
#endif
          {
            __reqState(REQUEST_RECEIVED);
            __reqParser(NULL);
          }
          // We are done!
          delete this;
        } else __reqState(REQUEST_RESPONSE);
      }
      return false;

    default:
      ESPWS_DEBUG("[%s] Unexpected request status [%s]", _request._remoteIdent.c_str(),
                  __strState());
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

  if (strcmp(_temp.begin(), "GET") == 0) {
    __setMethod(HTTP_GET);
  } else if (strcmp(_temp.begin(), "PUT") == 0) {
    __setMethod(HTTP_PUT);
  } else if (strcmp(_temp.begin(), "POST") == 0) {
    __setMethod(HTTP_POST);
  } else if (strcmp(_temp.begin(), "HEAD") == 0) {
    __setMethod(HTTP_HEAD);
  } else if (strcmp(_temp.begin(), "PATCH") == 0) {
    __setMethod(HTTP_PATCH);
  } else if (strcmp(_temp.begin(), "DELETE") == 0) {
    __setMethod(HTTP_DELETE);
  } else if (strcmp(_temp.begin(), "OPTIONS") == 0) {
    __setMethod(HTTP_OPTIONS);
  }

  __setVersion(memcmp(&_temp[indexVer+1], "HTTP/1.0", 8)? 1 : 0);
  __setUrl(urlDecode(&_temp[indexUrl+1]));

  ESPWS_DEBUGV("[%s] HTTP/1.%d %s %s\n", _request._remoteIdent.c_str(),
               _request.version(), _request.methodToString(), _request.url().c_str());
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

  if (_temp.equalsIgnoreCase("Host")) {
    __setHost(value);
    ESPWS_DEBUGV("[%s] + Host: '%s'\n", _request._remoteIdent.c_str(), _request.host().c_str());
  } else if (_temp.equalsIgnoreCase("Content-Type")) {
    __setContentType(value);
    ESPWS_DEBUGV("[%s] + Content-Type: '%s'\n", _request._remoteIdent.c_str(), _request.contentType().c_str());
  } else if (_temp.equalsIgnoreCase("Content-Length")) {
    size_t contentLength = value.toInt();
    if (!contentLength && errno) return false;
    __setContentLength(contentLength);
    ESPWS_DEBUGV("[%s] + Content-Length: %d\n", _request._remoteIdent.c_str(), _request.contentLength());
  } else if (_temp.equalsIgnoreCase("Expect")) {
    if (value.equalsIgnoreCase("100-continue")) {
      _expectingContinue = true;
      ESPWS_DEBUGV("[%s] + Expect: '%s'\n", _request._remoteIdent.c_str(), value.c_str());
    } else {
      ESPWS_DEBUGV("[%s] ? Expect: '%s'\n", _request._remoteIdent.c_str(), value.c_str());
#ifdef STRICT_PROTOCOL
      // According to RFC, unrecognised expect should be rejected with error
      _request.send(417);
      return false;
#endif
    }
  } else if (_temp.equalsIgnoreCase("Authorization")) {
    int authEnd = value.indexOf(' ');
    if (authEnd <= 0) return false;
    int indexData = authEnd;
    while (value[++indexData] == ' ');
    String auth = value.substring(0, authEnd);

    if (auth.equalsIgnoreCase("Basic")) {
      __setAuthType(AUTH_BASIC);
      __setAuthorization(&value[indexData]);
      ESPWS_DEBUGV("[%s] + Authorization: Basic '%s'\n", _request._remoteIdent.c_str(), _request.authorization().c_str());
      // Do Nothing
    } else if (auth.equalsIgnoreCase("Digest")) {
      __setAuthType(AUTH_DIGEST);
      __setAuthorization(&value[indexData]);
      ESPWS_DEBUGV("[%s] + Authorization: Digest '%s'\n", _request._remoteIdent.c_str(), _request.authorization().c_str());
    } else {
      __setAuthType(AUTH_OTHER);
      __setAuthorization(value);
      ESPWS_DEBUGV("[%s] ? Authorization: '%s'\n", _request._remoteIdent.c_str(), _request.authorization().c_str());
      return false;
    }
  } else {
    if (__reqHandler() && __reqHandler()->_isInterestingHeader(_temp)) {
      ESPWS_DEBUGV("[%s] ! %s: '%s'\n", _request._remoteIdent.c_str(), _temp.begin(), value.begin());
      __addHeader(_temp, value);
    }
  }
  return true;
}

void AsyncRequestHeadParser::_parse(void *&buf, size_t &len) {
  char *str = (char*)buf;
  while (true) {
    // Find new line in buf
    size_t i = 0;
    while (str[i] != '\n') {
      if (++i >= len) {
        // No new line, just add the buffer in _temp
        _temp.concat(str, len);
        return;
      }
    }
    // Found new line - extract it and parse
    _temp.concat(str, i++);
    _temp.trim();
    len-= i;

    if (!_parseLine()) break;
    _temp.clear();
    if (len) {
      // Still have more buffer to process
      buf = str+= i;
      continue;
    }
  }
}
