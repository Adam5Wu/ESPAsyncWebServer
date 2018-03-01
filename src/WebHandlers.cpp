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

#include "WebHandlerImpl.h"

/*
 * Abstract handler
 * */

static const char RESPONSE_CONTINUE[] = "HTTP/1.1 100 Continue\r\n\r\n";

bool AsyncWebHandler::_checkContinue(AsyncWebRequest &request, bool continueHeader) {
	if (continueHeader) {
		// ASSUMPTION: write always succeed
		request._client.write(RESPONSE_CONTINUE, sizeof(RESPONSE_CONTINUE));
	}
	return true;
}

/*
 * Path-based URI handler
 * */

bool AsyncPathURIWebHandler::_canHandle(AsyncWebRequest const &request) {
	if (!(method & request.method())) return false;

	if (request.url().startsWith(path)) {
		ESPWS_DEBUGVV("[%s] '%s' matches '%s'\n",
			request._remoteIdent.c_str(), path.c_str(), request.url().c_str());
		return true;
	}

	if (request.url().length()+1 == path.length() && path.startsWith(request.url())) {
		// Matched directory without final slash
		return true;
	}

	return false;
}

bool AsyncPathURIWebHandler::_checkContinue(AsyncWebRequest &request, bool continueHeader) {
	if (request.url().length()+1 == path.length()) {
		ESPWS_DEBUGVV("[%s] Re-Dir: '%s'\n",
			request._remoteIdent.c_str(), request.url().c_str());
		_redirectDir(request);
		return false;
	}
	return AsyncWebHandler::_checkContinue(request, continueHeader);
}

/*
 * Static Directory & File handler
 * */

AsyncStaticWebHandler::AsyncStaticWebHandler(const String& path, Dir const& dir,
	const char* cache_control)
	: AsyncPathURIWebHandler(path, HTTP_GET), _dir(dir), _cache_control(cache_control)
{
	// Set defaults
	//_indexFile = "";
	_gzLookup = true;
	_gzFirst = true;
	//_onIndex = NULL;
	_onPathNotFound = std::bind(&AsyncStaticWebHandler::_pathNotFound, this, std::placeholders::_1);
	_onIndexNotFound = std::bind(&AsyncStaticWebHandler::_sendDirList, this, std::placeholders::_1);
	_onDirRedirect = std::bind(&AsyncStaticWebHandler::_redirectDir, this, std::placeholders::_1);
}

AsyncStaticWebHandler& AsyncStaticWebHandler::setCacheControl(const char* cache_control){
	_cache_control = cache_control;
	return *this;
}

AsyncStaticWebHandler& AsyncStaticWebHandler::setIndexFile(const char* filename){
	if (_onIndex)
		ESPWS_DEBUG("WARNING: Ineffective configuration, index handler in place!\n");
	_indexFile = filename;
	return *this;
}

AsyncStaticWebHandler& AsyncStaticWebHandler::lookupGZ(bool gzLookup, bool gzFirst) {
	_gzLookup = gzLookup;
	_gzFirst = gzFirst;
}

bool AsyncStaticWebHandler::_isInterestingHeader(String const& key) {
	return key.equalsIgnoreCase("If-None-Match") || key.equalsIgnoreCase("Accept-Encoding");
}

void AsyncStaticWebHandler::_handleRequest(AsyncWebRequest &request) {
	String subpath = request.url().substring(path.length());

	bool ServeDir = false;
	if (!subpath) {
		// Requesting root dir
		ESPWS_DEBUGVV("[%s] RootDir\n", request._remoteIdent.c_str());
		ServeDir = true;
	} else if (subpath.end()[-1] == '/') {
		// Requesting sub dir
		ESPWS_DEBUGVV("[%s] SubDir: '%s'\n", request._remoteIdent.c_str(), subpath.c_str());
		ServeDir = true;
		Dir CWD = _dir.openDir(subpath.c_str());
		if (!CWD) {
			_onPathNotFound(request);
			return;
		}
	} else {
		ESPWS_DEBUGVV("[%s] Path: '%s'\n", request._remoteIdent.c_str(), subpath.c_str());
	}

	if (ServeDir) {
		// We have a request on a valid dir
		if (_onIndex) {
			ESPWS_DEBUGVV("[%s] Dir onIndex\n", request._remoteIdent.c_str());
			_onIndex(request);
			return;
		} else {
			if (_indexFile) {
				// Need to look up index file
				subpath+= _indexFile;
			} else {
				// No index file lookup
				subpath.clear(true);
			}
		}
	}

	File CWF;
	bool gzEncode = _gzLookup;
	if (gzEncode) {
		auto Header = request.getHeader("Accept-Encoding");
		gzEncode = (Header != NULL) && Header->values.get_if([](String const &v) {
			return v.indexOf("gzip")>=0;
		}) != NULL;
	}

	// Handle file request path
	if (subpath) {
		ESPWS_DEBUGVV("[%s] File lookup: '%s'\n",
			request._remoteIdent.c_str(), subpath.c_str());
		if (gzEncode) {
			String gzPath = subpath + ".gz";
			if (_gzFirst) {
				ESPWS_DEBUGVV("[%s] GZFirst: '%s'\n",
					request._remoteIdent.c_str(), gzPath.c_str());
				CWF = _dir.openFile(gzPath.c_str(), "r");
				if (!CWF) {
					gzEncode = false;
					CWF = _dir.openFile(subpath.c_str(), "r");
				}
			} else {
				CWF = _dir.openFile(subpath.c_str(), "r");
				if (!CWF) {
					ESPWS_DEBUGVV("[%s] !GZFirst: '%s'\n",
						request._remoteIdent.c_str(), gzPath.c_str());
					CWF = _dir.openFile(gzPath.c_str(), "r");
				} else gzEncode = false;
			}
		} else CWF = _dir.openFile(subpath.c_str(), "r");

		if (!CWF && !ServeDir) {
			// Check the possibility that it is a dir
			if (_dir.isDir(subpath.c_str())) {
				// It is a dir that needs a gentle push
				ESPWS_DEBUGVV("[%s] Dir redirect\n", request._remoteIdent.c_str());
				_onDirRedirect(request);
				return;
			} else {
				// It is not a file, nor dir
				ESPWS_DEBUGVV("[%s] File not found\n", request._remoteIdent.c_str());
				_onPathNotFound(request);
				return;
			}
		}
	}

	if (!CWF) {
		// Dir index file not found
		ESPWS_DEBUGVV("[%s] Dir index not found\n", request._remoteIdent.c_str());
		_onIndexNotFound(request);
		return;
	}

	// We can serve a data file
	String etag;
	if (_cache_control){
		time_t fm = CWF.mtime();
		size_t fs = CWF.size();
		etag = "W/\""+String(fs)+'@'+String(fm,16)+'"';
		auto Header = request.getHeader("If-None-Match");
		if (Header != NULL && Header->values.contains(etag)) {
			request.send(304); // Not modified
			return;
		}
	}

	ESPWS_DEBUGVV("[%s] Serving '%s'\n", request._remoteIdent.c_str(), CWF.name());
	AsyncWebResponse * response = new AsyncFileResponse(CWF, subpath);
	if (_cache_control) {
		response->addHeader("Cache-Control", _cache_control.c_str());
		response->addHeader("ETag", etag.c_str());
	}
	if (gzEncode) {
		response->addHeader("Content-Encoding", "gzip");
	}
	request.send(response);
}

void AsyncStaticWebHandler::_sendDirList(AsyncWebRequest &request) {
	String subpath = request.url().substring(path.length());
	Dir CWD = subpath? _dir.openDir(subpath.c_str()) : _dir;
	if (!CWD) {
		request.send(500);
		return;
	}

	uint32_t startTS = millis();
	ESPWS_DEBUGV("[%s] Sending dir listing of '%s'\n", request._remoteIdent.c_str(), CWD.name());
	String OvfBuf;
	OvfBuf.concat("<html><head><title>Directory content of '");
	OvfBuf.concat(request.url());
	OvfBuf.concat("'</title><style>table{width:100%;border-collapse:collapse}"
		"th{background:#DDD;text-align:right}th:first-child{text-align:left}"
		"td{text-align:right}td:first-child{text-align:left}"
		"div.footnote{font-size:small;text-align:right}</style><head>"
		"<body><h1>Directory '");
	OvfBuf.concat(request.url());
	OvfBuf.concat("'</h1><hr><table><thead>"
		"<tr><th>Name</th><th>Size (bytes)</th><th>Modification Time</th></tr>"
		"</thead><tbody>");
	if (subpath)
		OvfBuf.concat("<tr><td><a href='..'>(Parent directory)</a></td><td></td><td></td></tr>");
	CWD.next(true);

	request.sendChunked(200,
		[=,&request](uint8_t* buf, size_t len, size_t offset) mutable -> size_t {
			size_t outLen = 0;
			if (OvfBuf) {
				outLen = OvfBuf.length() < len? OvfBuf.length(): len;
				memcpy(buf,OvfBuf.begin(),outLen);
				if (outLen >= OvfBuf.length()) OvfBuf.clear();
				else OvfBuf.remove(0, outLen);
			}
			while (CWD && outLen < len) {
				if (CWD.entryName()) {
					{
						String EntryRef = CWD.entryName();
						if (CWD.isEntryDir()) EntryRef.concat('/');
						OvfBuf.concat("<tr><td><a href='");
						OvfBuf.concat(EntryRef);
						OvfBuf.concat("'>",2);
						OvfBuf.concat(EntryRef);
					}
					OvfBuf.concat("</a></td><td>",13);
					OvfBuf.concat(CWD.entrySize());
					OvfBuf.concat("</td><td>",9);
					time_t mtime = CWD.entryMtime();
					char strbuf[30];
					OvfBuf.concat(ctime_r(&mtime, strbuf));
					OvfBuf.concat("</td></tr>",10);
					CWD.next();
				} else {
					uint32_t endTS = millis();
					OvfBuf.concat("</tbody></table><hr><div class='footnote'>Served by ");
					OvfBuf.concat(request._server.VERTOKEN);
					OvfBuf.concat(" (",2);
					OvfBuf.concat(GetPlatformAnnotation());
					OvfBuf.concat(") [",3);
					OvfBuf.concat(endTS-startTS);
					OvfBuf.concat("ms]</span></body></html>");
					CWD = Dir();
				}
				size_t moveLen = OvfBuf.length() < len-outLen? OvfBuf.length(): len-outLen;
				memcpy(buf+outLen,OvfBuf.begin(),moveLen);
				if (moveLen >= OvfBuf.length()) OvfBuf.clear();
				else OvfBuf.remove(0, moveLen);
				outLen+= moveLen;
			}
			return outLen;
		}, "text/html");
}

void AsyncStaticWebHandler::_pathNotFound(AsyncWebRequest &request) {
	request.send(404); // File not found
}
