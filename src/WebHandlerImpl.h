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
#ifndef AsynWebHandlerImpl_H_
#define AsynWebHandlerImpl_H_

#include "stddef.h"
#include <time.h>
#include <FS.h>

#include <ESPAsyncWebServer.h>

class AsyncPathURIWebHandler: public AsyncWebHandler {
  protected:
    // May not be compliant with standard (no protocol and server), but seems to work OK with most browsers
    void _redirectDir(AsyncWebRequest &request) {
      String newLocation = request.oUrl()+'/';
      if (!request.oQuery().empty())
        newLocation.concat(request.oQuery());
      request.redirect(newLocation);
    }

  public:
    String const path;
    WebRequestMethodComposite const method;

    AsyncPathURIWebHandler(const String& p, WebRequestMethodComposite m)
    : path((p[0]=='/'? "":"/")+p+(p.end()[-1]=='/'? "":"/")), method(m) {}

    virtual bool _canHandle(AsyncWebRequest const &request) override;
    virtual bool _checkContinue(AsyncWebRequest &request, bool continueHeader) override;
};

class AsyncStaticWebHandler: public AsyncPathURIWebHandler {
  protected:
    Dir _dir;
    String _cache_control;
    String _indexFile;
    bool _gzLookup, _gzFirst;

    void _sendDirList(AsyncWebRequest &request);
    void _pathNotFound(AsyncWebRequest &request);

  public:
    ArRequestHandlerFunction _onIndex;
    ArRequestHandlerFunction _onPathNotFound;
    ArRequestHandlerFunction _onIndexNotFound;
    ArRequestHandlerFunction _onDirRedirect;

    AsyncStaticWebHandler(const String& path, Dir const& dir, const char* cache_control);

    virtual bool _isInterestingHeader(String const& key) override;

    AsyncStaticWebHandler& setCacheControl(const char* cache_control);
    AsyncStaticWebHandler& lookupGZ(bool gzLookup, bool gzFirst);
    AsyncStaticWebHandler& setIndexFile(const char* filename);

    virtual void _handleRequest(AsyncWebRequest &request) override;

#ifdef HANDLE_REQUEST_CONTENT
    virtual bool _handleBody(AsyncWebRequest &request, size_t offset, void *buf, size_t size) override {
      // Do not expect request body
      return false;
    }

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
    virtual bool _handleParamData(AsyncWebRequest &request, String const& name, String const& contentType,
                                  size_t contentLength, size_t offset, void *buf, size_t size) override {
      // Do not expect request param
      return false;
    }
#endif
#endif
};

class AsyncCallbackWebHandler: public AsyncPathURIWebHandler {
  public:
    StringArray interestedHeaders;
    ArRequestHandlerFunction onRequest;
#ifdef HANDLE_REQUEST_CONTENT
    ArBodyHandlerFunction onBody;
#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
    ArParamDataHandlerFunction onParamData;
#endif
#endif

    AsyncCallbackWebHandler(const String& path, WebRequestMethodComposite method = HTTP_ANY)
    : AsyncPathURIWebHandler(path, method) {}

    virtual bool _isInterestingHeader(String const& key) override
    { return interestedHeaders.containsIgnoreCase(key); }

    virtual void _handleRequest(AsyncWebRequest &request) override {
      if (onRequest) onRequest(request);
      else request.send(500);
    }

#ifdef HANDLE_REQUEST_CONTENT
    virtual bool _handleBody(AsyncWebRequest &request, size_t offset, void *buf, size_t size) override
    { return onBody? onBody(request, offset, buf, size) : false; }

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
    virtual bool _handleParamData(AsyncWebRequest &request, String const& name, String const& contentType,
                                  size_t contentLength, size_t offset, void *buf, size_t size) override
    { return onParamData? onParamData(request, name, contentType, contentLength, offset, buf, size) : false; }
#endif
#endif
};

#endif /* AsynWebHandlerImpl_H_ */
