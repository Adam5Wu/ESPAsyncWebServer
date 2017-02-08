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
  char temp[] = "xx";

  String Ret;
  Ret.reserve(len);
  while (*buf){
    if ((*buf == '%') && buf[1] && buf[2]){
      temp[0] = *++buf;
      temp[1] = *++buf;
      Ret.concat((char)strtol(temp, NULL, 16));
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
      Ret.concat(HexLookup[(*buf >> 4) & 0xF]);
      Ret.concat(HexLookup[(*buf >> 0) & 0xF]);
    }
    buf++;
  }
  return Ret;
}

#define SCHED_RES      10
#define SCHED_SHARE    TCP_SND_BUF
// Minimal heap available before scheduling a response processing
// 4K = Flash physical sector size
// 2K = Misc heap uses
#define SCHED_MINHEAP  (4096+2048+SCHED_SHARE)

class RequestScheduler : private LinkedList<AsyncWebRequest*> {
  protected:
    os_timer_t timer = {0};
    bool running = false;
    uint8_t idleCnt = 0;

    ItemType *_cur = NULL;

    void startTimer() {
      if (!running) {
        running = true;
        os_timer_arm(&timer, SCHED_RES, true);
        ESPWS_DEBUGVV("<Scheduler> Start\n");
      }
    }
    void stopTimer() {
      if (running) {
        running = false;
        os_timer_disarm(&timer);
        ESPWS_DEBUGVV("<Scheduler> Stop\n");
      }
    }

    static void timerThunk(void *arg)
    { ((RequestScheduler*)arg)->run(true); }

  public:
    RequestScheduler()
    : LinkedList(std::bind(&RequestScheduler::curValidator, this, std::placeholders::_1))
    {
      os_timer_setfn(&timer, &RequestScheduler::timerThunk, this);
    }
    ~RequestScheduler() { stopTimer(); }

    void schedule(AsyncWebRequest *req) {
      if (append(req) == 0) startTimer();
      ESPWS_DEBUGVV("<Scheduler> +[%s], Queue=%d\n", req->_remoteIdent.c_str(), _count);
    }

    void deschedule(AsyncWebRequest *req) {
      remove(req);
      ESPWS_DEBUGVV("<Scheduler> -[%s], Queue=%d\n", req->_remoteIdent.c_str(), _count);
    }

    void curValidator(AsyncWebRequest *x) {
      if (_cur && _cur->value() == x) _cur = _cur->next;
    }

    void run(bool sched) {
      int _procCnt = 0;
      size_t freeHeap = ESP.getFreeHeap();
      while (_procCnt++ <= _count && freeHeap >= SCHED_MINHEAP) {
        if (!_cur) _cur = _head;
        if (_cur) {
          idleCnt = 0;
          ItemType *__cur = _cur;
          if (_cur->value()->_makeProgress(SCHED_SHARE, sched))
            freeHeap = ESP.getFreeHeap();
          // Move to next request, if current has not been removed
          if (_cur == __cur) _cur = _cur->next;
        } else {
          if (!++idleCnt) stopTimer();
          break;
        }
      }
    }

} Scheduler;

AsyncWebRequest::AsyncWebRequest(AsyncWebServer const &s, AsyncClient &c)
  : _server(s)
  , _client(c)
  , _handler(NULL)
  , _response(NULL)
  , _parser(NULL)
  , _state(REQUEST_SETUP)
  , _keepAlive(false)
  , _version(0)
  , _method(0)
  //, _url()
  //, _host()
  //, _contentType()
  , _contentLength(0)
  , _authType(AUTH_NONE)
  //, _authorization()
  , _headers(NULL)
  , _queries(NULL)
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
  Scheduler.deschedule(this);
  delete _parser;
  delete _response;
}

const char * AsyncWebRequest::methodToString() const {
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

ESPWS_DEBUGDO(const char* AsyncWebRequest::_stateToString(void) const {
  switch (_state) {
    case REQUEST_SETUP: return "Setup";
    case REQUEST_START: return "Start";
    case REQUEST_HEADERS: return "Headers";
    case REQUEST_BODY: return "Body";
    case REQUEST_RECEIVED: return "Received";
    case REQUEST_RESPONSE: return "Response";
    case REQUEST_ERROR: return "Error";
    case REQUEST_FINALIZE: return "Finalize";
    default: return "???";
  }
})

void AsyncWebRequest::_recycleClient(void) {
  delete _response;
  _handler = NULL;
  _response = NULL;
  //_keepAlive = false;
  //_version = 0;
  _method = 0;
  _contentLength = 0;
  _authType = AUTH_NONE;
  _state = REQUEST_SETUP;
  _client.setRxTimeout(DEFAULT_IDLE_TIMEOUT);
}

bool AsyncWebRequest::_makeProgress(size_t resShare, bool sched){
  switch (_state) {
    // The following state should never be seem here!
    ESPWS_DEBUGDO(REQUEST_RECEIVED: panic());

    case REQUEST_RESPONSE:
      ESPWS_DEBUGDO(while (!_response) panic());

      if (_response->_sending()) {
        if (_client.canSend()) {
          ESPWS_DEBUGVV("[%s] PROG: %d\n", _remoteIdent.c_str(), resShare);
          bool heapChanged = _response->_process(resShare) > 0;
          if (_response->_sending() || !_keepAlive) return heapChanged;
        }
        break;
      }

      if (!_response->_failed() && _keepAlive) {
        // Recycle for another request
        _recycleClient();
        return true;
      }

    case REQUEST_ERROR:
      if (!sched) return false;
      _client.close(true);

    case REQUEST_FINALIZE:
      delete this;
      return true;
  }
  return false;
}

void AsyncWebRequest::_onAck(size_t len, uint32_t time){
  ESPWS_DEBUGVV("[%s] ACK: %u @ %u\n", _remoteIdent.c_str(), len, time);
  if(_response && !_response->_finished()) {
    _response->_ack(len, time);
    if (!_response->_sending() && _keepAlive) _recycleClient();
  }
}

void AsyncWebRequest::_onError(int8_t error){
  ESPWS_DEBUG("[%s] ERROR: %d, client state: %s\n", _remoteIdent.c_str(), error, _client.stateToString());
}

void AsyncWebRequest::_onTimeout(uint32_t time){
  ESPWS_DEBUG("[%s] TIMEOUT: %u, client state: %s\n", _remoteIdent.c_str(), time, _client.stateToString());
  _state = REQUEST_ERROR;
}

void AsyncWebRequest::_onDisconnect(){
  ESPWS_DEBUGV("[%s] DISCONNECT, response state: %s\n", _remoteIdent.c_str(),
              _response? _response->_stateToString() : "NULL");
  _state = REQUEST_FINALIZE;
}

void AsyncWebRequest::_onData(void *buf, size_t len) {
  if (_state == REQUEST_SETUP) {
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
    ESPWS_DEBUG("[%s] On-Data: ignored extra data of %d bytes [%s] [%s]\n", _remoteIdent.c_str(),
                len, _stateToString(), _response? _response->_stateToString() : "NULL");
  }

  if (_state == REQUEST_RECEIVED) {
    _handler->_handleRequest(*this);
    if (_state == REQUEST_RECEIVED) {
      ESPWS_DEBUG("[%s] Ineffective handler!\n", _remoteIdent.c_str());
      _state = REQUEST_ERROR;
    }
    // Free up resources no longer needed
    _contentType.clear(true);
    _authorization.clear(true);
    _headers.clear();
    _queries.clear();
    _oUrl.clear(true);
    _oQuery.clear(true);
  }

  if (_state == REQUEST_RESPONSE) {
    _client.setRxTimeout(0);
    _response->_respond(*this);
    // Free up resources no longer needed
    _url.clear(true);
    _host.clear(true);
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

    ESPWS_DEBUGVV("[%s] Query [%s] = '%s'\n", _remoteIdent.c_str(), name, value);
    _queries.append(AsyncWebQuery(urlDecode(name,value-name), urlDecode(value,buf-value)));
  }
}

bool AsyncWebRequest::hasHeader(const String& name) const {
  return getHeader(name) != NULL;
}

AsyncWebHeader const* AsyncWebRequest::getHeader(const String& name) const {
  return _headers.get_if([&](AsyncWebHeader const &v) {
    return name.equalsIgnoreCase(v.name);
  });
}

bool AsyncWebRequest::hasQuery(const String& name) const {
  return getQuery(name) != NULL;
}

AsyncWebQuery const* AsyncWebRequest::getQuery(const String& name) const {
  return _queries.get_if([&](AsyncWebQuery const &v) {
    return name.equalsIgnoreCase(v.name);
  });
}

void AsyncWebRequest::send(AsyncWebResponse *response) {
  ESPWS_DEBUGDO(while (_response != NULL) panic());

  if(response == NULL){
    ESPWS_DEBUG("[%s] WARNING: NULL response\n", _remoteIdent.c_str());
    _state = REQUEST_ERROR;
    return;
  }

  _state = REQUEST_RESPONSE;
  _response = response;
}

AsyncWebResponse * AsyncWebRequest::beginResponse(int code, const String& content, const String& contentType){
  return content.empty()? (AsyncWebResponse*) new AsyncSimpleResponse(code)
                        : new AsyncStringResponse(code, content, contentType);
}

AsyncWebResponse * AsyncWebRequest::beginResponse(FS &fs, const String& path, const String& contentType, bool download){
  return new AsyncFileResponse(fs, path, contentType, download);
}

AsyncWebResponse * AsyncWebRequest::beginResponse(File content, const String& path, const String& contentType, bool download){
  return new AsyncFileResponse(content, path, contentType, download);
}

AsyncWebResponse * AsyncWebRequest::beginResponse(int code, Stream &content, const String& contentType, size_t len){
  return new AsyncStreamResponse(code, content, contentType, len);
}

AsyncWebResponse * AsyncWebRequest::beginResponse(int code, AwsResponseFiller callback, const String& contentType, size_t len){
  return new AsyncCallbackResponse(code, callback, contentType, len);
}

AsyncWebResponse * AsyncWebRequest::beginChunkedResponse(int code, AwsResponseFiller callback, const String& contentType){
  return _version? (AsyncWebResponse*) new AsyncChunkedResponse(code, callback, contentType)
                 : new AsyncCallbackResponse(code, callback, contentType, -1);
}

AsyncPrintResponse * AsyncWebRequest::beginPrintResponse(int code, const String& contentType){
  return new AsyncPrintResponse(code, contentType);
}

AsyncWebResponse * AsyncWebRequest::beginResponse_P(int code, const uint8_t * content, const String& contentType, size_t len){
  return new AsyncProgmemResponse(code, content, contentType, len);
}

AsyncWebResponse * AsyncWebRequest::beginResponse_P(int code, PGM_P content, const String& contentType){
  return beginResponse_P(code, (const uint8_t *)content, contentType, strlen_P(content));
}

void AsyncWebRequest::redirect(const String& url){
  AsyncWebResponse * response = beginResponse(302);
  response->addHeader("Location", url.c_str());
  send(response);
}
