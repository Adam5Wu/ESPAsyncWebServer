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

extern "C" {
  #include "lwip/opt.h"
}

#define MIN_FREE_HEAP  8192

/*
 * Abstract Response
 * */

AsyncWebServerResponse::AsyncWebServerResponse(int code)
  : _code(code)
  , _headers(LinkedList<AsyncWebHeader *>([](AsyncWebHeader *h){ delete h; }))
  , _state(RESPONSE_SETUP)
{}

const char* AsyncWebServerResponse::_responseCodeToString(int code) {
  switch (code) {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 307: return "Temporary Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Time-out";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Request Entity Too Large";
    case 414: return "Request-URI Too Large";
    case 415: return "Unsupported Media Type";
    case 416: return "Requested range not satisfiable";
    case 417: return "Expectation Failed";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Time-out";
    case 505: return "HTTP Version not supported";
    default:  return "";
  }
}

void AsyncWebServerResponse::setCode(int code) {
  MUSTNOTSTART();
  _code = code;
}

void AsyncWebServerResponse::addHeader(const String& name, const String& value) {
  MUSTNOTSTART();
  _headers.add(new AsyncWebHeader(name, value));
}

/*
 * Simple (no content) Response
 * */

AsyncSimpleResponse::AsyncSimpleResponse(int code)
  : AsyncWebServerResponse(code)
  //, _head()
  , _sendbuf(nullptr)
  , _bufLen(0)
  , _bufSent(0)
  , _bufPrepared(0)
  , _inFlightLength(0)
{}

void AsyncSimpleResponse::_respond(AsyncWebServerRequest *request) {
  assembleHead(request->version());
  _state = RESPONSE_HEADERS;
  _ack(request, 0, 0);
}

void AsyncSimpleResponse::assembleHead(uint8_t version) {
  if (version)
    addHeader("Connection", "close");

  _head.printf("HTTP/1.%d %d %s\r\n", version, _code, _responseCodeToString(_code));

  for(const auto& header: _headers)
    _head.printf("%s: %s\r\n", header->name().c_str(), header->value().c_str());
  _headers.free();

  _head.print("\r\n");
  DEBUGV("[AsyncSimpleResponse::assembleHead] %s",_head.c_str());
}

size_t AsyncSimpleResponse::_ack(AsyncWebServerRequest *request, size_t len, uint32_t time) {
  _inFlightLength -= len;
  size_t written = 0;
  while (_sending() && (ESP.getFreeHeap() > MIN_FREE_HEAP) && prepareSendBuf(request)) {
    size_t sendLen = request->client()->write((const char*)&_sendbuf[_bufSent], _bufLen);
    if (sendLen) {
      DEBUGV("[AsyncSimpleResponse::_ack] {%s:%d} Sent out %d of %d\n",
             request->client()->remoteIP().toString().c_str(),request->client()->remotePort(),
             sendLen,_bufLen);
      written += sendLen;
      _bufSent += sendLen;
      if (!(_bufLen-= sendLen))
        releaseSendBuf();
    } else break;
  }
  _inFlightLength += written;
  if (_waitack() && !_inFlightLength) {
    // All data acked, now we are done!
    DEBUGV("[AsyncSimpleResponse::_ack] {%s:%d} All data acked\n",
           request->client()->remoteIP().toString().c_str(),request->client()->remotePort());
    _state = RESPONSE_END;
  }
  if (_finished()) {
    DEBUGV("[AsyncSimpleResponse::_ack] {%s:%d} Finalizing connection\n",
           request->client()->remoteIP().toString().c_str(),request->client()->remotePort());
    requestCleanup(request);
  }
  return written;
}

bool AsyncSimpleResponse::prepareSendBuf(AsyncWebServerRequest *request) {
  while (!_sendbuf) {
    size_t space = request->client()->space();
    if (space < TCP_MSS/4) {
      // Send buffer too small, wait for it to grow bigger
      DEBUGV("[AsyncSimpleResponse::prepareSendBuf] {%s:%d} Wait for more send buffer\n",
             request->client()->remoteIP().toString().c_str(),request->client()->remotePort());
      break;
    }
    _bufSent = 0;

    if (_state == RESPONSE_HEADERS) {
      DEBUGV("[AsyncSimpleResponse::prepareSendBuf] {%s:%d} Preparing head @%d\n",
             request->client()->remoteIP().toString().c_str(),request->client()->remotePort(),
             _bufPrepared);
      if (prepareHeadSendBuf(space))
        break;
    }

    if (_state == RESPONSE_CONTENT) {
      DEBUGV("[AsyncSimpleResponse::prepareSendBuf] {%s:%d} Preparing content @%d\n",
             request->client()->remoteIP().toString().c_str(),request->client()->remotePort(),
             _bufPrepared);
      prepareContentSendBuf(space);
    }

    break;
  }
  return !!_sendbuf;
}

bool AsyncSimpleResponse::prepareHeadSendBuf(size_t space) {
  if (_head.empty()) {
    _head.clear(true);
    _state = RESPONSE_CONTENT;
    return false;
  }

  if (prepareAllocatedSendBuf((uint8_t*)_head.begin(), _head.length(), space)) {
    DEBUGV("[AsyncSimpleResponse::prepareHeadSendBuf] Head all prepared @%d\n",_bufPrepared+_bufLen);
    _head.clear();
    _bufPrepared = 0;
  }
  return true;
}

bool AsyncSimpleResponse::prepareContentSendBuf(size_t space) {
  // Implement null content
  DEBUGV("[AsyncSimpleResponse::prepareContentSendBuf] Content all prepared @%d\n",_bufPrepared+_bufLen);
  _state = RESPONSE_WAIT_ACK;
  return false;
}

bool AsyncSimpleResponse::prepareAllocatedSendBuf(uint8_t const *buf, size_t limit, size_t space) {
  DEBUGV("[AsyncSimpleResponse::prepareAllocatedSendBuf] Preparing on buffer of %d up to %d\n",limit,space);
  _sendbuf = (uint8_t*)&buf[_bufPrepared];
  size_t bufToSend = limit - _bufPrepared;
  if (space >= bufToSend) {
    _bufLen = bufToSend;
    return true;
  } else _bufLen = space;
  _bufPrepared+= _bufLen;
  return false;
}

/*
 * Basic (with concept of typed/sized content) Response
 * */

AsyncBasicResponse::AsyncBasicResponse(int code, const String& contentType)
  : AsyncSimpleResponse(code)
  , _contentType(contentType)
  , _contentLength(-1)
{}

void AsyncBasicResponse::assembleHead(uint8_t version) {
  if (_contentLength && _contentLength != -1) {
    addHeader("Content-Length", String(_contentLength));
    if (version)
      addHeader("Accept-Ranges", "none");
  }
  if (!_contentType.empty()) {
    addHeader("Content-Type", _contentType);
    _contentType.clear(true);
  } else if (_contentLength && _contentLength != -1) {
    // Make a safe (conservative) guess
    addHeader("Content-Type", "application/octet-stream");
  }

  AsyncSimpleResponse::assembleHead(version);
}

bool AsyncBasicResponse::prepareContentSendBuf(size_t space) {
  // While we establish the concept, non-null content is not supported, yet
  while (_contentLength && _contentLength != -1) panic();
  return AsyncSimpleResponse::prepareContentSendBuf(space);
}

void AsyncBasicResponse::setContentLength(size_t len) {
  MUSTNOTSTART();
  _contentLength = len;
}

void AsyncBasicResponse::setContentType(const String& type) {
  MUSTNOTSTART();
  _contentType = type;
}

/*
 * String Content Response
 * */

AsyncStringResponse::AsyncStringResponse(int code, const String& content, const String& contentType)
  : AsyncBasicResponse(code, contentType)
  , _content(content)
{
  _contentLength = content.length();
  if (_contentType.empty())
    _contentType = "text/plain";
}

void AsyncStringResponse::assembleHead(uint8_t version) {
  if (_contentLength > _content.length() && _contentLength != -1) {
    DEBUGV("[AsyncStringResponse::assembleHead] Correct content length overshoot %d -> %d\n",_contentLength,_content.length());
    _contentLength = _content.length();
  }

  AsyncBasicResponse::assembleHead(version);
}

bool AsyncStringResponse::prepareContentSendBuf(size_t space) {
  if (_content.empty()) {
    _content.clear(true);
    return AsyncSimpleResponse::prepareContentSendBuf(space);
  }

  if (prepareAllocatedSendBuf((uint8_t*)_content.begin(), _contentLength, space)) {
    _content.clear();
    _bufPrepared = 0;
  }
  return true;
}

/*
 * Printable String Content Response
 * */

AsyncPrintResponse::AsyncPrintResponse(int code, const String& contentType)
  : AsyncBasicResponse(code, contentType)
  //, _content()
{
}

void AsyncPrintResponse::assembleHead(uint8_t version) {
  if (_contentLength > _content.length() && _contentLength != -1) {
    DEBUGV("[AsyncPrintResponse::assembleHead] Correct content length overshoot %d -> %d\n",_contentLength,_content.length());
    _contentLength = _content.length();
  }

  AsyncBasicResponse::assembleHead(version);
}

bool AsyncPrintResponse::prepareContentSendBuf(size_t space) {
  if (_content.empty()) {
    _content.clear(true);
    return AsyncSimpleResponse::prepareContentSendBuf(space);
  }

  if (prepareAllocatedSendBuf((uint8_t*)_content.begin(), _contentLength, space)) {
    _content.clear();
    _bufPrepared = 0;
  }
  return true;
}

size_t AsyncPrintResponse::write(const uint8_t *data, size_t len){
  MUSTNOTSTART();
  return _content.write(data, len);
}

/*
 * Buffered (abstract) Content Response
 * */

AsyncBufferedResponse::AsyncBufferedResponse(int code, const String& contentType)
  : AsyncBasicResponse(code, contentType)
{}

bool AsyncBufferedResponse::prepareContentSendBuf(size_t space) {
  if (_bufPrepared >= _contentLength)
    return AsyncSimpleResponse::prepareContentSendBuf(space);

  size_t bufToSend = (_contentLength == -1)? space : _contentLength - _bufPrepared;
  _bufLen = (space < bufToSend)? space : bufToSend;
  DEBUGV("[AsyncBufferedResponse::prepareContentSendBuf] Preparing %d / %d\n",_bufLen,bufToSend);

  _sendbuf = (uint8_t*)malloc(_bufLen);
  if (_sendbuf) {
    _bufLen = fillBuffer((uint8_t*)_sendbuf, _bufLen);
    _bufPrepared+= _bufLen;
  } else {
    DEBUGV("[AsyncBufferedResponse::prepareContentSendBuf] Buffer allocation failed!\n");
  }
  return true;
}

void AsyncBufferedResponse::releaseSendBuf(void) {
  if (_state != RESPONSE_HEADERS)
    free((void*)_sendbuf);
  _sendbuf = NULL;
}

/*
 * File Content Response
 * */

AsyncFileResponse::AsyncFileResponse(File const& content, const String& path, const String& contentType, bool download)
  : AsyncBufferedResponse(200, contentType)
  , _content(content)
{
  if (_content) {
    DEBUGV("[AsyncFileResponse::_prepareServing] File '%s'\n",path.c_str());
    _contentLength = _content.size();

    if (contentType.empty()) {
      int extensionStart = path.lastIndexOf('.')+1;
      String extension = path.begin() + extensionStart;

      if (extension == "htm" || extension == "html") _contentType = "text/html";
      else if (extension == "css") _contentType = "text/css";
      else if (extension == "json") _contentType = "text/json";
      else if (extension == "js") _contentType = "application/javascript";
      else if (extension == "png") _contentType = "image/png";
      else if (extension == "gif") _contentType = "image/gif";
      else if (extension == "jpg" || extension == "jpeg") _contentType = "image/jpeg";
      else if (extension == "ico") _contentType = "image/x-icon";
      else if (extension == "svg") _contentType = "image/svg+xml";
      else if (extension == "eot") _contentType = "font/eot";
      else if (extension == "woff") _contentType = "font/woff";
      else if (extension == "woff2") _contentType = "font/woff2";
      else if (extension == "ttf") _contentType = "font/ttf";
      else if (extension == "xml") _contentType = "text/xml";
      else if (extension == "txt") _contentType = "text/plain";
      else if (extension == "xhtml") _contentType = "application/xhtml+xml";
      else if (extension == "pdf") _contentType = "application/pdf";
      else if (extension == "zip") _contentType = "application/zip";
      else if (extension == "gz") _contentType = "application/x-gzip";
      else _contentType = "application/octet-stream";

      DEBUGV("[AsyncFileResponse::_prepareServing] Extension '%s', MIME '%s'\n",extension.c_str(),_contentType.c_str());
    }

    if (String(_content.name()).endsWith(".gz")) {
      if (_contentType != "application/x-gzip")
        addHeader("Content-Encoding", "gzip");
    }

    if (download) {
      int filenameStart = path.lastIndexOf('/') + 1;
      char* filename = (char*)path.c_str() + filenameStart;
      char buf[26+path.length()-filenameStart];
      snprintf(buf, sizeof (buf), "attachment; filename=\"%s\"", filename);
      addHeader("Content-Disposition", buf);
    }
  }
}

void AsyncFileResponse::assembleHead(uint8_t version) {
  if (_content) {
    if (_contentLength > _content.size() && _contentLength != -1) {
      DEBUGV("[AsyncFileResponse::assembleHead] Correct content length overshoot %d -> %d\n",_contentLength,_content.size());
      _contentLength = _content.size();
    }
  } else {
    _code = 404;
    _contentLength = 0; // Prevents fillBuffer from being called
    _contentType.clear();
  }

  AsyncBasicResponse::assembleHead(version);
}

size_t AsyncFileResponse::fillBuffer(uint8_t *buf, size_t maxLen) {
  size_t outLen = _content.read(buf, maxLen);
  DEBUGV("[AsyncFileResponse::fillBuffer] File read up to %d, got %d\n",maxLen,outLen);
  if (_contentLength == -1 && !outLen) {
    _contentLength = 0; // Stops fillBuffer from being called again
    _state = RESPONSE_WAIT_ACK;
    return 0;
  }
  return outLen;
}

/*
 * Stream Content Response
 * */

AsyncStreamResponse::AsyncStreamResponse(int code, Stream &content, const String& contentType, size_t len)
  : AsyncBufferedResponse(code, contentType)
  , _content(content)
{
  _contentLength = len;
}

size_t AsyncStreamResponse::fillBuffer(uint8_t *buf, size_t maxLen) {
  size_t available = _content.available();
  // Unsized content stop condition
  if (!available && _contentLength == -1) {
    _contentLength = 0; // Stops fillBuffer from being called again
    _state = RESPONSE_WAIT_ACK;
    return 0;
  }
  size_t outLen = (available > maxLen)? maxLen:available;
  return _content.readBytes(buf, outLen);
}

/*
 * ProgMem Content Response
 * */

AsyncProgmemResponse::AsyncProgmemResponse(int code, const uint8_t* content, const String& contentType, size_t len)
  : AsyncBufferedResponse(code, contentType)
  , _content(content)
{
  _contentLength = len;
  // Invalid length value
  if (_contentLength == -1) panic();
}

size_t AsyncProgmemResponse::fillBuffer(uint8_t *buf, size_t maxLen) {
  memcpy_P(buf, _content + _bufPrepared, maxLen);
  return maxLen;
}

/*
 * Callback Content Response
 * */

AsyncCallbackResponse::AsyncCallbackResponse(int code, AwsResponseFiller callback, const String& contentType, size_t len)
  : AsyncBufferedResponse(code, contentType)
  , _callback(callback)
{
  _contentLength = len;
}

size_t AsyncCallbackResponse::fillBuffer(uint8_t *buf, size_t maxLen) {
  size_t outLen = _callback(buf, maxLen, _bufPrepared);
  // Unsized content stop condition
  if (!outLen && _contentLength == -1) {
    _contentLength = 0; // Stops fillBuffer from being called again
    _state = RESPONSE_WAIT_ACK;
    return 0;
  }
  return outLen;
}

/*
 * Chunked (callback) Content Response
 * */

AsyncChunkedResponse::AsyncChunkedResponse(int code, AwsResponseFiller callback, const String& contentType)
  : AsyncBufferedResponse(code, contentType)
  , _callback(callback)
{}

void AsyncChunkedResponse::assembleHead(uint8_t version){
  if (version) {
    addHeader("Transfer-Encoding", "chunked");
  } else {
    _code = 505;
    _contentLength = 0; // Prevents fillBuffer from being called
    _contentType.clear();
    _headers.free();
  }

  AsyncBasicResponse::assembleHead(version);
}

size_t AsyncChunkedResponse::fillBuffer(uint8_t *buf, size_t maxLen){
  if (maxLen <= 32) return 0; // Buffer too small to worth the effort
  if (maxLen > 0x2000+8) maxLen = 0x2000+8;

  size_t chunkLen = _callback(buf+6, maxLen-8, _bufPrepared-(8*_chunkCnt));
  // Encapsulate chunk
  sprintf((char*)buf, "%04x\r\n", chunkLen);
  buf[6+chunkLen] = 0x0D;
  buf[6+chunkLen+1] = 0x0A;
  // Check for termination signal
  if (!chunkLen) {
    _contentLength = 0; // Stops fillBuffer from being called again
    _state = RESPONSE_WAIT_ACK;
  }
  return chunkLen+8;
}