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
#include "ESPAsyncWebServer.h"
#include "WebResponseImpl.h"
#include "WebAuthentication.h"

extern "C" {
  #include <errno.h>
  #include "lwip/opt.h"
}

#ifndef ESP8266
#define os_strlen strlen
#endif

static const String SharedEmptyString = String();

enum { PARSE_REQ_START, PARSE_REQ_HEADERS, PARSE_REQ_BODY, PARSE_REQ_END, PARSE_REQ_FAIL };

AsyncWebServerRequest::AsyncWebServerRequest(AsyncWebServer* s, AsyncClient* c)
  : _client(c)
  , _server(s)
  , _handler(NULL)
  , _response(NULL)
  //, _interestingHeaders()
  , _wantAllHeaders(false)
  //, _temp()
  , _parseState(0)
  , _version(0)
  , _method(HTTP_ANY)
  //, _url()
  //, _host()
  //, _contentType()
  //, _boundary()
  //, _authorization()
  , _isDigest(false)
  , _isMultipart(false)
  , _isPlainPost(false)
  , _expectingContinue(false)
  , _contentLength(0)
  , _parsedLength(0)
  , _headers(LinkedList<AsyncWebHeader *>([](AsyncWebHeader *h){ delete h; }))
  , _params(LinkedList<AsyncWebParameter *>([](AsyncWebParameter *p){ delete p; }))
  , _multiParseState(0)
  , _boundaryPosition(0)
  , _itemStartIndex(0)
  , _itemSize(0)
  //, _itemName()
  //, _itemFilename()
  //, _itemType()
  //, _itemValue()
  , _itemBuffer(0)
  , _itemBufferIndex(0)
  , _itemIsFile(false)
  ESPWS_DEBUGDO(, _remoteIdent(c->remoteIP().toString()+':'+c->remotePort()))
{
  ESPWS_DEBUGV("[%s] CONNECTED\n", _remoteIdent.c_str());
  c->onError([](void *r, AsyncClient* c, int8_t error){
    AsyncWebServerRequest *req = (AsyncWebServerRequest*)r;
    req->_onError(error);
  }, this);
  c->onAck([](void *r, AsyncClient* c, size_t len, uint32_t time){
    AsyncWebServerRequest *req = (AsyncWebServerRequest*)r;
    req->_onAck(len, time);
  }, this);
  c->onDisconnect([](void *r, AsyncClient* c){
    AsyncWebServerRequest *req = (AsyncWebServerRequest*)r;
    req->_onDisconnect();
    delete c;
  }, this);
  c->onTimeout([](void *r, AsyncClient* c, uint32_t time){
    AsyncWebServerRequest *req = (AsyncWebServerRequest*)r;
    req->_onTimeout(time);
  }, this);
  c->onData([](void *r, AsyncClient* c, void *buf, size_t len){
    AsyncWebServerRequest *req = (AsyncWebServerRequest*)r;
    req->_onData(buf, len);
  }, this);
  c->onPoll([](void *r, AsyncClient* c){
    AsyncWebServerRequest *req = (AsyncWebServerRequest*)r;
    req->_onPoll();
  }, this);
}

AsyncWebServerRequest::~AsyncWebServerRequest(){
  delete _response;
}

#define __is_param_char(c) ((c) && ((c) != '{') && ((c) != '[') && ((c) != '&') && ((c) != '='))

void AsyncWebServerRequest::_onData(void *buf, size_t len){
  char *str = (char*)buf;
  while (true) {
    if(_parseState < PARSE_REQ_BODY){
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
      _temp.concat(str, i);
      _temp.trim();
      _parseLine();
      _temp.clear();
      if (++i < len) {
        // Still have more buffer to process
        buf = str+= i;
        len-= i;
        continue;
      }
    } else if(_parseState == PARSE_REQ_BODY){
      if(_isMultipart){
        size_t i;
        for(i=0; i<len; i++){
          _parseMultipartPostByte(((uint8_t*)buf)[i], i == len - 1);
          _parsedLength++;
        }
      } else {
        if(_parsedLength == 0){
          if(_contentType.startsWith("application/x-www-form-urlencoded")){
            _isPlainPost = true;
          } else if(_contentType == "text/plain" && __is_param_char(*str)){
            size_t i = 0;
            while (i<len && __is_param_char(str[i++]));
            if(i < len && str[i-1] == '=')
              _isPlainPost = true;
          }
        }
        if(!_isPlainPost) {
          // TODO: check if authenticated before calling the body
          if(_handler) _handler->handleBody(this, (uint8_t*)buf, len, _parsedLength, _contentLength);
          _parsedLength += len;
        } else {
          size_t i;
          for(i=0; i<len; i++){
            _parsedLength++;
            _parsePlainPostChar(str[i]);
          }
        }
      }

      if(_parsedLength == _contentLength){
        _temp.clear(true);
        _parseState = PARSE_REQ_END;
        // TODO: check if authenticated before calling handleRequest and request auth instead
        if(_handler) _handler->handleRequest(this);
        else send(501);
      }
    }
    break;
  }
}

void AsyncWebServerRequest::_onPoll(){
  if(_response != NULL && _client != NULL && _client->canSend() && !_response->_finished()){
    _response->_ack(this, 0, 0);
  }
}

void AsyncWebServerRequest::_onAck(size_t len, uint32_t time){
  ESPWS_DEBUGVV("[%s] ACK: %u @ %u\n", _remoteIdent.c_str(), len, time);
  if(_response != NULL){
    if(!_response->_finished()){
      _response->_ack(this, len, time);
    } else {
      AsyncWebServerResponse* r = _response;
      _response = NULL;
      delete r;
    }
  }
}

void AsyncWebServerRequest::_onError(int8_t error){
  ESPWS_DEBUG("[%s] ERROR: %d, state: %s\n", _remoteIdent.c_str(), error, _client->stateToString());
}

void AsyncWebServerRequest::_onTimeout(uint32_t time){
  ESPWS_DEBUG("[%s] TIMEOUT: %u, state: %s\n", _remoteIdent.c_str(), time, _client->stateToString());
  _client->close();
}

void AsyncWebServerRequest::_onDisconnect(){
  ESPWS_DEBUGV("[%s] DISCONNECT, response state: %s\n", _remoteIdent.c_str(), _response? _response->stateToString() : "NULL");
  _server->_handleDisconnect(this);
}

static const char response[] = "HTTP/1.1 100 Continue\r\n\r\n";

void AsyncWebServerRequest::_parseLine(){
  if(_parseState == PARSE_REQ_START){
    if(!_temp.empty() && _parseReqHead()) {
      // Perform rewrite and handler lookup
      _server->_rewriteRequest(this);
      _server->_attachHandler(this);

      _parseState = PARSE_REQ_HEADERS;
    } else {
      _parseState = PARSE_REQ_FAIL;
      _client->close();
    }
  } else if(_parseState == PARSE_REQ_HEADERS) {
    if(!_temp.empty()) {
      // More headers
      if (!_parseReqHeader()) {
        _parseState = PARSE_REQ_FAIL;
        _client->close();
      }
    } else {
      // End of headers

      // Protocol compliance checking
      if (_version && !_host) {
        // Host header is required for HTTP/1.1 per RFC
        send(400);
      } else {
        // TODO: Check handler for authentication
        if(!_handler) {
          // No handler available
          send(501);
        } else {
          // Handle 100-Continue
          if(_expectingContinue)
            _client->write(response, sizeof(response));

          if(_contentLength){
            _parseState = PARSE_REQ_BODY;
          } else {
            _parseState = PARSE_REQ_END;
            _handler->handleRequest(this);
          }
        }
      }
    }
  }
}

void AsyncWebServerRequest::_addParam(AsyncWebParameter *p){
  _params.add(p);
}

void AsyncWebServerRequest::_parseGetParams(char *params){
  while (*params){
    char *name = params;
    while (*params != '&')
      if (*params) params++;
      else break;
    if (name == params) {
      params++;
      continue;
    }
    if (*params) *params++ = '\0';

    char *value = name;
    while (*value != '=')
      if (*value) value++;
      else break;
    if (*value) *value++ = '\0';

    ESPWS_DEBUGVV("[%s] Query [%s] = '%s'\n", _remoteIdent.c_str(), name, value);
    _addParam(new AsyncWebParameter(name, value));
  }
}

bool AsyncWebServerRequest::_parseReqHead(){
  // Split the head into method, url and version
  int indexUrl = _temp.indexOf(' ');
  if (indexUrl <= 0) return false;
  _temp[indexUrl] = '\0';

  int indexVer = _temp.indexOf(' ', indexUrl+1);
  if (indexVer <= 0) return false;
  _temp[indexVer] = '\0';

  if (strcmp(_temp.begin(), "GET") == 0) {
    _method = HTTP_GET;
  } else if (strcmp(_temp.begin(), "PUT") == 0) {
    _method = HTTP_PUT;
  } else if (strcmp(_temp.begin(), "POST") == 0) {
    _method = HTTP_POST;
  } else if (strcmp(_temp.begin(), "HEAD") == 0) {
    _method = HTTP_HEAD;
  } else if (strcmp(_temp.begin(), "PATCH") == 0) {
    _method = HTTP_PATCH;
  } else if (strcmp(_temp.begin(), "DELETE") == 0) {
    _method = HTTP_DELETE;
  } else if (strcmp(_temp.begin(), "OPTIONS") == 0) {
    _method = HTTP_OPTIONS;
  }

  _version = memcmp(&_temp[indexVer+1], "HTTP/1.0", 8)? 1 : 0;

  _url = urlDecode(&_temp[indexUrl+1]);

  ESPWS_DEBUGVV("[%s] HTTP/1.%d %s %s\n", _remoteIdent.c_str(), _version, methodToString(), _url.c_str());

  int indexQuery = _url.indexOf('?');
  if(indexQuery > 0){
    _parseGetParams(&_url[indexQuery+1]);
    _url.remove(indexQuery);
  }
  return true;
}

bool AsyncWebServerRequest::_parseReqHeader(){
  // Split the header into key and value
  int keyEnd = _temp.indexOf(':');
  if (keyEnd <= 0) return false;
  int indexValue = keyEnd;
  while (_temp[++indexValue] == ' ');
  String value = _temp.substring(indexValue);
  _temp.remove(keyEnd);

  ESPWS_DEBUGVV("[%s] Header [%s] : '%s'\n", _remoteIdent.c_str(), _temp.c_str(), value.c_str());

  if (_temp.equalsIgnoreCase("Host")) {
    _host = std::move(value);
    // In case host is empty string, at least mark the header as present
    if (!_host) _host.reserve(0);
    ESPWS_DEBUGVV("[%s] + Host: '%s'\n", _remoteIdent.c_str(), _host.c_str());
  } else if (_temp.equalsIgnoreCase("Content-Type")) {
    if (strncmp(value.begin(), "multipart/", 10) == 0) {
      int typeEnd = value.indexOf(';', 10);
      if (typeEnd <= 0) return false;
      int indexBoundary = value.indexOf('=', typeEnd + 8);
      if (indexBoundary <= 0) return false;
      _boundary = &value[indexBoundary+1];
      value.remove(typeEnd);

      _contentType = std::move(value);
      _isMultipart = true;
      ESPWS_DEBUGVV("[%s] + Content-Type: '%s', boundary='%s'\n", _remoteIdent.c_str(), _contentType.c_str(), _boundary.c_str());
    } else {
      _contentType = std::move(value);
      ESPWS_DEBUGVV("[%s] + Content-Type: '%s'\n", _remoteIdent.c_str(), _contentType.c_str());
    }
  } else if (_temp.equalsIgnoreCase("Content-Length")) {
    _contentLength = atoi(value.begin());
    if (!_contentLength && errno) return false;
    ESPWS_DEBUGVV("[%s] + Content-Length: %d\n", _remoteIdent.c_str(), _contentLength);
  } else if (_temp.equalsIgnoreCase("Expect")) {
    if (value.equalsIgnoreCase("100-continue")) {
      _expectingContinue = true;
      ESPWS_DEBUGVV("[%s] + Expect: 100-continue\n", _remoteIdent.c_str());
    }
  } else if (_temp.equalsIgnoreCase("Authorization")) {
    int authEnd = value.indexOf(' ');
    if (authEnd <= 0) return false;
    int indexData = authEnd;
    while (value[++indexData] == ' ');
    _authorization = &value[indexData];
    value.remove(authEnd);

    if (value.equalsIgnoreCase("Basic")) {
      ESPWS_DEBUGVV("[%s] + Authorization: Basic '%s'\n", _remoteIdent.c_str(), _authorization.c_str());
      // Do Nothing
    } else if (value.equalsIgnoreCase("Digest")) {
      ESPWS_DEBUGVV("[%s] + Authorization: Digest '%s'\n", _remoteIdent.c_str(), _authorization.c_str());
      _isDigest = true;
    } else return false;
  } else {
    if (_wantAllHeaders || _interestingHeaders.containsIgnoreCase(_temp.begin())) {
      ESPWS_DEBUGVV("[%s] ? %s: '%s'\n", _remoteIdent.c_str(), _temp.begin(), value.begin());
      _headers.add(new AsyncWebHeader(std::move(_temp), std::move(value)));
    }
  }
  return true;
}

void AsyncWebServerRequest::_parsePlainPostChar(char data){
  if(!data || data == '&' || _parsedLength >= _contentLength){
    String name = urlDecode(_temp);
    String value;
    uint16_t delim;
    if(!name[0] == '{' && !name[0] == '[' && (delim = name.indexOf('=')) > 0){
      value = name.substring(delim + 1);
      name.remove(delim);
    } else {
      value = std::move(name);
      name = "body";
    }
    _addParam(new AsyncWebParameter(std::move(name), std::move(value), true));
    _temp.clear();
  } else _temp += data;
}

void AsyncWebServerRequest::_handleUploadByte(uint8_t data, bool last){
  _itemBuffer[_itemBufferIndex++] = data;

  if(last || _itemBufferIndex >= TCP_MSS){
    // TODO: Check if authenticated before calling the upload
    if(_handler)
      _handler->handleUpload(this, _itemFilename, _itemSize - _itemBufferIndex, _itemBuffer, _itemBufferIndex, false);
    _itemBufferIndex = 0;
  }
}

enum {
  EXPECT_BOUNDARY,
  PARSE_HEADERS,
  WAIT_FOR_RETURN1,
  EXPECT_FEED1,
  EXPECT_DASH1,
  EXPECT_DASH2,
  BOUNDARY_OR_DATA,
  DASH3_OR_RETURN2,
  EXPECT_FEED2,
  PARSING_FINISHED,
  PARSE_ERROR
};

void AsyncWebServerRequest::_parseMultipartPostByte(uint8_t data, bool last){
#define itemWriteByte(b) { _itemSize++; if(_itemIsFile) _handleUploadByte(b, last); else _itemValue+=(char)(b); }

  if(!_parsedLength){
    _multiParseState = EXPECT_BOUNDARY;
    _temp.clear();
    _itemName.clear();
    _itemFilename.clear();
    //_itemType.clear();
  }

  if(_multiParseState == WAIT_FOR_RETURN1){
    if(data != '\r'){
      itemWriteByte(data);
    } else {
      _multiParseState = EXPECT_FEED1;
    }
  } else if(_multiParseState == EXPECT_BOUNDARY){
    if(_parsedLength < 2 && data != '-'){
      _multiParseState = PARSE_ERROR;
      return;
    } else if(_parsedLength - 2 < _boundary.length() && _boundary.c_str()[_parsedLength - 2] != data){
      _multiParseState = PARSE_ERROR;
      return;
    } else if(_parsedLength - 2 == _boundary.length() && data != '\r'){
      _multiParseState = PARSE_ERROR;
      return;
    } else if(_parsedLength - 3 == _boundary.length()){
      if(data != '\n'){
        _multiParseState = PARSE_ERROR;
        return;
      }
      _multiParseState = PARSE_HEADERS;
      _itemIsFile = false;
    }
  } else if(_multiParseState == PARSE_HEADERS){
    if((char)data != '\r' && (char)data != '\n')
       _temp += (char)data;
    if((char)data == '\n'){
      if(!_temp.empty()){
        if(_temp.length() > 12 && _temp.substring(0, 12).equalsIgnoreCase("Content-Type")){
          _itemType = _temp.substring(14);
          _itemIsFile = true;
        } else if(_temp.length() > 19 && _temp.substring(0, 19).equalsIgnoreCase("Content-Disposition")){
          _temp = _temp.substring(_temp.indexOf(';') + 2);
          while(_temp.indexOf(';') > 0){
            String name = _temp.substring(0, _temp.indexOf('='));
            String nameVal = _temp.substring(_temp.indexOf('=') + 2, _temp.indexOf(';') - 1);
            if(name == "name"){
              _itemName = nameVal;
            } else if(name == "filename"){
              _itemFilename = nameVal;
              _itemIsFile = true;
            }
            _temp = _temp.substring(_temp.indexOf(';') + 2);
          }
          String name = _temp.substring(0, _temp.indexOf('='));
          String nameVal = _temp.substring(_temp.indexOf('=') + 2, _temp.length() - 1);
          if(name == "name"){
            _itemName = nameVal;
          } else if(name == "filename"){
            _itemFilename = nameVal;
            _itemIsFile = true;
          }
        }
        _temp.clear();
      } else {
        _multiParseState = WAIT_FOR_RETURN1;
        //value starts from here
        _itemSize = 0;
        _itemStartIndex = _parsedLength;
        _itemValue = String();
        if(_itemIsFile){
          free(_itemBuffer);
          _itemBuffer = (uint8_t*)malloc(TCP_MSS);
          if(_itemBuffer == NULL){
            _multiParseState = PARSE_ERROR;
            return;
          }
          _itemBufferIndex = 0;
        }
      }
    }
  } else if(_multiParseState == EXPECT_FEED1){
    if(data != '\n'){
      _multiParseState = WAIT_FOR_RETURN1;
      itemWriteByte('\r'); _parseMultipartPostByte(data, last);
    } else {
      _multiParseState = EXPECT_DASH1;
    }
  } else if(_multiParseState == EXPECT_DASH1){
    if(data != '-'){
      _multiParseState = WAIT_FOR_RETURN1;
      itemWriteByte('\r'); itemWriteByte('\n');  _parseMultipartPostByte(data, last);
    } else {
      _multiParseState = EXPECT_DASH2;
    }
  } else if(_multiParseState == EXPECT_DASH2){
    if(data != '-'){
      _multiParseState = WAIT_FOR_RETURN1;
      itemWriteByte('\r'); itemWriteByte('\n'); itemWriteByte('-');  _parseMultipartPostByte(data, last);
    } else {
      _multiParseState = BOUNDARY_OR_DATA;
      _boundaryPosition = 0;
    }
  } else if(_multiParseState == BOUNDARY_OR_DATA){
    if(_boundaryPosition < _boundary.length() && _boundary.c_str()[_boundaryPosition] != data){
      _multiParseState = WAIT_FOR_RETURN1;
      itemWriteByte('\r'); itemWriteByte('\n'); itemWriteByte('-');  itemWriteByte('-');
      uint8_t i;
      for(i=0; i<_boundaryPosition; i++)
        itemWriteByte(_boundary.c_str()[i]);
      _parseMultipartPostByte(data, last);
    } else if(_boundaryPosition == _boundary.length() - 1){
      _multiParseState = DASH3_OR_RETURN2;
      if(!_itemIsFile){
        _addParam(new AsyncWebParameter(std::move(_itemName), std::move(_itemValue), true));
      } else {
        if(_itemSize){
          // TODO: Check if authenticated before calling the upload
          if(_handler) _handler->handleUpload(this, _itemFilename, _itemSize - _itemBufferIndex, _itemBuffer,
                                              _itemBufferIndex, true);
          _itemBufferIndex = 0;
          _addParam(new AsyncWebParameter(std::move(_itemName), std::move(_itemFilename), true, true, _itemSize));
        }
        free(_itemBuffer);
        _itemBuffer = NULL;
      }
    } else {
      _boundaryPosition++;
    }
  } else if(_multiParseState == DASH3_OR_RETURN2){
    if(data == '-' && (_contentLength - _parsedLength - 4) != 0){
      ESPWS_DEBUG("[%s] ERROR: The parser got to the end of the POST but is expecting %u bytes more!\n",
                  _remoteIdent.c_str(), _contentLength - _parsedLength - 4);
      _contentLength = _parsedLength + 4;//lets close the request gracefully
    }
    if(data == '\r'){
      _multiParseState = EXPECT_FEED2;
    } else if(data == '-' && _contentLength == (_parsedLength + 4)){
      _multiParseState = PARSING_FINISHED;
    } else {
      _multiParseState = WAIT_FOR_RETURN1;
      itemWriteByte('\r'); itemWriteByte('\n'); itemWriteByte('-');  itemWriteByte('-');
      uint8_t i; for(i=0; i<_boundary.length(); i++) itemWriteByte(_boundary.c_str()[i]);
      _parseMultipartPostByte(data, last);
    }
  } else if(_multiParseState == EXPECT_FEED2){
    if(data == '\n'){
      _multiParseState = PARSE_HEADERS;
      _itemIsFile = false;
    } else {
      _multiParseState = WAIT_FOR_RETURN1;
      itemWriteByte('\r'); itemWriteByte('\n'); itemWriteByte('-');  itemWriteByte('-');
      uint8_t i; for(i=0; i<_boundary.length(); i++) itemWriteByte(_boundary.c_str()[i]);
      itemWriteByte('\r'); _parseMultipartPostByte(data, last);
    }
  }
}


size_t AsyncWebServerRequest::headers() const{
  return _headers.length();
}

bool AsyncWebServerRequest::hasHeader(const String& name) const {
  for(const auto& h: _headers){
    if(h->name().equalsIgnoreCase(name))
      return true;
  }
  return false;
}

AsyncWebHeader* AsyncWebServerRequest::getHeader(const String& name) const {
  for(auto& h: _headers){
    if(h->name().equalsIgnoreCase(name)){
      return h;
    }
  }
  return NULL;
}

AsyncWebHeader* AsyncWebServerRequest::getHeader(size_t num) const {
  auto header = _headers.nth(num);
  return header ? *header : NULL;
}

size_t AsyncWebServerRequest::params() const {
  return _params.length();
}

bool AsyncWebServerRequest::hasParam(const String& name, bool post, bool file) const {
  for(const auto& p: _params){
    if(p->name() == name && p->isPost() == post && p->isFile() == file)
      return true;
  }
  return false;
}

AsyncWebParameter* AsyncWebServerRequest::getParam(const String& name, bool post, bool file) const {
  for(auto& p: _params){
    if(p->name() == name && p->isPost() == post && p->isFile() == file)
      return p;
  }
  return NULL;
}

AsyncWebParameter* AsyncWebServerRequest::getParam(size_t num) const {
  auto param = _params.nth(num);
  return param ? *param : NULL;
}

void AsyncWebServerRequest::addInterestingHeader(const String& name){
  if (name != "*") {
    if(!_interestingHeaders.containsIgnoreCase(name))
      _interestingHeaders.add(name);
  } else {
    _interestingHeaders.free();
    _wantAllHeaders = true;
  }
}

void AsyncWebServerRequest::send(AsyncWebServerResponse *response){
  _response = response;
  if(_response == NULL){
    _client->close(true);
    _onDisconnect();
    return;
  }
  _client->setRxTimeout(0);
  _response->_respond(this);
}

AsyncWebServerResponse * AsyncWebServerRequest::beginResponse(int code, const String& content, const String& contentType){
  return content.empty()? (AsyncWebServerResponse*) new AsyncSimpleResponse(code)
                        : new AsyncStringResponse(code, content, contentType);
}

AsyncWebServerResponse * AsyncWebServerRequest::beginResponse(FS &fs, const String& path, const String& contentType,
                                                              bool download){
  return new AsyncFileResponse(fs, path, contentType, download);
}

AsyncWebServerResponse * AsyncWebServerRequest::beginResponse(File content, const String& path, const String& contentType,
                                                              bool download){
  return new AsyncFileResponse(content, path, contentType, download);
}

AsyncWebServerResponse * AsyncWebServerRequest::beginResponse(int code, Stream &content, const String& contentType, size_t len){
  return new AsyncStreamResponse(code, content, contentType, len);
}

AsyncWebServerResponse * AsyncWebServerRequest::beginResponse(int code, AwsResponseFiller callback, const String& contentType,
                                                              size_t len){
  return new AsyncCallbackResponse(code, callback, contentType, len);
}

AsyncWebServerResponse * AsyncWebServerRequest::beginChunkedResponse(int code, AwsResponseFiller callback, const String& contentType){
  return _version? (AsyncWebServerResponse*) new AsyncChunkedResponse(code, callback, contentType)
                 : new AsyncCallbackResponse(code, callback, contentType, -1);
}

AsyncPrintResponse * AsyncWebServerRequest::beginPrintResponse(int code, const String& contentType){
  return new AsyncPrintResponse(code, contentType);
}

AsyncWebServerResponse * AsyncWebServerRequest::beginResponse_P(int code, const uint8_t * content, const String& contentType,
                                                                size_t len){
  return new AsyncProgmemResponse(code, content, contentType, len);
}

AsyncWebServerResponse * AsyncWebServerRequest::beginResponse_P(int code, PGM_P content, const String& contentType){
  return beginResponse_P(code, (const uint8_t *)content, contentType, strlen_P(content));
}

void AsyncWebServerRequest::redirect(const String& url){
  AsyncWebServerResponse * response = beginResponse(302);
  response->addHeader("Location",url);
  send(response);
}

bool AsyncWebServerRequest::authenticate(const char *username, const char *realm, const char *password, bool passwordIsHash){
  if(_authorization){
    if(_isDigest)
      return checkDigestAuthentication(_authorization.begin(), methodToString(), username, password,
                                       realm, passwordIsHash, NULL, NULL, NULL);
    else if(!passwordIsHash)
      return checkBasicAuthentication(_authorization.c_str(), username, password);
    else
      return _authorization.equals(password);
  }
  return false;
}

bool AsyncWebServerRequest::authenticate(const char *hash){
  if(!_authorization || hash == NULL)
    return false;

  char *username = NULL, *realm = NULL;

  if(_isDigest) {
    String hashstr(hash);

    char* ptr = username = hashstr.begin();
    while (*ptr != ':') if (!*++ptr) break;
    if (!*ptr) return false;
    *ptr++ = '\0';

    realm = ptr;
    while (*ptr != ':') if (!*++ptr) break;
    if (!*ptr) return false;
    *ptr++ = '\0';

    hash = ptr;
  }

  return authenticate(username, realm, hash, true);
}

void AsyncWebServerRequest::requestAuthentication(const char * realm, bool isDigest){
  AsyncWebServerResponse * r = beginResponse(401);
  if(!isDigest && realm == NULL){
    r->addHeader("WWW-Authenticate", "Basic realm=\"Login Required\"");
  } else if(!isDigest){
    String header = "Basic realm=\"";
    header.concat(realm);
    header.concat('"');
    r->addHeader("WWW-Authenticate", header);
  } else {
    String header = "Digest ";
    header.concat(requestDigestAuthentication(realm));
    r->addHeader("WWW-Authenticate", header);
  }
  send(r);
}

bool AsyncWebServerRequest::hasArg(const char* name) const {
  for(const auto& arg: _params){
    if(arg->name() == name)
      return true;
  }
  return false;
}

const String& AsyncWebServerRequest::arg(const String& name) const {
  for(const auto& arg: _params){
    if(arg->name() == name)
      return arg->value();
  }
  return SharedEmptyString;
}

const String& AsyncWebServerRequest::arg(size_t i) const {
  return getParam(i)->value();
}

const String& AsyncWebServerRequest::argName(size_t i) const {
  return getParam(i)->name();
}

const String& AsyncWebServerRequest::header(const char* name) const {
  AsyncWebHeader* h = getHeader(String(name));
  return h ? h->value() : SharedEmptyString;
}

const String& AsyncWebServerRequest::header(size_t i) const {
  AsyncWebHeader* h = getHeader(i);
  return h ?  h->value() : SharedEmptyString;
}

const String& AsyncWebServerRequest::headerName(size_t i) const {
  AsyncWebHeader* h = getHeader(i);
  return h ? h->name() : SharedEmptyString;
}

String AsyncWebServerRequest::urlDecode(const String& text) const {
  char temp[] = "0x00";
  unsigned int len = text.length();
  unsigned int i = 0;
  String decoded = String();
  decoded.reserve(len); // Allocate the string internal buffer - never longer from source text
  while (i < len){
    char decodedChar;
    char encodedChar = text.charAt(i++);
    if ((encodedChar == '%') && (i + 1 < len)){
      temp[2] = text.charAt(i++);
      temp[3] = text.charAt(i++);
      decodedChar = strtol(temp, NULL, 16);
    } else if (encodedChar == '+') {
      decodedChar = ' ';
    } else {
      decodedChar = encodedChar;  // normal ascii char
    }
    decoded.concat(decodedChar);
  }
  return decoded;
}


const char * AsyncWebServerRequest::methodToString() const {
  if(_method == HTTP_ANY) return "ANY";
  else if(_method & HTTP_GET) return "GET";
  else if(_method & HTTP_POST) return "POST";
  else if(_method & HTTP_DELETE) return "DELETE";
  else if(_method & HTTP_PUT) return "PUT";
  else if(_method & HTTP_PATCH) return "PATCH";
  else if(_method & HTTP_HEAD) return "HEAD";
  else if(_method & HTTP_OPTIONS) return "OPTIONS";
  return "UNKNOWN";
}
