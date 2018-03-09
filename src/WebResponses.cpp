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

String const& GetPlatformSignature(void) {
	static String _PlatformSignature;
	if (!_PlatformSignature) {
#if defined(ESP8266)
		_PlatformSignature.concat(FC("ESP8266 SDK-"));
		_PlatformSignature.concat(system_get_sdk_version());
		_PlatformSignature.concat(FC(" ID#"));
		_PlatformSignature.concat(system_get_chip_id(), 16);
		_PlatformSignature.replace('(','[');
		_PlatformSignature.replace(')',']');
#endif
	}
	return _PlatformSignature;
}

/*
 * Abstract Response
 * */

AsyncWebResponse::AsyncWebResponse(int code)
	: _code(code)
	, _state(RESPONSE_SETUP)
	, _request(nullptr)
{
	// Nothing
}

AsyncWebResponse::~AsyncWebResponse(void) {
	// Nothing
}

PGM_P AsyncWebResponse::_responseCodeToString(void) {
	switch (_code) {
		case 100: return PSTR_C("Continue");
		case 101: return PSTR_C("Switching Protocols");
		case 200: return PSTR_C("OK");
		case 201: return PSTR_C("Created");
		case 202: return PSTR_C("Accepted");
		case 203: return PSTR_C("Non-Authoritative Information");
		case 204: return PSTR_C("No Content");
		case 205: return PSTR_C("Reset Content");
		case 206: return PSTR_C("Partial Content");
#ifdef HANDLE_WEBDAV
		case 207: return PSTR_C("Multi-Status");
#endif
		case 300: return PSTR_C("Multiple Choices");
		case 301: return PSTR_C("Moved Permanently");
		case 302: return PSTR_C("Found");
		case 303: return PSTR_C("See Other");
		case 304: return PSTR_C("Not Modified");
		case 305: return PSTR_C("Use Proxy");
		case 307: return PSTR_C("Temporary Redirect");
		case 400: return PSTR_C("Bad Request");
		case 401: return PSTR_C("Unauthorized");
		case 402: return PSTR_C("Payment Required");
		case 403: return PSTR_C("Forbidden");
		case 404: return PSTR_C("Not Found");
		case 405: return PSTR_C("Method Not Allowed");
		case 406: return PSTR_C("Not Acceptable");
		case 407: return PSTR_C("Proxy Authentication Required");
		case 408: return PSTR_C("Request Time-out");
		case 409: return PSTR_C("Conflict");
		case 410: return PSTR_C("Gone");
		case 411: return PSTR_C("Length Required");
		case 412: return PSTR_C("Precondition Failed");
		case 413: return PSTR_C("Request Entity Too Large");
		case 414: return PSTR_C("Request-URI Too Large");
		case 415: return PSTR_C("Unsupported Media Type");
		case 416: return PSTR_C("Requested range not satisfiable");
		case 417: return PSTR_C("Expectation Failed");
		case 500: return PSTR_C("Internal Server Error");
		case 501: return PSTR_C("Not Implemented");
		case 502: return PSTR_C("Bad Gateway");
		case 503: return PSTR_C("Service Unavailable");
		case 504: return PSTR_C("Gateway Time-out");
		case 505: return PSTR_C("HTTP Version not supported");
		default:  return PSTR_C("? Unknown Status Code ?");
	}
}

void AsyncWebResponse::setCode(int code) {
	if (_started()) {
		ESPWS_LOG("[%s] ERROR: Response already started, cannot change code!\n");
		return;
	}
	_code = code;
}

void AsyncWebResponse::_respond(AsyncWebRequest &request) {
	// Disable connection keep-alive if response is an error or redirection
	if (_code < 200 || _code >= 300 && _code != 304) {
		request.noKeepAlive();
	}

	_request = &request;
}

ESPWS_DEBUGDO(PGM_P AsyncWebResponse::_stateToString(void) const {
	switch (_state) {
		case RESPONSE_SETUP: return PSTR_C("Setup");
		case RESPONSE_HEADERS: return PSTR_C("Headers");
		case RESPONSE_CONTENT: return PSTR_C("Content");
		case RESPONSE_WAIT_ACK: return PSTR_C("WaitAck");
		case RESPONSE_END: return PSTR_C("End");
		case RESPONSE_FAILED: return PSTR_C("Failed");
		default: return PSTR_C("???");
	}
})

/*
 * Simple (no content) Response
 * */

void AsyncSimpleResponse::_respond(AsyncWebRequest &request) {
	AsyncWebResponse::_respond(request);

	if (_state == RESPONSE_SETUP) {
#ifndef HANDLE_AUTHENTICATION
		ESPWS_LOG("[%s:%d] %d %s %s %s\n",
			request._client.remoteIP().toString().c_str(), request._client.remotePort(),
			_code, SFPSTR(request.methodToString()), request.host().c_str(), request.url().c_str());
#else
		ESPWS_LOG("[%s:%d (%s)] %d %s %s %s\n",
			request._client.remoteIP().toString().c_str(), request._client.remotePort(),
			request.session() ? request.session()->IDENT.ID.c_str() : UNKNOWN_ID,
			_code, SFPSTR(request.methodToString()), request.host().c_str(), request.url().c_str());
#endif
		_assembleHead();
		_state = RESPONSE_HEADERS;
		// ASSUMPTION: status line is ALWAYS shorter than TCP_SND_BUF
		// TRUE with current implementation (TCP_SND_BUF = 2*TCP_MSS, and TCP_MSS = 1460)
		_sendbuf = (uint8_t*)_status.begin();
		_bufLen = _status.length();
		_kickstart();
	} else {
		ESPWS_DEBUG("[%s] Unexpected response state: %s\n",
			_request->_remoteIdent.c_str(), SFPSTR(_stateToString()));
		_state = RESPONSE_FAILED;
	}
}

void AsyncSimpleResponse::_assembleHead(void) {
	uint8_t version = _request->version();
	if (!_request->keepAlive()) {
		addHeader(FC("Connection"), FC("close"));
	} else if (!version) {
		addHeader(FC("Connection"), FC("keep-alive"));
	}

	ESPWS_DEBUGVV("[%s]--- Headers Start ---\n%s--- Headers End ---\n",
		_request->_remoteIdent.c_str(), _headers.c_str(), _request->_remoteIdent.c_str());

	_status.concat("HTTP/1.",7);
	_status.concat(version);
	_status.concat(' ');
	_status.concat(_code);
	_status.concat(' ');
	_status.concat(FPSTR(_responseCodeToString()));
	// Generate server header
	_status.concat("\r\nServer: ",10);
	_status.concat(FPSTR(AsyncWebServer::VERTOKEN));
#ifdef PLATFORM_SIGNATURE
	_status.concat(" (",2);
	_status.concat(GetPlatformSignature());
	_status.concat(')');
#endif
	_status.concat("\r\n",2);

	_headers.concat("\r\n",2);
}

void AsyncSimpleResponse::addHeader(String const &name, String const &value) {
	if (_started()) {
		ESPWS_LOG("[%s] ERROR: Response already started, cannot add more header!\n");
		return;
	}

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
		if (_bufLen) {
			size_t sendLen = _request->_client.add((const char*)&_sendbuf[_bufSent], _bufLen);
			if (sendLen) {
				ESPWS_DEBUGVV("[%s] Queued %d of %d\n",
					_request->_remoteIdent.c_str(), sendLen, _bufLen);
				written += sendLen;
				_bufSent += sendLen;
				resShare -= sendLen;
				if (!(_bufLen-= sendLen))
					_releaseSendBuf(true);
			} else {
				ESPWS_DEBUGVV("[%s] Pipe congested, %d share left\n",
					_request->_remoteIdent.c_str(), resShare);
				break;
			}
		} else break;
	}
	// If all prepared data are sent, no need to occupy memory
	if (!_bufLen) _releaseSendBuf();
	if (written) {
		if (!_request->_client.send()) {
			ESPWS_DEBUGVV("[%s] WARNING: TCP send failed!\n", _request->_remoteIdent.c_str());
		} else {
			_inFlightLength += written;
			ESPWS_DEBUGVV("[%s] In-flight %d\n", _request->_remoteIdent.c_str(), _inFlightLength);
		}
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
			ESPWS_DEBUGVV("[%s] Preparing head @%d\n",
				_request->_remoteIdent.c_str(), _bufPrepared);
			_prepareHeadSendBuf(space);
		} else if (_state == RESPONSE_CONTENT) {
			_headers.clear(true);
			ESPWS_DEBUGVV("[%s] Preparing content @%d\n",
				_request->_remoteIdent.c_str(), _bufPrepared);
			_prepareContentSendBuf(space);
		}
		break;
	}
	return !!_sendbuf;
}

void AsyncSimpleResponse::_releaseSendBuf(bool more) {
	if (_state == RESPONSE_HEADERS) {
		if (_bufPrepared >= _headers.length()) {
			if (_isHeadOnly()) {
				// Do not send content body
				ESPWS_DEBUGVV("[%s] Satisfied head-only request @%d\n",
					_request->_remoteIdent.c_str(), _bufPrepared);
				_state = RESPONSE_WAIT_ACK;
			} else {
				_state = RESPONSE_CONTENT;
				_bufPrepared = 0;
				// Probe whether there is any content to send
				_prepareContentSendBuf(0);
			}
		}
	}
	_sendbuf = nullptr;
}

void AsyncSimpleResponse::_prepareHeadSendBuf(size_t space) {
	_prepareAllocatedSendBuf((uint8_t*)_headers.begin(), _headers.length(), space);
}

void AsyncSimpleResponse::_prepareContentSendBuf(size_t space) {
	// Implement null content
	ESPWS_DEBUGV("[%s] End of body content\n", _request->_remoteIdent.c_str());
	_state = RESPONSE_WAIT_ACK;
}

void AsyncSimpleResponse::_prepareAllocatedSendBuf(uint8_t const *buf, size_t limit, size_t space) {
	if (space) {
		ESPWS_DEBUGVV("[%s] Preparing static buffer of %d up to %d\n",
			_request->_remoteIdent.c_str(), limit, space);
		_sendbuf = (uint8_t*)&buf[_bufPrepared];
		size_t bufToSend = limit - _bufPrepared;
		_bufLen = space >= bufToSend? bufToSend: space;
		_bufPrepared+= _bufLen;
	}
}

/*
 * Basic (with concept of typed/sized content) Response
 * */

AsyncBasicResponse::AsyncBasicResponse(int code, String const &contentType)
	: AsyncSimpleResponse(code)
	, _contentType(contentType)
	, _contentLength(-1)
#ifdef ADVERTISE_ACCEPTRANGES
	, _acceptRanges(false)
#endif
{}

void AsyncBasicResponse::_assembleHead(void) {
	if (_contentLength && _contentLength != -1) {
		addHeader(FC("Content-Length"), String(_contentLength));
#ifdef ADVERTISE_ACCEPTRANGES
		if (_request->version()) {
			addHeader(FC("Accept-Ranges"), _acceptRanges ? FC("bytes") : FC("none"));
		}
#endif
	}
	if (_contentType) {
		addHeader(FC("Content-Type"), _contentType);
		_contentType.clear(true);
	} else if (_contentLength && _contentLength != -1) {
		// Make a safe (conservative) guess
		addHeader(FC("Content-Type"), FC("application/octet-stream"));
	}

	AsyncSimpleResponse::_assembleHead();
}

void AsyncBasicResponse::_prepareContentSendBuf(size_t space) {
	// While we establish the concept, non-null content is not supported, yet
	if (_contentLength && _contentLength != -1) {
		ESPWS_DEBUGV("[%s] WARNING: Non-null content support not implemented!\n");
	}
	AsyncSimpleResponse::_prepareContentSendBuf(space);
}

void AsyncBasicResponse::setContentLength(size_t len) {
	if (_started()) {
		ESPWS_LOG("[%s] ERROR: Response already started, cannot change content length!\n");
		return;
	}
	_contentLength = len;
}

void AsyncBasicResponse::setContentType(String const &type) {
	if (_started()) {
		ESPWS_LOG("[%s] ERROR: Response already started, cannot change content type!\n");
		return;
	}
	_contentType = type;
}

/*
 * String Reference Content Response
 * */

AsyncStringRefResponse::AsyncStringRefResponse(int code, String const &content,
	String const &contentType)
	: AsyncBasicResponse(code, contentType)
	, _content(content)
{
	if (!_contentType)
		_contentType = "text/plain";
}

void AsyncStringRefResponse::_assembleHead(void) {
	if (_contentLength == -1) {
		_contentLength = _content.length();
	} else if (_contentLength > _content.length()) {
		ESPWS_DEBUGV("[%s] Corrected content length overshoot %d -> %d\n",
			_request->_remoteIdent.c_str(), _contentLength, _content.length());
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
	if (_started()) {
		ESPWS_LOG("[%s] ERROR: Response already started, cannot append more data!\n");
		return 0;
	}
	return __content.write(data, len);
}

/*
 * Buffered (abstract) Content Response
 * */

#define STAGEBUF_SIZE 512

AsyncBufferedResponse::AsyncBufferedResponse(int code, String const &contentType)
	: AsyncBasicResponse(code, contentType), _stashbuf(nullptr)
{}

void AsyncBufferedResponse::_prepareContentSendBuf(size_t space) {
	if (_bufPrepared >= _contentLength)
		AsyncSimpleResponse::_prepareContentSendBuf(space);

	if (space) {
		size_t bufToSend = (_contentLength == -1)? space : _contentLength - _bufPrepared;
		_bufLen = (space < bufToSend)? space : bufToSend;
		if (_bufLen) {
			ESPWS_DEBUGV("[%s] Preparing %d / %d\n",
				_request->_remoteIdent.c_str(), _bufLen, bufToSend);

			_sendbuf = _stashbuf? _stashbuf : (_stashbuf = (uint8_t*)malloc(STAGEBUF_SIZE));
			if (_sendbuf) {
				_bufLen = _fillBuffer((uint8_t*)_sendbuf, _bufLen < STAGEBUF_SIZE? _bufLen
					: STAGEBUF_SIZE);
				_bufPrepared+= _bufLen;
			} else ESPWS_DEBUGV("[%s] Buffer allocation failed!\n", _request->_remoteIdent.c_str());
		}
	}
}

void AsyncBufferedResponse::_releaseSendBuf(bool more) {
	if (_state <= RESPONSE_HEADERS) {
		AsyncSimpleResponse::_releaseSendBuf(more);
		return;
	}
	if (!more) {
		free((void*)_stashbuf);
		_stashbuf = nullptr;

		if (_state == RESPONSE_CONTENT) {
			if (_bufPrepared >= _contentLength) {
				_state = RESPONSE_WAIT_ACK;
			}
		}
	}
	_sendbuf = nullptr;
}

/*
 * File Content Response
 * */

AsyncFileResponse::AsyncFileResponse(File const& content, String const &path,
	String const &contentType, int code, bool download)
	: AsyncBufferedResponse(code, contentType)
	, _content(content)
{
	if (_content) {
		_contentLength = _content.size();

		if (!contentType) {
			int extensionStart = path.lastIndexOf('.')+1;
			String extension = path.begin() + extensionStart;

			if (extension == FC("htm") || extension == FC("html"))
				_contentType = FC("text/html");
			else if (extension == FC("css")) _contentType = FC("text/css");
			else if (extension == FC("json")) _contentType = FC("text/json");
			else if (extension == FC("js")) _contentType = FC("application/javascript");
			else if (extension == FC("png")) _contentType = FC("image/png");
			else if (extension == FC("gif")) _contentType = FC("image/gif");
			else if (extension == FC("jpg") || extension == FC("jpeg"))
				_contentType = FC("image/jpeg");
			else if (extension == FC("ico")) _contentType = FC("image/x-icon");
			else if (extension == FC("svg")) _contentType = FC("image/svg+xml");
			else if (extension == FC("eot")) _contentType = FC("font/eot");
			else if (extension == FC("woff")) _contentType = FC("font/woff");
			else if (extension == FC("woff2")) _contentType = FC("font/woff2");
			else if (extension == FC("ttf")) _contentType = FC("font/ttf");
			else if (extension == FC("xml")) _contentType = FC("text/xml");
			else if (extension == FC("txt")) _contentType = FC("text/plain");
			else if (extension == FC("xhtml")) _contentType = FC("application/xhtml+xml");
			else if (extension == FC("pdf")) _contentType = FC("application/pdf");
			else if (extension == FC("zip")) _contentType = FC("application/zip");
			else if (extension == FC("gz")) _contentType = FC("application/x-gzip");
			//else _contentType = FC("application/octet-stream");
		}

		if (download) {
			size_t filenameStart = path.lastIndexOf('/') + 1;
			char buf[26+path.length()-filenameStart];
			snprintf_P(buf, sizeof(buf), PSTR_C("attachment; filename=\"%s\""), path.begin()+filenameStart);
			addHeader(FC("Content-Disposition"), buf);
		}
	}
}

void AsyncFileResponse::_assembleHead(void) {
	if (_content) {
		if (_contentLength > _content.size() && _contentLength != -1) {
			ESPWS_DEBUGV("[%s] Corrected content length overshoot %d -> %d\n",
				_request->_remoteIdent.c_str(), _contentLength, _content.size());
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
	ESPWS_DEBUGVV("[%s] File read up to %d, got %d\n",
		_request->_remoteIdent.c_str(), maxLen, outLen);
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

AsyncStreamResponse::AsyncStreamResponse(int code, Stream &content,
	String const &contentType, size_t len)
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

AsyncProgmemResponse::AsyncProgmemResponse(int code, PGM_P content,
	String const &contentType, size_t len)
	: AsyncBufferedResponse(code, contentType)
	, _content(content)
{
	_contentLength = len;
	if (_contentLength == -1) {
		_contentLength = strlen_P(content);
		ESPWS_DEBUGV("PROGMEM string length = %d\n", _contentLength);
	}
}

size_t AsyncProgmemResponse::_fillBuffer(uint8_t *buf, size_t maxLen) {
	memcpy_P(buf, _content + _bufPrepared, maxLen);
	return maxLen;
}

/*
 * Callback Content Response
 * */

AsyncCallbackResponse::AsyncCallbackResponse(int code, AwsResponseFiller callback,
	String const &contentType, size_t len)
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

AsyncChunkedResponse::AsyncChunkedResponse(int code, AwsResponseFiller callback,
	String const &contentType)
	: AsyncBufferedResponse(code, contentType)
	, _callback(callback)
{}

void AsyncChunkedResponse::_assembleHead(void){
	if (_request->version()) {
		addHeader(FC("Transfer-Encoding"), FC("chunked"));
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
	buf[0] = HexLookup_UC[(chunkLen >> 12) & 0xF];
	buf[1] = HexLookup_UC[(chunkLen >> 8) & 0xF];
	buf[2] = HexLookup_UC[(chunkLen >> 4) & 0xF];
	buf[3] = HexLookup_UC[(chunkLen >> 0) & 0xF];
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
