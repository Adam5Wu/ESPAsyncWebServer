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

#include "WebResponseImpl.h"

extern "C" {
  #include "lwip/opt.h"
  #include "user_interface.h"
}

static String _PlatformAnnotation;

String const& GetPlatformAnnotation(void) {
  if (_PlatformAnnotation.empty()) {
#if defined(ESP8266)
    _PlatformAnnotation.concat("ESP8266 NonOS-", 14);
    _PlatformAnnotation.concat(system_get_sdk_version());
    _PlatformAnnotation.concat(" ID#", 4);
    _PlatformAnnotation.concat(system_get_chip_id(), 16);
    _PlatformAnnotation.replace('(','[');
    _PlatformAnnotation.replace(')',']');
#endif
  }
  return _PlatformAnnotation;
}

/*
 * Abstract Response
 * */

AsyncWebResponse::AsyncWebResponse(int code)
  : _code(code)
  , _state(RESPONSE_SETUP)
  , _request(NULL)
{}

const char* AsyncWebResponse::_responseCodeToString() {
  switch (_code) {
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
    default:  return "???";
  }
}

void AsyncWebResponse::setCode(int code) {
  MUSTNOTSTART();
  _code = code;
}

void AsyncWebResponse::_respond(AsyncWebRequest &request) {
  _request = &request;
  // Disable connection keep-alive if response is an error
  if (_code < 200 || _code >= 400) request.noKeepAlive();
}

ESPWS_DEBUGDO(const char* AsyncWebResponse::_stateToString(void) const {
  switch (_state) {
    case RESPONSE_SETUP: return "Setup";
    case RESPONSE_HEADERS: return "Headers";
    case RESPONSE_CONTENT: return "Content";
    case RESPONSE_WAIT_ACK: return "WaitAck";
    case RESPONSE_END: return "End";
    case RESPONSE_FAILED: return "Failed";
    default: return "???";
  }
})

/*
 * Simple (no content) Response
 * */

AsyncSimpleResponse::AsyncSimpleResponse(int code)
  : AsyncWebResponse(code)
  //, _status()
  //, _headers()
  , _sendbuf(NULL)
  , _bufLen(0)
  , _bufSent(0)
  , _bufPrepared(0)
  , _inFlightLength(0)
{}

void AsyncSimpleResponse::_respond(AsyncWebRequest &request) {
  AsyncWebResponse::_respond(request);

  if (_state == RESPONSE_SETUP) {
    ESPWS_LOG("[%s:%d] %d %s %s %s\n", request._client.remoteIP().toString().c_str(), request._client.remotePort(),
              _code, request.methodToString(), request.host().c_str(), request.url().c_str());

    _assembleHead();
    _state = RESPONSE_HEADERS;
    // ASSUMPTION: status line is ALWAYS shorter than TCP_SND_BUF
    // TRUE with current implementation (TCP_SND_BUF = 2*TCP_MSS, and TCP_MSS = 1460)
    _sendbuf = (uint8_t*)_status.begin();
    _bufLen = _status.length();
    _kickstart();
  } else {
    ESPWS_DEBUG("[%s] Unexpected response state: %s\n", _request->_remoteIdent.c_str(), _stateToString());
    _state = RESPONSE_FAILED;
  }
}

void AsyncSimpleResponse::_assembleHead(void) {
  uint8_t version = _request->version();
  if (!_request->keepAlive()) {
    addHeader("Connection", "close");
  } else if (!version) {
    addHeader("Connection", "keep-alive");
  }

  ESPWS_DEBUGVV("[%s]--- Headers Start ---\n%s[%s]--- Headers End ---\n", _request->_remoteIdent.c_str(),
                _headers.c_str(), _request->_remoteIdent.c_str());

  _status.concat("HTTP/1.",7);
  _status.concat(version);
  _status.concat(' ');
  _status.concat(_code);
  _status.concat(' ');
  _status.concat(_responseCodeToString());
  // Generate server header
  _status.concat("\r\nServer: ",10);
  _status.concat(_request->_server.VERTOKEN);
  _status.concat(" (",2);
  _status.concat(GetPlatformAnnotation());
  _status.concat(")\r\n",3);

  _headers.concat("\r\n",2);
}

void AsyncSimpleResponse::addHeader(const char *name, const char *value) {
  MUSTNOTSTART();

  _headers.concat(name);
  _headers.concat(": ",2);
  _headers.concat(value);
  _headers.concat("\r\n",2);
}

void AsyncSimpleResponse::_ack(size_t len, uint32_t time) {
  _inFlightLength -= len;
  if (_waitack() && !_inFlightLength) {
    // All data acked, now we are done!
    ESPWS_DEBUGV("[%s] All data acked, finalizing\n", _request->_remoteIdent.c_str());
    _requestComplete();
  }
}

size_t AsyncSimpleResponse::_process(size_t resShare) {
  ESPWS_DEBUGVV("[%s] Processing share %d\n", _request->_remoteIdent.c_str(), resShare);
  size_t written = 0;
  while (_sending() && resShare && _prepareSendBuf(resShare)) {
    size_t sendLen = _request->_client.add((const char*)&_sendbuf[_bufSent], _bufLen);
    if (sendLen) {
      ESPWS_DEBUGVV("[%s] Queued %d of %d\n", _request->_remoteIdent.c_str(), sendLen, _bufLen);
      written += sendLen;
      _bufSent += sendLen;
      resShare -= sendLen;
      if (!(_bufLen-= sendLen))
        _releaseSendBuf(true);
    } else {
      ESPWS_DEBUGVV("[%s] Pipe congested, %d share left\n", _request->_remoteIdent.c_str(), resShare);
      break;
    }
  }
  // If all prepared data are sent, no need to occupy memory
  if (!_bufLen) _releaseSendBuf();
  if (written) {
    // ASSUMPTION: No error that concerns us will happen
    // TRUE with current implementation (error code is only possible when nothing to send)
    _request->_client.send();
    _inFlightLength += written;
    ESPWS_DEBUGVV("[%s] In-flight %d\n", _request->_remoteIdent.c_str(), _inFlightLength);
  }
  return written;
}

bool AsyncSimpleResponse::_prepareSendBuf(size_t resShare) {
  while (!_sendbuf) {
    size_t space = _request->_client.space() < resShare? _request->_client.space(): resShare;
    if (space < resShare/2 && space < TCP_MSS/4) {
      // Send buffer too small, wait for it to grow bigger
      ESPWS_DEBUGVV("[%s] Wait for larger send buffer\n", _request->_remoteIdent.c_str());
      break;
    }
    _bufSent = 0;

    if (_state == RESPONSE_HEADERS) {
      _status.clear(true);
      ESPWS_DEBUGVV("[%s] Preparing head @%d\n", _request->_remoteIdent.c_str(), _bufPrepared);
      _prepareHeadSendBuf(space);
    } else if (_state == RESPONSE_CONTENT) {
      _headers.clear(true);
      ESPWS_DEBUGVV("[%s] Preparing content @%d\n", _request->_remoteIdent.c_str(), _bufPrepared);
      _prepareContentSendBuf(space);
    }
    break;
  }
  return !!_sendbuf;
}

void AsyncSimpleResponse::_releaseSendBuf(bool more) {
  if (_state == RESPONSE_HEADERS) {
    if (_bufPrepared >= _headers.length()) {
      _state = RESPONSE_CONTENT;
      _bufPrepared = 0;
      // Probe whether there is any content to send
      _prepareContentSendBuf(0);
    }
  }
  _sendbuf = NULL;
}

void AsyncSimpleResponse::_prepareHeadSendBuf(size_t space) {
  _prepareAllocatedSendBuf((uint8_t*)_headers.begin(), _headers.length(), space);
}

void AsyncSimpleResponse::_prepareContentSendBuf(size_t space) {
  // Implement null content
  ESPWS_DEBUGV("[%s] No content to send\n", _request->_remoteIdent.c_str());
  _state = RESPONSE_WAIT_ACK;
}

void AsyncSimpleResponse::_prepareAllocatedSendBuf(uint8_t const *buf, size_t limit, size_t space) {
  if (space) {
    ESPWS_DEBUGVV("[%s] Preparing static buffer of %d up to %d\n", _request->_remoteIdent.c_str(), limit, space);
    _sendbuf = (uint8_t*)&buf[_bufPrepared];
    size_t bufToSend = limit - _bufPrepared;
    _bufLen = space >= bufToSend? bufToSend: space;
    _bufPrepared+= _bufLen;
  }
}

/*
 * Basic (with concept of typed/sized content) Response
 * */

AsyncBasicResponse::AsyncBasicResponse(int code, const String& contentType)
  : AsyncSimpleResponse(code)
  , _contentType(contentType)
  , _contentLength(-1)
{}

void AsyncBasicResponse::_assembleHead(void) {
  if (_contentLength && _contentLength != -1) {
    addHeader("Content-Length", String(_contentLength).c_str());
    if (_request->version())
      addHeader("Accept-Ranges", "none");
  }
  if (!_contentType.empty()) {
    addHeader("Content-Type", _contentType.c_str());
    _contentType.clear(true);
  } else if (_contentLength && _contentLength != -1) {
    // Make a safe (conservative) guess
    addHeader("Content-Type", "application/octet-stream");
  }

  AsyncSimpleResponse::_assembleHead();
}

void AsyncBasicResponse::_prepareContentSendBuf(size_t space) {
  // While we establish the concept, non-null content is not supported, yet
  while (_contentLength && _contentLength != -1) panic();
  AsyncSimpleResponse::_prepareContentSendBuf(space);
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
 * String Reference Content Response
 * */

AsyncStringRefResponse::AsyncStringRefResponse(int code, const String& content, const String& contentType)
  : AsyncBasicResponse(code, contentType)
  , _content(content)
{
  if (_contentType.empty())
    _contentType = "text/plain";
}

void AsyncStringRefResponse::_assembleHead(void) {
  if (_contentLength == -1) {
    _contentLength = _content.length();
  } else if (_contentLength > _content.length()) {
    ESPWS_DEBUGV("[%s] Corrected content length overshoot %d -> %d\n", _request->_remoteIdent.c_str(),
                 _contentLength, _content.length());
    _contentLength = _content.length();
  }

  AsyncBasicResponse::_assembleHead();
}

void AsyncStringRefResponse::_prepareContentSendBuf(size_t space) {
  if (_bufPrepared >= _contentLength)
    AsyncSimpleResponse::_prepareContentSendBuf(space);
  _prepareAllocatedSendBuf((uint8_t*)_content.begin(), _contentLength, space);
}

/*
 * Printable String Content Response
 * */

size_t AsyncPrintResponse::write(const uint8_t *data, size_t len){
  MUSTNOTSTART();
  return __content.write(data, len);
}

/*
 * Buffered (abstract) Content Response
 * */

#define STAGEBUF_SIZE 512

AsyncBufferedResponse::AsyncBufferedResponse(int code, const String& contentType)
  : AsyncBasicResponse(code, contentType), _stashbuf(NULL)
{}

void AsyncBufferedResponse::_prepareContentSendBuf(size_t space) {
  if (_bufPrepared >= _contentLength)
    AsyncSimpleResponse::_prepareContentSendBuf(space);

  if (space) {
    size_t bufToSend = (_contentLength == -1)? space : _contentLength - _bufPrepared;
    _bufLen = (space < bufToSend)? space : bufToSend;
    ESPWS_DEBUGV("[%s] Preparing %d / %d\n", _request->_remoteIdent.c_str(), _bufLen, bufToSend);

    _sendbuf = _stashbuf? _stashbuf : (_stashbuf = (uint8_t*)malloc(STAGEBUF_SIZE));
    if (_sendbuf) {
      _bufLen = _fillBuffer((uint8_t*)_sendbuf, _bufLen < STAGEBUF_SIZE? _bufLen : STAGEBUF_SIZE);
      _bufPrepared+= _bufLen;
    } else ESPWS_DEBUGV("[%s] Buffer allocation failed!\n", _request->_remoteIdent.c_str());
  }
}

void AsyncBufferedResponse::_releaseSendBuf(bool more) {
  if (_state <= RESPONSE_HEADERS) {
    AsyncSimpleResponse::_releaseSendBuf(more);
    return;
  }
  if (!more) {
    free((void*)_stashbuf);
    _stashbuf = NULL;

    if (_state == RESPONSE_CONTENT) {
      if (_bufPrepared >= _contentLength) {
        _state = RESPONSE_WAIT_ACK;
      }
    }
  }
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
    }

    if (download) {
      size_t filenameStart = path.lastIndexOf('/') + 1;
      char buf[26+path.length()-filenameStart];
      snprintf(buf, sizeof(buf), "attachment; filename=\"%s\"", path.begin()+filenameStart);
      addHeader("Content-Disposition", buf);
    }
  }
}

void AsyncFileResponse::_assembleHead(void) {
  if (_content) {
    if (_contentLength > _content.size() && _contentLength != -1) {
      ESPWS_DEBUGV("[%s] Corrected content length overshoot %d -> %d\n", _request->_remoteIdent.c_str(),
                   _contentLength, _content.size());
      _contentLength = _content.size();
    }
  } else {
    _code = 404;
    _contentLength = 0; // Prevents fillBuffer from being called
    _contentType.clear(true);
    _headers.clear(true);
  }

  AsyncBasicResponse::_assembleHead();
}

size_t AsyncFileResponse::_fillBuffer(uint8_t *buf, size_t maxLen) {
  size_t outLen = _content.read(buf, maxLen);
  ESPWS_DEBUGVV("[%s] File read up to %d, got %d\n", _request->_remoteIdent.c_str(), maxLen, outLen);
  // Unsized content stop condition
  if (_contentLength == -1 && !outLen) {
    _contentLength = 0; // Stops fillBuffer from being called again
    _state = RESPONSE_WAIT_ACK;
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

size_t AsyncStreamResponse::_fillBuffer(uint8_t *buf, size_t maxLen) {
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

size_t AsyncProgmemResponse::_fillBuffer(uint8_t *buf, size_t maxLen) {
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

size_t AsyncCallbackResponse::_fillBuffer(uint8_t *buf, size_t maxLen) {
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

void AsyncChunkedResponse::_assembleHead(void){
  if (_request->version()) {
    addHeader("Transfer-Encoding", "chunked");
  } else {
    _code = 505;
    _contentLength = 0; // Prevents fillBuffer from being called
    _contentType.clear(true);
    _headers.clear(true);
  }

  AsyncBasicResponse::_assembleHead();
}

void AsyncChunkedResponse::_prepareContentSendBuf(size_t space) {
  // Make sure the buffer we are going to prepare is reasonable
  if (space <= 32) return; // Too small to worth the effort
  if (space > 0x2000) space = 0x2000; // Too big to work with (don't want to starve others!)

  AsyncBufferedResponse::_prepareContentSendBuf(space);
}

size_t AsyncChunkedResponse::_fillBuffer(uint8_t *buf, size_t maxLen){
  size_t chunkLen = _callback(buf+6, maxLen-8, _bufPrepared-(8*_chunkCnt));
  // Encapsulate chunk
  buf[0] = HexLookup[(chunkLen >> 12) & 0xF];
  buf[1] = HexLookup[(chunkLen >> 8) & 0xF];
  buf[2] = HexLookup[(chunkLen >> 4) & 0xF];
  buf[3] = HexLookup[(chunkLen >> 0) & 0xF];
  buf[4] = '\r';
  buf[5] = '\n';
  buf[6+chunkLen] = '\r';
  buf[6+chunkLen+1] = '\n';
  // Check for termination signal
  if (!chunkLen) {
    _contentLength = 0; // Stops fillBuffer from being called again
    _state = RESPONSE_WAIT_ACK;
  }
  return chunkLen+8;
}
