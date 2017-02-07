// ESPasyncJson.h
/*
  Async Response to use with arduinoJson and asyncwebserver
  Written by Andrew Melvin (SticilFace) with help from me-no-dev and BBlanchon.
  Modified by Zhenyu Wu <Adam_5Wu@hotmail.com> for VFATFS, 2017.01

  example of callback in use

   server.on("/json", HTTP_ANY, [](AsyncWebServerRequest * request) {

    AsyncJsonResponse * response = new AsyncJsonResponse();
    JsonObject& root = response->getRoot();
    root["key1"] = "key number one";
    JsonObject& nested = root.createNestedObject("nested");
    nested["key1"] = "key number one";

    response->setLength();
    request->send(response);
  });

*/
#ifndef ASYNC_JSON_H_
#define ASYNC_JSON_H_

#include <ArduinoJson.h>

/*
 * Json Response
 * */

class AsyncJsonResponse: public AsyncBufferedResponse {
  private:
    JsonBuffer *_buf;
    JsonVariant _root;

    class BufferWindowPrint : public Print {
      private:
        uint8_t* _buf;
        size_t _win_start;
        size_t _win_size;
        size_t _prn_pos;
      public:
        BufferWindowPrint(uint8_t* buf, size_t from, size_t len)
        : _buf(buf), _win_start(from), _win_size(len), _prn_pos{0} {}
        virtual size_t write(uint8_t c) override {
          if (_win_start) {
            _win_start--;
            return 1;
          }
          if (_win_size) {
            _buf[_prn_pos++] = c;
            _win_size--;
            return 1;
          }
          return 0;
        }
    };

  struct JSON_ARRAY_T {};
  AsyncJsonResponse(JSON_ARRAY_T)
    : AsyncBufferedResponse(200, "text/json")
    , _buf(new DynamicJsonBuffer())
    , _root(_buf->createArray())
    , root(_root)
  {}

  struct JSON_OBJECT_T {};
  AsyncJsonResponse(JSON_OBJECT_T)
    : AsyncBufferedResponse(200, "text/json")
    , _buf(new DynamicJsonBuffer())
    , _root(_buf->createObject())
    , root(_root)
  {}

  public:
    JsonVariant &root;

    AsyncJsonResponse(JsonVariant &refRoot)
      : AsyncBufferedResponse(200, "text/json")
      , _buf(NULL)
      , _root()
      , root(refRoot)
    {}

    static AsyncJsonResponse* Create(bool isArray = false) {
      JSON_ARRAY_T JSON_ARRAY;
      JSON_OBJECT_T JSON_OBJECT;
      return isArray? new AsyncJsonResponse(JSON_ARRAY) : new AsyncJsonResponse(JSON_OBJECT);
    }

    ~AsyncJsonResponse() { delete _buf; }

    virtual String _assembleHead(uint8_t version) override {
      _contentLength = _root.measureLength();
      return AsyncBasicResponse::_assembleHead(version);
    }

    size_t _fillBuffer(uint8_t *data, size_t len){
      BufferWindowPrint prn(data, _bufPrepared, len);
      _root.printTo(prn);
      return len;
    }
};
#endif