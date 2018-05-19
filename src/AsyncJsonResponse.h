// ESPasyncJson.h
/*
  Async Response to use with arduinoJson and asyncwebserver
  Written by Andrew Melvin (SticilFace) with help from me-no-dev and BBlanchon.
  Modified by Zhenyu Wu <Adam_5Wu@hotmail.com> for VFATFS, 2017-2018

  example of callback in use:

  server.on("/json", HTTP_ANY, [](AsyncWebServerRequest * request) {
    AsyncJsonResponse * response = AsyncJsonResponse::CreateNewObjectResponse();
    response->root["key1"] = "key number one";
    JsonObject& nested = response->root.createNestedObject("nested");
    nested["key1"] = "key number one";
    request->send(response);
  });
*/
#ifndef AsyncJson_H_
#define AsyncJson_H_

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "WebResponseImpl.h"

#include <functional>

/*
 * Json Response
 * */

#define ASYNCWEB_JSON_MAXIMUM_BUFFER 4096
//#define ASYNCWEB_JSON_BUFFER_STATIC

#ifdef ASYNCWEB_JSON_BUFFER_STATIC
typedef StaticJsonBuffer<ASYNCWEB_JSON_MAXIMUM_BUFFER> AsyncJsonBuffer;
#else
#include <BoundedAllocator.h>
typedef Internals::DynamicJsonBufferBase<BoundedOneshotAllocator> AsyncJsonBuffer;
#endif
typedef std::function<JsonVariant(AsyncJsonBuffer &)> JsonCreateRootCallback;

class AsyncJsonResponse: public AsyncChunkedResponse {
	private:
#ifndef ASYNCWEB_JSON_BUFFER_STATIC
		BoundedOneshotAllocator _bufferAllocator;
#endif
		AsyncJsonBuffer _jsonBuffer;
		JsonVariant _jsonRoot;
		bool _prettyPrint;

		size_t _JsonFiller(uint8_t*, size_t, size_t);

	public:
		JsonVariant &root;

		AsyncJsonResponse(JsonCreateRootCallback const& root_cb, int code = 200)
			: AsyncChunkedResponse(code,
				std::bind(&AsyncJsonResponse::_JsonFiller, this,
					std::placeholders::_1, std::placeholders::_2,
					std::placeholders::_3),
				FL("text/json"))
#ifdef ASYNCWEB_JSON_BUFFER_STATIC
			//, _jsonBuffer()
#else
			, _bufferAllocator(ASYNCWEB_JSON_MAXIMUM_BUFFER)
			, _jsonBuffer(_bufferAllocator,
				ASYNCWEB_JSON_MAXIMUM_BUFFER-AsyncJsonBuffer::EmptyBlockSize)
#endif
			, _jsonRoot(std::move(root_cb(_jsonBuffer)))
			, _prettyPrint(false)
			, root(_jsonRoot)
			{}

		void setPrettyPrint(bool enable = true);

		static AsyncJsonResponse* CreateNewObjectResponse(int code = 200) {
			return new AsyncJsonResponse([](AsyncJsonBuffer &buf) {
					return JsonVariant(buf.createObject());
				}, code);
		}

		static AsyncJsonResponse* CreateNewArrayResponse(int code = 200) {
			return new AsyncJsonResponse([](AsyncJsonBuffer &buf) {
					return JsonVariant(buf.createArray());
				}, code);
		}
};
#endif