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
#ifndef ASYNCWEBSERVERRESPONSEIMPL_H_
#define ASYNCWEBSERVERRESPONSEIMPL_H_

#include "StreamString.h"

class AsyncSimpleResponse: public AsyncWebServerResponse {
  private:
    PrintString _head;

  protected:
    uint8_t const *_sendbuf;
    size_t _bufLen;
    size_t _bufSent;

    size_t _bufPrepared;
    size_t _inFlightLength;

    virtual void assembleHead(uint8_t version);

    virtual bool prepareSendBuf(AsyncWebServerRequest *request);
    virtual bool prepareHeadSendBuf(size_t space);
    // We do not support content at this stage, but since the concept of content is important
    //  we land the concept here, but only implement null content
    virtual bool prepareContentSendBuf(size_t space);
    virtual void releaseSendBuf(void) { _sendbuf = NULL; }

    virtual void requestCleanup(AsyncWebServerRequest *request) { request->client()->close(true); }

    bool prepareAllocatedSendBuf(uint8_t const *buf, size_t limit, size_t space);

  public:
    AsyncSimpleResponse(int code);
    ~AsyncSimpleResponse() { releaseSendBuf(); }

    void _respond(AsyncWebServerRequest *request);
    size_t _ack(AsyncWebServerRequest *request, size_t len, uint32_t time);
};

class AsyncBasicResponse: public AsyncSimpleResponse {
  protected:
    String _contentType;
    size_t _contentLength;

    virtual void assembleHead(uint8_t version) override;
    // We now build the concept of typed and sized content, still no implementation here
    virtual bool prepareContentSendBuf(size_t space) override;

  public:
    AsyncBasicResponse(int code, const String& contentType=String());

    virtual void setContentLength(size_t len);
    virtual void setContentType(const String& type);
};

class AsyncStringResponse: public AsyncBasicResponse {
  private:
    String _content;

  protected:
    virtual void assembleHead(uint8_t version) override;
    virtual bool prepareContentSendBuf(size_t space) override;

  public:
    AsyncStringResponse(int code, const String& content, const String& contentType=String());
};

class AsyncPrintResponse: public AsyncBasicResponse, public PrintString {
  private:
    PrintString _content;

  protected:
    virtual void assembleHead(uint8_t version) override;
    virtual bool prepareContentSendBuf(size_t space) override;

  public:
    AsyncPrintResponse(int code, const String& contentType=String());

    size_t write(const uint8_t *data, size_t len);
    size_t write(uint8_t data) { return write(&data, 1); }

    using Print::write;
};

class AsyncBufferedResponse: public AsyncBasicResponse {
  protected:
    AsyncBufferedResponse(int code, const String& contentType=String());

    virtual bool prepareContentSendBuf(size_t space) override;
    virtual void releaseSendBuf(void) override;
    virtual size_t fillBuffer(uint8_t *buf, size_t maxLen) = 0;
};

class AsyncFileResponse: public AsyncBufferedResponse {
  private:
    File _content;

  protected:
    virtual void assembleHead(uint8_t version) override;
    virtual size_t fillBuffer(uint8_t *buf, size_t maxLen) override;

  public:
    AsyncFileResponse(FS &fs, const String& path, const String& contentType=String(), bool download=false)
    : AsyncFileResponse(fs.open(path,"r"), path, contentType, download) {}

    AsyncFileResponse(File const& content, const String& path, const String& contentType=String(), bool download=false);
};

class AsyncStreamResponse: public AsyncBufferedResponse {
  private:
    Stream &_content;

  protected:
    virtual void assembleHead(uint8_t version) override;
    virtual size_t fillBuffer(uint8_t *buf, size_t maxLen) override;

  public:
    AsyncStreamResponse(int code, Stream &content, const String& contentType, size_t len);
};

class AsyncProgmemResponse: public AsyncBufferedResponse {
  private:
    const uint8_t* _content;

  protected:
    virtual void assembleHead(uint8_t version) override;
    virtual size_t fillBuffer(uint8_t *buf, size_t maxLen) override;

  public:
    AsyncProgmemResponse(int code, const uint8_t* content, const String& contentType, size_t len);
};

class AsyncCallbackResponse: public AsyncBufferedResponse {
  private:
    AwsResponseFiller _callback;

  protected:
    virtual size_t fillBuffer(uint8_t *buf, size_t maxLen) override;

  public:
    AsyncCallbackResponse(int code, AwsResponseFiller callback, const String& contentType, size_t len);
};

class AsyncChunkedResponse: public AsyncBufferedResponse {
  private:
    AwsResponseFiller _callback;
    size_t _chunkCnt;

  protected:
    virtual void assembleHead(uint8_t version) override;
    virtual size_t fillBuffer(uint8_t *buf, size_t maxLen) override;

  public:
    AsyncChunkedResponse(int code, AwsResponseFiller callback, const String& contentType);
};

#endif /* ASYNCWEBSERVERRESPONSEIMPL_H_ */
