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
          if (__reqState() == REQUEST_HEADERS)
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
          if (_request.contentLength()) {
            // Switch parser
            AsyncWebParser* newParser = NULL;
            for (auto& item : BodyParserRegistry) {
              if (newParser = item(_request)) break;
            }
            ESPWS_DEBUGVVDO({
              if (newParser) ESPWS_DEBUG("[%s] Using registered body parser\n", _request._remoteIdent.c_str());
              else ESPWS_DEBUG("[%s] Using generic body parser\n", _request._remoteIdent.c_str());
            });
            __reqParser(newParser? newParser : new AsyncRequestPassthroughContentParser(_request));
            __reqState(REQUEST_BODY);
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
  __setUrl(&_temp[indexUrl+1]);

  // Per RFC, HTTP 1.1 connections are persistent by default
  if (_request.version()) __setKeepAlive(true);

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
  } else if (_temp.equalsIgnoreCase("Connection")) {
    if (value.equalsIgnoreCase("keep-alive")) {
      __setKeepAlive(true);
      ESPWS_DEBUGV("[%s] + Connection: '%s'\n", _request._remoteIdent.c_str(), value.c_str());
    } else if (value.equalsIgnoreCase("close")) {
      __setKeepAlive(false);
      ESPWS_DEBUGV("[%s] + Connection: '%s'\n", _request._remoteIdent.c_str(), value.c_str());
    } else {
      ESPWS_DEBUGV("[%s] ? Connection: '%s'\n", _request._remoteIdent.c_str(), value.c_str());
#ifdef STRICT_PROTOCOL
      _request.send(400);
      __reqState(REQUEST_RESPONSE);
      return false;
#endif
    }
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
      __reqState(REQUEST_RESPONSE);
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
  while (len) {
    // Find new line in buf
    size_t i = 0;
    while (str[i] != '\n') {
      if (++i >= len) {
        // No new line, just add the buffer in _temp
        _temp.concat(str, len);
        len = 0;
        return;
      }
    }
    // Found new line - extract it and parse
    _temp.concat(str, i++);
    _temp.trim();

    len-= i;
    buf = str+= i;
    if (!_parseLine()) break;
    _temp.clear();
  }
}

#ifdef HANDLE_REQUEST_CONTENT

void AsyncRequestPassthroughContentParser::_parse(void *&buf, size_t &len) {
  // Simply track the upload progress and invoke handler
  if (!__reqHandler()->_handleBody(_request, _curOfs, buf, len)) {
    ESPWS_DEBUG("[%s] WARNING: Request body handling terminated abnormally!\n", _request._remoteIdent.c_str());
    __reqState(REQUEST_ERROR);
  } else {
    _curOfs+= len;
    len = 0;

    if (_curOfs >= _request.contentLength()) {
      __reqState(REQUEST_RECEIVED);
      __reqParser(NULL);
      // We are done!
      delete this;
    }
  }
}

typedef enum {
  PARSER_KEY,
  PARSER_VALUE
} SimpleFormParserState;

class AsyncRequestSimpleFormContentParser: public AsyncWebParser {
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
        if (callback) callback();
        if (__reqState() == REQUEST_BODY) {
          __reqState(REQUEST_RECEIVED);
          __reqParser(NULL);
          // We are done!
          delete this;
        }
        return true;
      }
      return false;
    }

    ESPWS_DEBUGDO(const char* _stateToString(void) const {
      switch (_state) {
        case PARSER_KEY: return "Key";
        case PARSER_VALUE: return "Value";
        default: return "???";
      }
    })

    bool _pushKeyVal(String &&value, bool _flush) {
      if (_state == PARSER_VALUE) {
        bool HandlerCallback = _flush || _memCacheFull();
        if (HandlerCallback) {
          ESPWS_DEBUGVV("[%s] * [%s]@%0.4X = '%s'\n", _request._remoteIdent.c_str(),
                        _key.c_str(), _valOfs, value.c_str());
          __reqHandler()->_handleParamData(_request, _key, EMPTY_STRING, _flush?0:value.length(),
                                           _valOfs, value.begin(), value.length());
          _valOfs+= value.length();
        } else {
          ESPWS_DEBUGVV("[%s] + [%s] = '%s'\n", _request._remoteIdent.c_str(),
                        _key.c_str(), value.c_str());
          _memCached+= _key.length();
          _memCached+= value.length();
          __addParam(_key, value);
        }
        return true;
      }

      ESPWS_DEBUG("[%s] WARNING: Invalid request parameter state '%s'\n",
                  _request._remoteIdent.c_str(), _stateToString());
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
    AsyncRequestSimpleFormContentParser(AsyncWebRequest &request)
    : AsyncWebParser(request), _state(PARSER_KEY), _curOfs(0), _valOfs(0), _memCached(0) {}

    virtual void _parse(void *&buf, size_t &len) override {
      char *str = (char*)buf;
      while (len) {
        char delim = '\0';
        size_t limit = 0;
        switch (_state) {
          case PARSER_KEY:
            delim = '=';
            limit = REQUEST_PARAM_KEYMAX;
            break;
          case PARSER_VALUE:
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
              ESPWS_DEBUG("[%s] WARNING: Request parameter token exceeds length limit!\n", _request._remoteIdent.c_str());
              __reqState(REQUEST_ERROR);
              return;
            }
            _temp.concat(str, i);
            len-= i;
            buf = str+= i;
            _curOfs+= i;

            // Have we reached the end?
            if (_checkReachEnd([&](){
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

        if (!_temp.empty()) {
          String item = urlDecode(_temp.begin(),_temp.length());
          switch (_state) {
            case PARSER_KEY:
              _key = std::move(item);
              _state = PARSER_VALUE;
              break;
            case PARSER_VALUE:
              _pushKeyVal(std::move(item), _valOfs);
              _state = PARSER_KEY;
              _valOfs = 0;
              break;
          }
        }
        if (_checkReachEnd(NULL)) return;
        _temp.clear();
      }
    }
};

LinkedList<ArBodyParserMaker> BodyParserRegistry(NULL, {
#ifdef HANDLE_REQUEST_CONTENT_SIMPLEFORM
  [](AsyncWebRequest &request) {
    return request.contentType("application/x-www-form-urlencoded")?
    new AsyncRequestSimpleFormContentParser(request): NULL;
  },
#endif
#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
  [](AsyncWebRequest &request) {
    return request.contentType().startsWith("multipart/form-data;",20,0,true)?
    new AsyncRequestMultipartFormContentParser(request): NULL;
  },
#endif
});

#endif