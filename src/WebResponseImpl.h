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

String const& GetPlatformAnnotation(void);

class AsyncSimpleResponse: public AsyncWebResponse {
  private:
    String _status;

  protected:
    String _headers;

    uint8_t const *_sendbuf;
    size_t _bufLen;
    size_t _bufSent;

    size_t _bufPrepared;
    size_t _inFlightLength;

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

    void _prepareAllocatedSendBuf(uint8_t const *buf, size_t limit, size_t space);

  public:
    AsyncSimpleResponse(int code);
    ~AsyncSimpleResponse() { _releaseSendBuf(); }

    virtual void addHeader(const char *name, const char *value) override;
    virtual void _respond(AsyncWebRequest &request) override;
    virtual void _ack(size_t len, uint32_t time) override;
    virtual size_t _process(size_t resShare) override;
};

class AsyncBasicResponse: public AsyncSimpleResponse {
  protected:
    String _contentType;
    size_t _contentLength;

    virtual void _assembleHead(void) override;
    virtual void _kickstart(void)
    { if (!_contentLength) AsyncSimpleResponse::_kickstart(); }

    // We now build the concept of typed and sized content, still no implementation here
    virtual void _prepareContentSendBuf(size_t space) override;

  public:
    AsyncBasicResponse(int code, const String& contentType=String());

    virtual void setContentLength(size_t len);
    virtual void setContentType(const String& type);
};

class AsyncStringRefResponse: public AsyncBasicResponse {
  private:
    String const &_content;

  protected:
    virtual void _assembleHead(void) override;
    virtual void _prepareContentSendBuf(size_t space) override;

  public:
    AsyncStringRefResponse(int code, const String& content, const String& contentType=String());
};

class AsyncStringResponse: public AsyncStringRefResponse {
  private:
    String __content;

  public:
    AsyncStringResponse(int code, const String& content, const String& contentType=String())
      : AsyncStringRefResponse(code, __content, contentType), __content(content) {}
};

class AsyncPrintResponse: public AsyncStringRefResponse, public Print {
  private:
    PrintString __content;

  public:
    AsyncPrintResponse(int code, const String& contentType=String())
      : AsyncStringRefResponse(code, __content, contentType) {}

    size_t write(const uint8_t *data, size_t len);
    size_t write(uint8_t data) { return write(&data, 1); }

    using Print::write;
};

class AsyncBufferedResponse: public AsyncBasicResponse {
  protected:
    uint8_t const *_stashbuf;
    AsyncBufferedResponse(int code, const String& contentType=String());

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
    AsyncFileResponse(FS &fs, const String& path, const String& contentType=String(), bool download=false)
      : AsyncFileResponse(fs.open(path, "r"), path, contentType, download) {}

    AsyncFileResponse(File const& content, const String& path, const String& contentType=String(), bool download=false);
};

class AsyncStreamResponse: public AsyncBufferedResponse {
  private:
    Stream &_content;

  protected:
    virtual void _assembleHead(void) override;
    virtual size_t _fillBuffer(uint8_t *buf, size_t maxLen) override;

  public:
    AsyncStreamResponse(int code, Stream &content, const String& contentType, size_t len);
};

class AsyncProgmemResponse: public AsyncBufferedResponse {
  private:
    const uint8_t* _content;

  protected:
    virtual void _assembleHead(void) override;
    virtual size_t _fillBuffer(uint8_t *buf, size_t maxLen) override;

  public:
    AsyncProgmemResponse(int code, const uint8_t* content, const String& contentType, size_t len);
};

class AsyncCallbackResponse: public AsyncBufferedResponse {
  private:
    AwsResponseFiller _callback;

  protected:
    virtual size_t _fillBuffer(uint8_t *buf, size_t maxLen) override;

  public:
    AsyncCallbackResponse(int code, AwsResponseFiller callback, const String& contentType, size_t len);
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
    AsyncChunkedResponse(int code, AwsResponseFiller callback, const String& contentType);
};

#endif /* AsyncWebResponseImpl_H_ */
