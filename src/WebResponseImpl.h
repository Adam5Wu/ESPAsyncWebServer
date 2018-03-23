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

#ifndef AsyncWebResponseImpl_H_
#define AsyncWebResponseImpl_H_

#include <StreamString.h>

#include <ESPAsyncWebServer.h>

String const& GetPlatformSignature(void);

class AsyncSimpleResponse: public AsyncWebResponse {
	private:
		String _status;

	protected:
		String _headers;

		uint8_t const *_sendbuf = nullptr;
		size_t _bufLen = 0;
		size_t _bufSent = 0;

		size_t _bufPrepared = 0;
		size_t _inFlightLength = 0;

		virtual void _assembleHead(void);
		virtual void _kickstart(void)
		{ _process(_bufLen+_headers.length()); }

		virtual bool _prepareSendBuf(size_t resShare);
		virtual void _prepareHeadSendBuf(size_t space);
		// We do not support content at this stage, but since the concept of content is important
		//  we land the concept here, but only implement null content
		virtual void _prepareContentSendBuf(size_t space);
		virtual void _releaseSendBuf(bool more = false);

		virtual void _requestComplete(void) { _state = RESPONSE_END; }

		bool _isHeadOnly(void) { _request->method() == HTTP_HEAD; }
		void _prepareAllocatedSendBuf(uint8_t const *buf, size_t limit, size_t space);

	public:
		AsyncSimpleResponse(int code): AsyncWebResponse(code) {}
		~AsyncSimpleResponse() { _releaseSendBuf(); }

		virtual void addHeader(String const &name, String const &value) override;
		virtual void _respond(AsyncWebRequest &request) override;
		virtual void _ack(size_t len, uint32_t time) override;
		virtual size_t _process(size_t resShare) override;
};

class AsyncBasicResponse: public AsyncSimpleResponse {
	protected:
		String _contentType;
		size_t _contentLength;
#ifdef ADVERTISE_ACCEPTRANGES
		bool _acceptRanges;
#endif

		virtual void _assembleHead(void) override;
		virtual void _kickstart(void)
		{ if (!_contentLength) AsyncSimpleResponse::_kickstart(); }

		// We now build the concept of typed and sized content, still no implementation here
		virtual void _prepareContentSendBuf(size_t space) override;

	public:
		AsyncBasicResponse(int code, String const &contentType=String());

		virtual void setContentLength(size_t len);
		virtual void setContentType(String const &type);
};

class AsyncStringRefResponse: public AsyncBasicResponse {
	private:
		String const &_content;

	protected:
		virtual void _assembleHead(void) override;
		virtual void _prepareContentSendBuf(size_t space) override;

	public:
		AsyncStringRefResponse(int code, String const &content, String const &contentType=String());
};

class AsyncStringResponse: public AsyncStringRefResponse {
	private:
		String __content;

	public:
		AsyncStringResponse(int code, String const &content, String const &contentType=String())
			: AsyncStringRefResponse(code, __content, contentType), __content(content) {}
		AsyncStringResponse(int code, String &&content, String const &contentType=String())
			: AsyncStringRefResponse(code, __content, contentType), __content(std::move(content)) {}
};

class AsyncPrintResponse: public AsyncStringRefResponse, public Print {
	private:
		PrintString __content;

	public:
		AsyncPrintResponse(int code, String const &contentType=String())
			: AsyncStringRefResponse(code, __content, contentType) {}

		size_t write(const uint8_t *data, size_t len);
		size_t write(uint8_t data) { return write(&data, 1); }

		using Print::write;
};

class AsyncBufferedResponse: public AsyncBasicResponse {
	protected:
		uint8_t const *_stashbuf;
		AsyncBufferedResponse(int code, String const &contentType=String());

		virtual void _prepareContentSendBuf(size_t space) override;
		virtual void _releaseSendBuf(bool more) override;
		virtual size_t _fillBuffer(uint8_t *buf, size_t maxLen) = 0;
};

class AsyncFileResponse: public AsyncBufferedResponse {
	private:
		File _content;

	protected:
		virtual void _assembleHead(void) override;
		virtual size_t _fillBuffer(uint8_t *buf, size_t maxLen) override;

	public:
		AsyncFileResponse(FS &fs, String const &path, String const &contentType=String(),
			int code=200, bool download=false)
			: AsyncFileResponse(fs.open(path, "r"), path, contentType, code, download) {}

		AsyncFileResponse(File const& content, String const &path,
			String const &contentType=String(), int code=200, bool download=false);

		static String contentTypeByName(String const &filename);
};

class AsyncStreamResponse: public AsyncBufferedResponse {
	private:
		Stream &_content;

	protected:
		virtual void _assembleHead(void) override;
		virtual size_t _fillBuffer(uint8_t *buf, size_t maxLen) override;

	public:
		AsyncStreamResponse(int code, Stream &content, String const &contentType, size_t len);
};

class AsyncProgmemResponse: public AsyncBufferedResponse {
	private:
		PGM_P _content;

	protected:
		virtual size_t _fillBuffer(uint8_t *buf, size_t maxLen) override;

	public:
		AsyncProgmemResponse(int code, PGM_P content, String const &contentType, size_t len);
};

class AsyncCallbackResponse: public AsyncBufferedResponse {
	private:
		AwsResponseFiller _callback;

	protected:
		virtual size_t _fillBuffer(uint8_t *buf, size_t maxLen) override;

	public:
		AsyncCallbackResponse(int code, AwsResponseFiller callback,
			String const &contentType, size_t len);
};

class AsyncChunkedResponse: public AsyncBufferedResponse {
	private:
		AwsResponseFiller _callback;
		size_t _chunkCnt;

	protected:
		virtual void _assembleHead(void) override;
		virtual void _prepareContentSendBuf(size_t space) override;
		virtual size_t _fillBuffer(uint8_t *buf, size_t maxLen) override;

	public:
		AsyncChunkedResponse(int code, AwsResponseFiller callback, String const &contentType);
};

#endif /* AsyncWebResponseImpl_H_ */
