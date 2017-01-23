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
#ifndef ASYNCWEBSERVERHANDLERIMPL_H_
#define ASYNCWEBSERVERHANDLERIMPL_H_

#include "stddef.h"
#include <time.h>
#include <FS.h>

class AsyncPathURIWebHandler: public AsyncWebHandler {
  protected:
    String _uri;
    WebRequestMethodComposite _method;
    ArRequestHandlerFunction _requestHandler;

  public:
    AsyncPathURIWebHandler(const String& uri, WebRequestMethodComposite method) {
      setUri(uri);
      setMethod(method);
    }

    void setUri(const String& uri) {
      _uri = uri[0]=='/'? uri : '/' + uri;
      // Ensure trailing '/'
      if (_uri.end()[-1] != '/') _uri+= '/';
    }
    void setMethod(WebRequestMethodComposite method) { _method = method; }

    void dirRedirect(AsyncWebServerRequest *request) {
      // May not be compliant with standard (no protocol and server), but seems to work OK with most browsers
      request->redirect(request->url()+'/');
    }
    virtual bool canHandle(AsyncWebServerRequest *request) override;
    virtual void handleRequest(AsyncWebServerRequest *request) override {
      if (_requestHandler) _requestHandler(request);
      else request->send(500); // Should not reach
    }
};

class AsyncStaticWebHandler: public AsyncPathURIWebHandler {
  private:
    bool _prepareRequest(String&& subpath, AsyncWebServerRequest *request);

  protected:
    Dir _dir;
    String _cache_control;
    String _indexFile;
    bool _gzLookup, _gzFirst;

    ArRequestHandlerFunction _onIndex;
    ArRequestHandlerFunction _onPathNotFound;
    ArRequestHandlerFunction _onIndexNotFound;

    virtual void sendDirList(AsyncWebServerRequest *request);
    virtual void pathNotFound(AsyncWebServerRequest *request);
    virtual void sendDataFile(AsyncWebServerRequest *request);

  public:
    AsyncStaticWebHandler(const String& uri, Dir const& dir, const char* cache_control);

    virtual bool canHandle(AsyncWebServerRequest *request) override;

    AsyncStaticWebHandler& setCacheControl(const char* cache_control);
    AsyncStaticWebHandler& lookupGZ(bool gzLookup, bool gzFirst);
    AsyncStaticWebHandler& setIndexFile(const char* filename);

    AsyncStaticWebHandler& onIndex(ArRequestHandlerFunction const& onIndex);
    AsyncStaticWebHandler& onPathNotFound(ArRequestHandlerFunction const& onPathNotFound);
    AsyncStaticWebHandler& onIndexNotFound(ArRequestHandlerFunction const& onIndexNotFound);
};

class AsyncCallbackWebHandler: public AsyncPathURIWebHandler {
  protected:
    WebRequestMethodComposite _method;
    ArRequestHandlerFunction _onRequest;
    ArUploadHandlerFunction _onUpload;
    ArBodyHandlerFunction _onBody;

  public:
    AsyncCallbackWebHandler(const String& uri = String(), WebRequestMethodComposite method = HTTP_ANY)
    : AsyncPathURIWebHandler(uri, method), _onRequest(NULL), _onUpload(NULL), _onBody(NULL) {}

    void onRequest(ArRequestHandlerFunction const& fn){ _onRequest = fn; }
    void onUpload(ArUploadHandlerFunction const& fn){ _onUpload = fn; }
    void onBody(ArBodyHandlerFunction const& fn){ _onBody = fn; }

    virtual bool canHandle(AsyncWebServerRequest *request) override {
      if (AsyncPathURIWebHandler::canHandle(request)) {
        if (!_requestHandler) request->addInterestingHeader("ANY");
        else _requestHandler = _onRequest;
        return true;
      }
      return false;
    }

    virtual void handleUpload(AsyncWebServerRequest *request, const String& filename, size_t index,
                              uint8_t *data, size_t len, bool final) override final {
      if(_onUpload)
        _onUpload(request, filename, index, data, len, final);
    }
    virtual void handleBody(AsyncWebServerRequest *request, uint8_t *data,
                            size_t len, size_t index, size_t total) override final {
      if(_onBody)
        _onBody(request, data, len, index, total);
    }
};

#endif /* ASYNCWEBSERVERHANDLERIMPL_H_ */
