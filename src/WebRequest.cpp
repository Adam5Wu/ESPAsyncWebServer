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

#ifndef ESP8266
#define os_strlen strlen
#endif

static const String SharedEmptyString = String();

#define __is_param_char(c) ((c) && ((c) != '{') && ((c) != '[') && ((c) != '&') && ((c) != '='))

enum { PARSE_REQ_START, PARSE_REQ_HEADERS, PARSE_REQ_BODY, PARSE_REQ_END, PARSE_REQ_FAIL };

AsyncWebServerRequest::AsyncWebServerRequest(AsyncWebServer* s, AsyncClient* c)
  : _client(c)
  , _server(s)
  , _handler(NULL)
  , _response(NULL)
  , _temp()
  , _parseState(0)
  , _version(0)
  , _method(HTTP_ANY)
  , _url()
  , _host()
  , _contentType()
  , _boundary()
  , _authorization()
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
  , _itemName()
  , _itemFilename()
  , _itemType()
  , _itemValue()
  , _itemBuffer(0)
  , _itemBufferIndex(0)
  , _itemIsFile(false)
  ESPWS_DEBUGDO(, _remoteIdent(c->remoteIP().toString()+':'+c->remotePort()))
{
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
  _headers.free();
  _params.free();
  _interestingHeaders.free();

  if(_response != NULL){
    delete _response;
  }
}

void AsyncWebServerRequest::_onData(void *buf, size_t len){
  while (true) {
    if(_parseState < PARSE_REQ_BODY){
      // Find new line in buf
      char *str = (char*)buf;
      size_t i = 0;
      for (; i < len; i++) if (str[i] == '\n') break;
      if (i == len) { // No new line, just add the buffer in _temp
        char ch = str[len-1];
        str[len-1] = 0;
        _temp.reserve(_temp.length()+len);
        _temp.concat(str);
        _temp.concat(ch);
      } else { // Found new line - extract it and parse
        str[i] = 0; // Terminate the string at the end of the line.
        _temp.concat(str);
        _temp.trim();
        _parseLine();
        if (++i < len) {
          // Still have more buffer to process
          // Iterative instead to recursive
          //_onData(str+i, len-i);
          buf = str+i;
          len-= i;
          continue;
        }
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
          } else if(_contentType == "text/plain" && __is_param_char(((char*)buf)[0])){
            size_t i = 0;
            while (i<len && __is_param_char(((char*)buf)[i++]));
            if(i < len && ((char*)buf)[i-1] == '='){
              _isPlainPost = true;
            }
          }
        }
        if(!_isPlainPost) {
          //check if authenticated before calling the body
          if(_handler) _handler->handleBody(this, (uint8_t*)buf, len, _parsedLength, _contentLength);
          _parsedLength += len;
        } else {
          size_t i;
          for(i=0; i<len; i++){
            _parsedLength++;
            _parsePlainPostChar(((uint8_t*)buf)[i]);
          }
        }
      }

      if(_parsedLength == _contentLength){
        _temp.clear(true);
        _parseState = PARSE_REQ_END;
        //check if authenticated before calling handleRequest and request auth instead
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
  ESPWS_DEBUG("[%s] DISCONNECT, response state: %s\n", _remoteIdent.c_str(), _response? _response->stateToString() : "NULL");
  _server->_handleDisconnect(this);
}

void AsyncWebServerRequest::_addParam(AsyncWebParameter *p){
  _params.add(p);
}

void AsyncWebServerRequest::_addGetParams(const String& params){
  size_t start = 0;
  while (start < params.length()){
    int end = params.indexOf('&', start);
    if (end < 0) end = params.length();
    int equal = params.indexOf('=', start);
    if (equal < 0 || equal > end) equal = end;
    String name = params.substring(start, equal);
    String value = equal + 1 < end ? params.substring(equal + 1, end) : String();
    _addParam(new AsyncWebParameter(name, value));
    start = end + 1;
  }
}

bool AsyncWebServerRequest::_parseReqHead(){
  // Split the head into method, url and version
  int index = _temp.indexOf(' ');
  String m = _temp.substring(0, index);
  index = _temp.indexOf(' ', index+1);
  String u = _temp.substring(m.length()+1, index);
  _temp = _temp.substring(index+1);

  if(m == "GET"){
    _method = HTTP_GET;
  } else if(m == "POST"){
    _method = HTTP_POST;
  } else if(m == "DELETE"){
    _method = HTTP_DELETE;
  } else if(m == "PUT"){
    _method = HTTP_PUT;
  } else if(m == "PATCH"){
    _method = HTTP_PATCH;
  } else if(m == "HEAD"){
    _method = HTTP_HEAD;
  } else if(m == "OPTIONS"){
    _method = HTTP_OPTIONS;
  }

  u = urlDecode(u);
  String g = String();
  index = u.indexOf('?');
  if(index > 0){
    g = u.substring(index +1);
    u = u.substring(0, index);
  }
  _url = u;
  _addGetParams(g);

  if(!_temp.startsWith("HTTP/1.0"))
    _version = 1;

  _temp.clear();
  return true;
}

bool AsyncWebServerRequest::_parseReqHeader(){
  int index = _temp.indexOf(':');
  if(index){
    String name = _temp.substring(0, index);
    uint8_t valofs = index + 1;
    while (_temp[valofs] == ' ') valofs++;
    if(name.equalsIgnoreCase("Host")){
      _host = _temp.substring(valofs);
      _server->_rewriteRequest(this);
      _server->_attachHandler(this);
    } else if(name.equalsIgnoreCase("Content-Type")){
      String value = _temp.substring(valofs);
      if (value.startsWith("multipart/")){
        _boundary = value.substring(value.indexOf('=')+1);
        _contentType = value.substring(0, value.indexOf(';'));
        _isMultipart = true;
      } else {
        _contentType = value;
      }
    } else if(name.equalsIgnoreCase("Content-Length")){
      _contentLength = atoi(&_temp[valofs]);
    } else if(name.equalsIgnoreCase("Expect")) {
      String value = _temp.substring(valofs);
      if (value == "100-continue") {
        _expectingContinue = true;
      }
    } else if(name.equalsIgnoreCase("Authorization")){
      String value = _temp.substring(valofs);
      if(value.length() > 5 && value.substring(0,5).equalsIgnoreCase("Basic")){
        _authorization = value.substring(6);
      } else if(value.length() > 6 && value.substring(0,6).equalsIgnoreCase("Digest")){
        _isDigest = true;
        _authorization = value.substring(7);
      }
    } else {
      if(_interestingHeaders.containsIgnoreCase(name) || _interestingHeaders.containsIgnoreCase("ANY")){
        String value = _temp.substring(valofs);
        _headers.add(new AsyncWebHeader(std::move(name), std::move(value)));
      }
    }
  }
  _temp.clear();
  return true;
}

void AsyncWebServerRequest::_parsePlainPostChar(uint8_t data){
  if(data && (char)data != '&')
    _temp += (char)data;
  if(!data || (char)data == '&' || _parsedLength == _contentLength){
    _temp = urlDecode(_temp);
    String name;
    String value;
    if(!_temp[0] == '{' && !_temp[0] == '[' && _temp.indexOf('=') > 0){
      value = _temp.substring(_temp.indexOf('=') + 1);
      _temp.remove(_temp.indexOf('='));
      name = std::move(_temp);
    } else {
      name = "body";
      value = std::move(_temp);
    }
    _addParam(new AsyncWebParameter(std::move(name), std::move(value), true));
    //_temp.clear();
  }
}

void AsyncWebServerRequest::_handleUploadByte(uint8_t data, bool last){
  _itemBuffer[_itemBufferIndex++] = data;

  if(last || _itemBufferIndex == 1460){
    //check if authenticated before calling the upload
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
#define itemWriteByte(b) do { _itemSize++; if(_itemIsFile) _handleUploadByte(b, last); else _itemValue+=(char)(b); } while(0)

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
          if(_itemBuffer)
            free(_itemBuffer);
          _itemBuffer = (uint8_t*)malloc(1460);
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
          //check if authenticated before calling the upload
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

static const char response[] = "HTTP/1.1 100 Continue\r\n\r\n";

void AsyncWebServerRequest::_parseLine(){
  if(_parseState == PARSE_REQ_START){
    if(_temp.empty()){
      _parseState = PARSE_REQ_FAIL;
      _client->close();
    } else {
      _parseReqHead();
      _parseState = PARSE_REQ_HEADERS;
    }
    return;
  }

  if(_parseState == PARSE_REQ_HEADERS){
    if(_temp.empty()){
      //end of headers
      if(_expectingContinue)
        _client->write(response, sizeof(response));
      //check handler for authentication
      if(_contentLength){
        _parseState = PARSE_REQ_BODY;
      } else {
        _parseState = PARSE_REQ_END;
        if(_handler) _handler->handleRequest(this);
        else send(501);
      }
    } else _parseReqHeader();
  }
}

size_t AsyncWebServerRequest::headers() const{
  return _headers.length();
}

bool AsyncWebServerRequest::hasHeader(const String& name) const {
  for(const auto& h: _headers){
    if(h->name().equalsIgnoreCase(name)){
      return true;
    }
  }
  return false;
}

AsyncWebHeader* AsyncWebServerRequest::getHeader(const String& name) const {
  for(const auto& h: _headers){
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
    if(p->name() == name && p->isPost() == post && p->isFile() == file){
      return true;
    }
  }
  return false;
}

AsyncWebParameter* AsyncWebServerRequest::getParam(const String& name, bool post, bool file) const {
  for(const auto& p: _params){
    if(p->name() == name && p->isPost() == post && p->isFile() == file){
      return p;
    }
  }
  return NULL;
}

AsyncWebParameter* AsyncWebServerRequest::getParam(size_t num) const {
  auto param = _params.nth(num);
  return param ? *param : NULL;
}

void AsyncWebServerRequest::addInterestingHeader(const String& name){
  if(!_interestingHeaders.containsIgnoreCase(name))
    _interestingHeaders.add(name);
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

AsyncWebServerResponse * AsyncWebServerRequest::beginChunkedResponse(int code, AwsResponseFiller callback,
                                                                     const String& contentType){
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

bool AsyncWebServerRequest::authenticate(const char * username, const char * password, const char * realm, bool passwordIsHash){
  if(_authorization){
    if(_isDigest)
      return checkDigestAuthentication(_authorization.c_str(), methodToString(), username, password,
                                       realm, passwordIsHash, NULL, NULL, NULL);
    else if(!passwordIsHash)
      return checkBasicAuthentication(_authorization.c_str(), username, password);
    else
      return _authorization.equals(password);
  }
  return false;
}

bool AsyncWebServerRequest::authenticate(const char * hash){
  if(!_authorization || hash == NULL)
    return false;

  if(_isDigest){
    String hStr = String(hash);
    int separator = hStr.indexOf(':');
    if(separator <= 0)
      return false;
    String username = hStr.substring(0, separator);
    hStr = hStr.substring(separator + 1);
    separator = hStr.indexOf(':');
    if(separator <= 0)
      return false;
    String realm = hStr.substring(0, separator);
    hStr = hStr.substring(separator + 1);
    return checkDigestAuthentication(_authorization.c_str(), methodToString(), username.c_str(), hStr.c_str(),
                                     realm.c_str(), true, NULL, NULL, NULL);
  }

  return (_authorization.equals(hash));
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
    if(arg->name() == name){
      return true;
    }
  }
  return false;
}

const String& AsyncWebServerRequest::arg(const String& name) const {
  for(const auto& arg: _params){
    if(arg->name() == name){
      return arg->value();
    }
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
