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

#include "ESPAsyncWebServer.h"
#include "WebRequestParsers.h"
#include "WebResponseImpl.h"

#ifndef ESP8266
#define os_strlen strlen
#endif

extern "C" {
	#include "lwip/opt.h"
	#include "user_interface.h"
}

String urlDecode(char const *buf, size_t len) {
	char temp[3];
	temp[2] = '\0';

	String Ret;
	Ret.reserve(len);
	while (*buf){
		if ((*buf == '%') && buf[1] && buf[2]){
			temp[0] = *++buf;
			temp[1] = *++buf;
			Ret.concat((char)strtol(temp, nullptr, 16));
		} else if (*buf == '+') {
			Ret.concat(' ');
		} else {
			Ret.concat(*buf);  // normal ascii char
		}
		buf++;
	}
	return Ret;
}

String urlEncode(char const *buf, size_t len) {
	String Ret;
	Ret.reserve(len);
	while (*buf){
		if (isalnum(*buf) || *buf == '-' || *buf == '_' || *buf == '.' || *buf == '~') {
			Ret.concat(*buf);  // normal ascii char
		} else if (*buf == ' ') {
			Ret.concat('+');
		} else {
			Ret.concat('%');
			Ret.concat(HexLookup_UC[(*buf >> 4) & 0xF]);
			Ret.concat(HexLookup_UC[(*buf >> 0) & 0xF]);
		}
		buf++;
	}
	return Ret;
}

#define SCHED_RES       10
#define SCHED_MAXSHARE  TCP_SND_BUF
// Minimal heap available before scheduling a response processing
#define SCHED_MINHEAP   2048

#ifdef PURGE_TIMEWAIT
struct tcp_pcb;
extern struct tcp_pcb* tcp_tw_pcbs;
extern "C" void tcp_abort (struct tcp_pcb* pcb);
#endif

static class RequestScheduler : private LinkedList<AsyncWebRequest*> {
	protected:
		os_timer_t timer = {0};
		ItemType *_cur = nullptr;
		uint8_t running = 0;

		void startTimer(void) {
			if (!running) {
				running = 1;
				os_timer_arm(&timer, SCHED_RES, true);
				ESPWS_DEBUGVV_S(L,"<Scheduler> Start\n");
			}
		}
		void stopTimer(void) {
			os_timer_disarm(&timer);
			ESPWS_DEBUGVV_S(L,"<Scheduler> Stop\n");
#ifdef PURGE_TIMEWAIT
			// Cleanup time-wait connections to conserve resources
			while (tcp_tw_pcbs) {
				tcp_abort(tcp_tw_pcbs);
			}
#endif
		}

		static void timerThunk(void *arg)
		{ ((RequestScheduler*)arg)->run(true); }

	public:
		RequestScheduler(void)
			: LinkedList(std::bind(&RequestScheduler::curValidator, this, std::placeholders::_1)) {
			os_timer_setfn(&timer, &RequestScheduler::timerThunk, this);
		}
		~RequestScheduler(void) { if (running) stopTimer(); }

		void schedule(AsyncWebRequest *req) {
			if (append(req) == 0) startTimer();
			ESPWS_DEBUGVV_S(L,"<Scheduler> +[%s], Queue=%d\n", req->_remoteIdent.c_str(), _count);
		}

		void deschedule(AsyncWebRequest *req) {
			remove(req);
			ESPWS_DEBUGVV_S(L,"<Scheduler> -[%s], Queue=%d\n", req->_remoteIdent.c_str(), _count);
		}

		void curValidator(AsyncWebRequest *x) {
			if (_cur && _cur->value() == x) _cur = _cur->next;
		}

		void run(bool sched) {
			int _procCnt = 0;
			size_t freeHeap = ESP.getFreeHeap();
#ifdef PURGE_TIMEWAIT
			if (freeHeap < SCHED_MINHEAP+SCHED_MAXSHARE) {
				ESPWS_DEBUGV_S(L,"<Scheduler> Purging time-wait connections\n");
				while (tcp_tw_pcbs) {
					tcp_abort(tcp_tw_pcbs);
				}
				freeHeap = ESP.getFreeHeap();
			}
#endif
			if (freeHeap < SCHED_MINHEAP) {
				ESPWS_DEBUG_S(L,"<Scheduler> WARNING: Not enough heap to make progress!\n");
				return;
			}

			while (++_procCnt <= _count && freeHeap >= SCHED_MINHEAP) {
				if (!_cur) _cur = _head;
				if (_cur) {
					running = 1;
					ItemType *__cur = _cur;
					size_t schedShare = min(freeHeap - SCHED_MINHEAP, (size_t)SCHED_MAXSHARE);
					if (_cur->value()->_makeProgress(schedShare, sched))
						freeHeap = ESP.getFreeHeap();
					// Move to next request, if current has not been removed
					if (_cur == __cur) _cur = _cur->next;
				} else {
					if (!++running) stopTimer();
					break;
				}
			}
		}

} Scheduler;

AsyncWebRequest::AsyncWebRequest(AsyncWebServer const &s, AsyncClient &c,
	ArTerminationNotify const &termNotify)
	: _server(s)
	, _client(c)
	, _termNotify(termNotify)
	, _handler(nullptr)
	, _response(nullptr)
	, _parser(nullptr)
	, _state(REQUEST_SETUP)
	, _keepAlive(false)
#ifdef HANDLE_WEBDAV
	, _translate(false)
#endif
	, _lastDiscardTS(0)
	, _version(0)
	, _method(HTTP_NONE)
	//, _url()
	//, _host()
	//, _contentType()
	, _contentLength(-1)
#ifdef HANDLE_AUTHENTICATION
	, _session(nullptr)
#endif
	, _headers(nullptr)
	, _queries(nullptr)
#ifdef HANDLE_REQUEST_CONTENT
#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
	, _params(nullptr)
#endif
#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
	, _uploads(nullptr)
#endif
#endif
	ESPWS_DEBUGDO(, _remoteIdent(c.remoteIP().toString()+':'+c.remotePort()))
{
	ESPWS_DEBUGV("[%s] CONNECTED\n", _remoteIdent.c_str());
	c.setRxTimeout(DEFAULT_IDLE_TIMEOUT);
	c.setAckTimeout(DEFAULT_ACK_TIMEOUT);
	c.onError([](void *r, AsyncClient* c, int8_t error){
		((AsyncWebRequest*)r)->_onError(error);
	}, this);
	c.onAck([](void *r, AsyncClient* c, size_t len, uint32_t time){
		((AsyncWebRequest*)r)->_onAck(len, time);
		Scheduler.run(false);
	}, this);
	c.onDisconnect([](void *r, AsyncClient* c){
		((AsyncWebRequest*)r)->_onDisconnect();
		delete c;
	}, this);
	c.onTimeout([](void *r, AsyncClient* c, uint32_t time){
		((AsyncWebRequest*)r)->_onTimeout(time);
	}, this);
	c.onData([](void *r, AsyncClient* c, void *buf, size_t len){
		((AsyncWebRequest*)r)->_onData(buf, len);
	}, this);
	Scheduler.schedule(this);
}

AsyncWebRequest::~AsyncWebRequest(){
	delete _parser;
	_termNotify(this);
	_cleanup(REQUEST_CLEANUP_STAGE3);
	Scheduler.deschedule(this);
}

PGM_P AsyncWebRequest::methodToString() const {
	return AsyncWebServer::mapMethod(_method);
}

ESPWS_DEBUGDO(PGM_P AsyncWebRequest::_stateToString(void) const {
	switch (_state) {
		case REQUEST_SETUP: return PSTR_C("Setup");
		case REQUEST_START: return PSTR_C("Start");
		case REQUEST_HEADERS: return PSTR_C("Headers");
		case REQUEST_BODY: return PSTR_C("Body");
		case REQUEST_RECEIVED: return PSTR_C("Received");
		case REQUEST_RESPONSE: return PSTR_C("Response");
		case REQUEST_HALT: return PSTR_C("Halt");
		case REQUEST_FINALIZE: return PSTR_C("Finalize");
		default: return PSTR_C("???");
	}
})

#ifdef HANDLE_AUTHENTICATION
WebACLMatchResult AsyncWebRequest::_setSession(WebAuthSession *session) {
	delete _session;
	_session = nullptr;

	WebACLMatchResult Ret = ACL_NONE;
	if (session) {
		Ret = _server._checkACL(_method, _url, session);
		if (Ret == ACL_ALLOWED) _session = session;
	}
	return Ret;
}
#endif

void AsyncWebRequest::_cleanup(uint8_t stages) {
	if (stages & REQUEST_CLEANUP_STAGE1) {
		_contentType.clear(true);
		_oUrl.clear(true);
		_oQuery.clear(true);
		_headers.clear();
		_queries.clear();
#ifdef HANDLE_REQUEST_CONTENT
	#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
		_params.clear();
	#endif
	#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
		_uploads.clear();
	#endif
#endif
	}
	if (stages & REQUEST_CLEANUP_STAGE2) {
		_url.clear(true);
		_host.clear(true);
		_accept.clear(true);
#ifdef REQUEST_ACCEPTLANG
		_acceptLanguage.clear(true);
#endif
#ifdef REQUEST_USERAGENT
		_userAgent.clear(true);
#endif
#ifdef REQUEST_REFERER
		_referer.clear(true);
#endif
	}
	if (stages & REQUEST_CLEANUP_STAGE3) {
		delete _response;
		_response = nullptr;
		if (_handler) {
			_handler->_terminateRequest(*this);
			_handler = nullptr;
		}

#ifdef HANDLE_AUTHENTICATION
		_setSession(nullptr);
#endif
	}
}

void AsyncWebRequest::_recycleClient(void) {
	// We can only recycle client if everything is OK, which implies that
	//   all parsing must have completed and parser freed
	if (_parser) {
		ESPWS_LOG("ERROR: Dirty parser state\n");
		panic();
	}
	ESPWS_DEBUGV("[%s] Recycling connection...\n", _remoteIdent.c_str());


#ifdef SUPPORT_CGI
	_cleanup(REQUEST_CLEANUP_STAGE1 | REQUEST_CLEANUP_STAGE2);
#endif
	_cleanup(REQUEST_CLEANUP_STAGE3);

	_method = HTTP_NONE;
	_contentLength = -1;
	_lastDiscardTS = 0;
	_state = REQUEST_SETUP;
	// Note: the following two fields are the reasons we are here, so no need to touch
	//_keepAlive = true;
	//_version = 1;
#ifdef HANDLE_WEBDAV
	_translate = false;
#endif

	_client.setRxTimeout(DEFAULT_IDLE_TIMEOUT);
}

bool AsyncWebRequest::_makeProgress(size_t resShare, bool sched){
	switch (_state) {
		// The following states should never be seem here!
		ESPWS_DEBUGDO(
		case REQUEST_RECEIVED:
			panic()
		);

		case REQUEST_RESPONSE:
			// If we are in discard mode
			if (_lastDiscardTS) {
				// Do not act on unscheduled progress
				if (!sched) return false;
				// Do not act until the channel is idle
				time_t idleSpan = millis() - _lastDiscardTS;
				if (idleSpan < REQUEST_DISCARD_IDLE)
					return false;
				_lastDiscardTS = 0;
			}

			ESPWS_DEBUGDO(if (!_response) panic());
			if (_response->_sending() && _client.canSend()) {
				ESPWS_DEBUGVV("[%s] Response progress: %d\n", _remoteIdent.c_str(), resShare);
				size_t progress = _response->_process(resShare);
				// Recycle for another request
				if (!_response->_sending() && !_response->_failed() && _keepAlive) {
					_recycleClient();
				}
				return progress > 0;
			}
			if (!_response->_finished()) break;

		case REQUEST_HALT:
			if (!sched) break;
			_client.close(true);
			// "Leak" through does the job faster
			//return true;

		case REQUEST_FINALIZE:
			delete this;
			return true;
	}
	return false;
}

void AsyncWebRequest::_onAck(size_t len, uint32_t time){
	if(_response && !_response->_finished()) {
		ESPWS_DEBUGVV("[%s] Response ACK: %u @ %u\n", _remoteIdent.c_str(), len, time);
		_response->_ack(len, time);
	} else {
		// Ack from a previous response, since we have already recycled, just ignore...
		ESPWS_DEBUGVV("[%s] Ignored ACK: %u @ %u\n", _remoteIdent.c_str(), len, time);
	}
}

void AsyncWebRequest::_onError(int8_t error){
	ESPWS_DEBUG("[%s] TCP ERROR: %d, client state: %s\n",
		_remoteIdent.c_str(), error, SFPSTR(_client.stateToString()));
}

void AsyncWebRequest::_onTimeout(uint32_t time){
	ESPWS_DEBUGV("[%s] TIMEOUT: %ums, client state: %s\n",
		_remoteIdent.c_str(), time, SFPSTR(_client.stateToString()));
	_state = REQUEST_HALT;
}

void AsyncWebRequest::_onDisconnect(){
	ESPWS_DEBUGV("[%s] DISCONNECT, response state: %s\n", _remoteIdent.c_str(),
		SFPSTR(_response? _response->_stateToString() : PSTR_C("(None)")));
	_state = REQUEST_FINALIZE;
}

void AsyncWebRequest::_onData(void *buf, size_t len) {
	if (_state == REQUEST_SETUP) {
#ifdef HANDLE_AUTHENTICATION
		_server._authMaintenance();
#endif
		_parser = new AsyncRequestHeadParser(*this);
		_state = REQUEST_START;
	}

	if (_state == REQUEST_START || _state == REQUEST_HEADERS) {
		_parser->_parse(buf, len);
		if (_state <= REQUEST_BODY && !len) return;
	}

#ifdef HANDLE_REQUEST_CONTENT
	if (_state == REQUEST_BODY) {
		_parser->_parse(buf, len);
		if (_state == REQUEST_BODY && !len) return;
	}
#endif

	if (len) {
		// We have some unprocessed request data, keep chewing...
		ESPWS_DEBUG("[%s] On-Data: ignored request data of %d bytes [%s] "
			"[Parser: %s] [Response: %s]\n",
			_remoteIdent.c_str(), len, SFPSTR(_stateToString()),
			SFPSTR(_parser? _parser->_stateToString() : PSTR_C("N/A")),
			SFPSTR(_response? _response->_stateToString() : PSTR_C("N/A")));
		_lastDiscardTS = millis();
	}

	if (_state == REQUEST_RECEIVED) {
		_handler->_handleRequest(*this);
		if (_state == REQUEST_RECEIVED) {
			ESPWS_DEBUG("[%s] Ineffective handler!\n", _remoteIdent.c_str());
			send_P(500, PSTR_C("Request handler produced no reply"), FC("text/plain"));
		}
	}

	if (_state == REQUEST_RESPONSE && !_response->_started()) {
		_client.setRxTimeout(0);
#ifndef SUPPORT_CGI
		// Free up resources no longer needed
		_cleanup(REQUEST_CLEANUP_STAGE1);
#endif
		_response->_respond(*this);
#ifndef SUPPORT_CGI
		// Free up resources no longer needed
		_cleanup(REQUEST_CLEANUP_STAGE2);
#endif
	}
}

void AsyncWebRequest::_setUrl(String && url) {
	int indexQuery = url.indexOf('?');
	if (indexQuery > 0){
		_oQuery = &url[indexQuery];
		_parseQueries(&url[indexQuery+1]);
		url.remove(indexQuery);
	} else {
		_queries.clear();
		_oQuery.clear(true);
	}
	_url = urlDecode(url.c_str(), url.length());
	_oUrl = std::move(url);
}

void AsyncWebRequest::_parseQueries(char *buf){
	_queries.clear();

	while (*buf){
		char *name = buf;
		while (*buf != '&')
			if (*buf) buf++;
			else break;
		if (name == buf) {
			buf++;
			continue;
		}
		if (*buf) *buf++ = '\0';

		char *value = name;
		while (*value != '=')
			if (*value) value++;
			else break;
		if (*value) *value++ = '\0';

		String _name = urlDecode(name,value-name);
		String _value = urlDecode(value,buf-value);
		ESPWS_DEBUGVV("[%s] Query [%s] = '%s'\n",
			_remoteIdent.c_str(), _name.c_str(), _value.c_str());
		if (_name.endsWith("[]",2,0,false)) {
			_params.append(AsyncWebParam(std::move(_name), std::move(_value)));
		} else _addUniqueNameVal(_queries, _name, _value);
	}
}

bool AsyncWebRequest::hasHeader(String const &name) const {
	return getHeader(name) != nullptr;
}

AsyncWebHeader const* AsyncWebRequest::getHeader(String const &name) const {
	return _headers.get_if([&](AsyncWebHeader const &v) {
		return name.equalsIgnoreCase(v.name);
	});
}

bool AsyncWebRequest::hasQuery(String const &name) const {
	return getQuery(name) != nullptr;
}

AsyncWebQuery const* AsyncWebRequest::getQuery(String const &name) const {
	return _queries.get_if([&](AsyncWebQuery const &v) {
		return name == v.name;
	});
}

#ifdef HANDLE_REQUEST_CONTENT

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
bool AsyncWebRequest::hasParam(String const &name) const {
	return getParam(name) != nullptr;
}

AsyncWebParam const* AsyncWebRequest::getParam(String const &name) const {
	return _params.get_if([&](AsyncWebParam const &v) {
		return name == v.name;
	});
}
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
bool AsyncWebRequest::hasUpload(String const &name) const {
	return getUpload(name) != nullptr;
}

AsyncWebUpload const* AsyncWebRequest::getUpload(String const &name) const {
	return _uploads.get_if([&](AsyncWebUpload const &v) {
		return name == v.name;
	});
}
#endif

#endif

void AsyncWebRequest::send(AsyncWebResponse *response) {
	if(_response){
		ESPWS_LOG("ERROR: Response already in progress!\n");
		delete response;
		return;
	}
	if(response == nullptr){
		ESPWS_LOG("ERROR: Response is NULL\n");
		return;
	}

	_state = REQUEST_RESPONSE;
	_response = response;
}

AsyncWebResponse *AsyncWebRequest::beginResponse(int code, String const &content,
	String const &contentType){
	return content ? (AsyncWebResponse*) new AsyncStringResponse(code, content, contentType)
		: new AsyncSimpleResponse(code);
}

AsyncWebResponse *AsyncWebRequest::beginResponse(int code, String &&content,
	String const &contentType){
	return content ? (AsyncWebResponse*) new AsyncStringResponse(code, std::move(content), contentType)
		: new AsyncSimpleResponse(code);
}

AsyncWebResponse *AsyncWebRequest::beginResponse(FS &fs, String const &path,
	String const &contentType, int code, bool download){
	return new AsyncFileResponse(fs, path, contentType, code, download);
}

AsyncWebResponse *AsyncWebRequest::beginResponse(File content, String const &path,
	String const &contentType, int code, bool download){
	return new AsyncFileResponse(content, path, contentType, code, download);
}

AsyncWebResponse *AsyncWebRequest::beginResponse(int code, Stream &content,
	String const &contentType, size_t len){
	return new AsyncStreamResponse(code, content, contentType, len);
}

AsyncWebResponse *AsyncWebRequest::beginResponse(int code, AwsResponseFiller callback,
	String const &contentType, size_t len){
	return new AsyncCallbackResponse(code, callback, contentType, len);
}

AsyncWebResponse *AsyncWebRequest::beginChunkedResponse(int code, AwsResponseFiller callback,
	String const &contentType){
	return _version? (AsyncWebResponse*) new AsyncChunkedResponse(code, callback, contentType)
		: new AsyncCallbackResponse(code, callback, contentType, -1);
}

AsyncPrintResponse * AsyncWebRequest::beginPrintResponse(int code, String const &contentType){
	return new AsyncPrintResponse(code, contentType);
}

AsyncWebResponse *AsyncWebRequest::beginResponse_P(int code, PGM_P content,
	String const &contentType, size_t len){
	return new AsyncProgmemResponse(code, content, contentType, len);
}

void AsyncWebRequest::redirect(String const &url){
	AsyncWebResponse *response = beginResponse(302);
	response->addHeader(FC("Location"), url);
	send(response);
}
