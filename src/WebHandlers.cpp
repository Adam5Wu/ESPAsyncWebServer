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
#include "ESPAsyncWebServer.h"
#include "WebHandlerImpl.h"

/*
 * Path-based URI handler
 * */

bool AsyncPathURIWebHandler::canHandle(AsyncWebServerRequest *request) {
  if (!(_method & request->method())) return false;

  if (request->url().startsWith(_uri)) {
    ESPWS_DEBUGVV("[AsyncPathURIWebHandler::canHandle] Match: '%s'\n", request->url().c_str());
    _requestHandler = NULL;
    return true;
  }

  if (_uri.startsWith(request->url()) && request->url().length()+1 == _uri.length()) {
    // Matched directory without final slash
    ESPWS_DEBUGVV("[AsyncPathURIWebHandler::canHandle] Redir: '%s'\n", request->url().c_str());
    _requestHandler = std::bind(&AsyncPathURIWebHandler::dirRedirect, this, std::placeholders::_1);
    return true;
  }

  return false;
}

/*
 * Static Directory & File handler
 * */

AsyncStaticWebHandler::AsyncStaticWebHandler(const String& uri, Dir const& dir, const char* cache_control)
  : AsyncPathURIWebHandler(uri, HTTP_GET), _dir(dir), _cache_control(cache_control)
{
  // Set defaults
  //_indexFile = "";
  _gzLookup = true;
  _gzFirst = true;
  //_onIndex = NULL;
  _onPathNotFound = std::bind(&AsyncStaticWebHandler::pathNotFound, this, std::placeholders::_1);
  _onIndexNotFound = std::bind(&AsyncStaticWebHandler::sendDirList, this, std::placeholders::_1);
}

AsyncStaticWebHandler& AsyncStaticWebHandler::setCacheControl(const char* cache_control){
  _cache_control = String(cache_control);
  return *this;
}

AsyncStaticWebHandler& AsyncStaticWebHandler::setIndexFile(const char* filename){
  if (_onIndex)
    ESPWS_DEBUG("[AsyncStaticWebHandler::setIndexFile] WARNING: Ineffective configuration!\n");
  _indexFile = filename;
  return *this;
}

AsyncStaticWebHandler& AsyncStaticWebHandler::lookupGZ(bool gzLookup, bool gzFirst) {
  _gzLookup = gzLookup;
  _gzFirst = gzFirst;
}

AsyncStaticWebHandler& AsyncStaticWebHandler::onIndex(ArRequestHandlerFunction const& onIndex) {
  _onIndex = onIndex;
}

AsyncStaticWebHandler& AsyncStaticWebHandler::onPathNotFound(ArRequestHandlerFunction const& onPathNotFound) {
  _onPathNotFound = onPathNotFound;
}

AsyncStaticWebHandler& AsyncStaticWebHandler::onIndexNotFound(ArRequestHandlerFunction const& onIndexNotFound) {
  _onIndexNotFound = onIndexNotFound;
}

bool AsyncStaticWebHandler::canHandle(AsyncWebServerRequest *request){
  bool Ret = AsyncPathURIWebHandler::canHandle(request);
  if (Ret && !_requestHandler) {
    // We have a match, deal with it
    _prepareRequest(request->url().substring(_uri.length()), request);
    if (!_cache_control.empty()) {
      // We interested in "If-None-Match" header to check if file was modified
      request->addInterestingHeader("If-None-Match");
    }
  }

  return Ret;
}

bool AsyncStaticWebHandler::_prepareRequest(String&& subpath, AsyncWebServerRequest *request)
{
  bool ServeDir = false;

  if (subpath.empty()) {
    ESPWS_DEBUGVV("[AsyncStaticWebHandler::_prepareRequest] RootDir\n");
    ServeDir = true;
    // Requesting root dir
    request->_tempDir = _dir;
  } else if (subpath.end()[-1] == '/') {
    ESPWS_DEBUGVV("[AsyncStaticWebHandler::_prepareRequest] SubDir: '%s'\n", subpath.c_str());
    ServeDir = true;
    // Requesting sub dir
    request->_tempDir = _dir.openDir(subpath.c_str());
    if (!request->_tempDir) {
      _requestHandler = _onPathNotFound;
      return false;
    }
  } else {
    ESPWS_DEBUGVV("[AsyncStaticWebHandler::_prepareRequest] Path: '%s'\n", subpath.c_str());
  }

  if (ServeDir) {
    // We have a request on a valid dir
    if (_onIndex) {
      ESPWS_DEBUGVV("[AsyncStaticWebHandler::_prepareRequest] Dir onIndex\n");
      _requestHandler = _onIndex;
      return true;
    } else {
      if (!_indexFile.empty()) {
        // Need to look up index file
        subpath+= _indexFile;
      } else {
        // No index file lookup
        subpath.clear();
      }
    }
  }

  // Handle file request path
  String gzPath;
  if (!subpath.empty()) {
    ESPWS_DEBUGVV("[AsyncStaticWebHandler::_prepareRequest] File lookup: '%s'\n",subpath.c_str());
    if (_gzLookup) {
      if (_gzFirst) {
        gzPath = subpath + ".gz";
        ESPWS_DEBUGVV("[AsyncStaticWebHandler::_prepareRequest] GZFirst: '%s'\n",gzPath.c_str());
        request->_tempFile = _dir.openFile(gzPath.c_str(), "r");
        if (!request->_tempFile) {
          gzPath.clear();
          request->_tempFile = _dir.openFile(subpath.c_str(), "r");
        }
      } else {
        request->_tempFile = _dir.openFile(subpath.c_str(), "r");
        if (!request->_tempFile) {
          gzPath = subpath + ".gz";
          ESPWS_DEBUGVV("[AsyncStaticWebHandler::_prepareRequest] !GZFirst: '%s'\n",gzPath.c_str());
          request->_tempFile = _dir.openFile(gzPath.c_str(), "r");
        }
      }
    } else {
      request->_tempFile = _dir.openFile(subpath.c_str(), "r");
    }

    if (!request->_tempFile && !ServeDir) {
      // Check the possibility that it is a dir
      if (_dir.isDir(subpath.c_str())) {
        // It is a dir that need a gentle push
        _requestHandler = std::bind(&AsyncPathURIWebHandler::dirRedirect, this, std::placeholders::_1);
      } else {
        ESPWS_DEBUGVV("[AsyncStaticWebHandler::_prepareRequest] File not found\n");
        // It is not a file, nor dir
        _requestHandler = _onPathNotFound;
      }
      return false;
    }
  }

  if (request->_tempFile) {
    // We can serve a data file
    if (!gzPath.empty()) {
      ESPWS_DEBUGVV("[AsyncStaticWebHandler::_prepareRequest] GZ file '%s'\n",gzPath.c_str());
      request->_tempPath = std::move(subpath);
    }
    _requestHandler = std::bind(&AsyncStaticWebHandler::sendDataFile, this, std::placeholders::_1);
    return true;
  } else {
    ESPWS_DEBUGVV("[AsyncStaticWebHandler::_prepareRequest] Dir index not found\n");
    // Dir index file not found
    _requestHandler = _onIndexNotFound;
    return false;
  }
}

void AsyncStaticWebHandler::sendDirList(AsyncWebServerRequest *request) {
  ESPWS_DEBUGVV("[AsyncStaticWebHandler::sendDirList] Forbid dir listing\n");
  request->send(403); // Dir listing is forbidden
}

void AsyncStaticWebHandler::pathNotFound(AsyncWebServerRequest *request) {
  request->send(404); // File not found
}

void AsyncStaticWebHandler::sendDataFile(AsyncWebServerRequest *request) {
  time_t fm = request->_tempFile.mtime();
  size_t fs = request->_tempFile.size();
  String etag = "W/\""+String(fs)+'@'+String(fm,16)+'"';
  if (etag == request->header("If-None-Match")) {
    request->_tempFile.close();
    request->send(304); // Not modified
  } else {
    ESPWS_DEBUGVV("[AsyncStaticWebHandler::_prepareRequest] Serving '%s'\n",request->_tempFile.name());
    const char* filepath = request->_tempPath.empty()? request->_tempFile.name() : request->_tempPath.c_str();
    AsyncWebServerResponse * response = new AsyncFileResponse(request->_tempFile, filepath);
    if (!_cache_control.empty()){
      response->addHeader("Cache-Control", _cache_control);
      response->addHeader("ETag", etag);
    }
    request->send(response);
  }
}
