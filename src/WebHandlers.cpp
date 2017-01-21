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

AsyncStaticWebHandler::AsyncStaticWebHandler(const char* uri, Dir const& dir, const char* cache_control)
  : _dir(dir), _uri(*uri=='/'?uri:(String('/')+uri)), _cache_control(cache_control)
{
  // Ensure trailing '/'
  if (_uri.end()[-1] != '/') _uri+= '/';

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
    DEBUGV("[AsyncStaticWebHandler::setIndexFile] Ineffective configuration!\n");
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
  if (request->method() == HTTP_GET && request->url().startsWith(_uri)) {
    DEBUGV("[AsyncStaticWebHandler::canHandle] Match: '%s'\n", request->url().c_str());
    // We have a match, deal with it
    _prepareRequest(request->url().substring(_uri.length()), request);
    if (!_cache_control.empty()) {
      // We interested in "If-None-Match" header to check if file was modified
      request->addInterestingHeader("If-None-Match");
    }
    return true;
  } else if (_uri.startsWith(request->url()) && request->url().length()+1 == _uri.length()) {
    DEBUGV("[AsyncStaticWebHandler::canHandle] Redir: '%s'\n", request->url().c_str());
    // Close, just need a gentle push
    _requestHandler = std::bind(&AsyncStaticWebHandler::dirRedirect, this, std::placeholders::_1);
    return true;
  }

  return false;
}

bool AsyncStaticWebHandler::_prepareRequest(String subpath, AsyncWebServerRequest *request)
{
  bool ServeDir = false;

  if (subpath.empty()) {
    DEBUGV("[AsyncStaticWebHandler::_prepareRequest] RootDir\n");
    ServeDir = true;
    // Requesting root dir
    request->_tempDir = _dir;
  } else if (subpath.end()[-1] == '/') {
    DEBUGV("[AsyncStaticWebHandler::_prepareRequest] SubDir: '%s'\n", subpath.c_str());
    ServeDir = true;
    // Requesting sub dir
    request->_tempDir = _dir.openDir(subpath.c_str());
    if (!request->_tempDir) {
      _requestHandler = _onPathNotFound;
      return false;
    }
  } else {
    DEBUGV("[AsyncStaticWebHandler::_prepareRequest] Path: '%s'\n", subpath.c_str());
  }

  if (ServeDir) {
    // We have a request on a valid dir
    if (_onIndex) {
      DEBUGV("[AsyncStaticWebHandler::_prepareRequest] Dir onIndex\n");
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
    DEBUGV("[AsyncStaticWebHandler::_prepareRequest] File lookup: '%s'\n",subpath.c_str());
    if (_gzLookup) {
      if (_gzFirst) {
        gzPath = subpath + ".gz";
        DEBUGV("[AsyncStaticWebHandler::_prepareRequest] GZFirst: '%s'\n",gzPath.c_str());
        request->_tempFile = _dir.openFile(gzPath.c_str(), "r");
        if (!request->_tempFile) {
          gzPath.clear();
          request->_tempFile = _dir.openFile(subpath.c_str(), "r");
        }
      } else {
        request->_tempFile = _dir.openFile(subpath.c_str(), "r");
        if (!request->_tempFile) {
          gzPath = subpath + ".gz";
          DEBUGV("[AsyncStaticWebHandler::_prepareRequest] !GZFirst: '%s'\n",gzPath.c_str());
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
        _requestHandler = std::bind(&AsyncStaticWebHandler::dirRedirect, this, std::placeholders::_1);
      } else {
        DEBUGV("[AsyncStaticWebHandler::_prepareRequest] File not found\n");
        // It is not a file, nor dir
        _requestHandler = _onPathNotFound;
      }
      return false;
    }
  }

  if (request->_tempFile) {
    // We can serve a data file
    if (!gzPath.empty()) {
      DEBUGV("[AsyncStaticWebHandler::_prepareRequest] GZ file '%s'\n",gzPath.c_str());
      request->_tempPath = subpath;
    }
    _requestHandler = std::bind(&AsyncStaticWebHandler::sendDataFile, this, std::placeholders::_1);
    return true;
  } else {
    DEBUGV("[AsyncStaticWebHandler::_prepareRequest] Dir index not found\n");
    // Dir index file not found
    _requestHandler = _onIndexNotFound;
    return false;
  }
}

void AsyncStaticWebHandler::handleRequest(AsyncWebServerRequest *request)
{
  if (_requestHandler)
    _requestHandler(request);
  else
    request->send(500); // Should not reach
}

void AsyncStaticWebHandler::dirRedirect(AsyncWebServerRequest *request) {
  // May not be compliant with standard (no protocol and server), but seems to work OK with most browsers
  request->redirect(request->url()+'/');
}

void AsyncStaticWebHandler::sendDirList(AsyncWebServerRequest *request) {
  DEBUGV("[AsyncStaticWebHandler::sendDirList] Forbid dir list\n");
  request->send(403); // Dir listing is forbidden
}

void AsyncStaticWebHandler::pathNotFound(AsyncWebServerRequest *request) {
  DEBUGV("[AsyncStaticWebHandler::sendDirList] Path not found\n");
  request->send(404); // File not found
}

void AsyncStaticWebHandler::sendDataFile(AsyncWebServerRequest *request) {
  time_t fm = request->_tempFile.mtime();
  size_t fs = request->_tempFile.size();
  String etag = "W/\""+String(fs)+'@'+String(fm,16)+'"';
  if (etag == request->header("If-None-Match")) {
    DEBUGV("[AsyncStaticWebHandler::_prepareRequest] Not modified\n");
    request->_tempFile.close();
    request->send(304); // Not modified
  } else {
    DEBUGV("[AsyncStaticWebHandler::_prepareRequest] Serving '%s'\n",request->_tempFile.name());
    const char* filepath = request->_tempPath.empty()? request->_tempFile.name() : request->_tempPath.c_str();
    AsyncWebServerResponse * response = new AsyncFileResponse(request->_tempFile, filepath);
    if (!_cache_control.empty()){
      response->addHeader("Cache-Control", _cache_control);
      response->addHeader("ETag", etag);
    }
    request->send(response);
  }
}
