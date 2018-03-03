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

static const char RESPONSE_CONTINUE[] PROGMEM = "HTTP/1.1 100 Continue\r\n\r\n";

bool AsyncWebHandler::_checkContinue(AsyncWebRequest &request, bool continueHeader) {
	if (continueHeader) {
		// ASSUMPTION: write always succeed
		String Resp(FPSTR(RESPONSE_CONTINUE));
		request._client.write(Resp.begin(), Resp.length());
	}
	return true;
}

/*
 * Host redirection handler
 * */

bool AsyncHostRedirWebHandler::_canHandle(AsyncWebRequest const &request) {
	if (!(method & request.method())) return false;
	if (!request.host()) {
		ESPWS_DEBUG("[%s] Host header not provided (at least not early enough)!\n",
			request._remoteIdent.c_str());
		return false;
	}
	return !request.host().equalsIgnoreCase(host);
}

bool AsyncHostRedirWebHandler::_checkContinue(AsyncWebRequest &request, bool continueHeader) {
	ESPWS_DEBUGVV("[%s] Host re-dir: [%s] -> [%s]\n",
		request._remoteIdent.c_str(), request.host().c_str(), host.c_str());
	_redirectHost(request);
	return false;
}

/*
 * Path-based handler
 * */

bool AsyncPathURIWebHandler::_canHandle(AsyncWebRequest const &request) {
	if (!(method & request.method())) return false;

	if (request.url().startsWith(path)) {
		ESPWS_DEBUGVV("[%s] '%s' prefix match '%s'\n",
			request._remoteIdent.c_str(), path.c_str(), request.url().c_str());
		return true;
	}

	if (request.url().length()+1 == path.length() && path.startsWith(request.url())) {
		ESPWS_DEBUGVV("[%s] '%s' control match '%s'\n",
		request._remoteIdent.c_str(), path.c_str(), request.url().c_str());
		return true;
	}

	return false;
}

bool AsyncPathURIWebHandler::_checkContinue(AsyncWebRequest &request, bool continueHeader) {
	if (request.url().length()+1 == path.length() && path.end()[-1] == '/') {
		ESPWS_DEBUGVV("[%s] Path re-dir: '%s'\n",
			request._remoteIdent.c_str(), request.url().c_str());
		_redirectDir(request);
		return false;
	}
	return AsyncWebHandler::_checkContinue(request, continueHeader);
}

/*
 * Static Directory & File handler
 * */

AsyncStaticWebHandler::AsyncStaticWebHandler(String const &path, Dir const& dir,
	const char* cache_control
#ifdef ADVANCED_STATIC_WEBHANDLER
	, bool write_support
#endif
):
	AsyncPathURIWebHandler(path, HTTP_STANDARD_READ
#ifdef ADVANCED_STATIC_WEBHANDLER
		| (write_support? HTTP_STANDARD_WRITE : 0)
#endif
	)
	, _dir(dir)
	, _cache_control(cache_control)
{
	// Set defaults
	//_GET_indexFile = "";
	_GET_gzLookup = true;
	_GET_gzFirst = true;
	//_onGETIndex = nullptr;
	_onGETPathNotFound = std::bind(&AsyncStaticWebHandler::_pathNotFound, this, std::placeholders::_1);
	_onGETIndexNotFound = std::bind(&AsyncStaticWebHandler::_GET_sendDirList, this, std::placeholders::_1);
	_onDirRedirect = std::bind(&AsyncStaticWebHandler::_redirectDir, this, std::placeholders::_1);
}

AsyncStaticWebHandler& AsyncStaticWebHandler::setCacheControl(const char* cache_control){
	_cache_control = cache_control;
	return *this;
}

AsyncStaticWebHandler& AsyncStaticWebHandler::setGETIndexFile(const char* filename){
	if (_onGETIndex)
		ESPWS_DEBUG("WARNING: Ineffective configuration, index handler in place!\n");
	_GET_indexFile = filename;
	return *this;
}

AsyncStaticWebHandler& AsyncStaticWebHandler::setGETLookupGZ(bool gzLookup, bool gzFirst) {
	_GET_gzLookup = gzLookup;
	_GET_gzFirst = gzFirst;
}

bool AsyncStaticWebHandler::_isInterestingHeader(String const& key) {
	return key.equalsIgnoreCase("If-None-Match") || key.equalsIgnoreCase("Accept-Encoding");
}

void AsyncStaticWebHandler::_handleRequest(AsyncWebRequest &request) {
	switch (request.method()) {
		case HTTP_GET:
		case HTTP_HEAD:
			_handleRead(request);
			break;

#ifdef ADVANCED_STATIC_WEBHANDLER
		case HTTP_PUT:
			_handleWrite(request);
			break;

		case HTTP_DELETE:
			_handleDelete(request);
			break;
#endif

		default:
			ESPWS_DEBUG("WARNING: Unimplemented method '%s'\n",
				request._server.mapMethod(request.method()));
			request.send(501);
	}
}

#ifdef ADVANCED_STATIC_WEBHANDLER
bool AsyncStaticWebHandler::_checkContinue(AsyncWebRequest &request, bool continueHeader) {
	// TODO
	return AsyncPathURIWebHandler::_checkContinue(request, continueHeader);
}
#endif

void AsyncStaticWebHandler::_handleRead(AsyncWebRequest &request) {
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
			_onGETPathNotFound(request);
			return;
		}
	} else {
		ESPWS_DEBUGVV("[%s] Path: '%s'\n", request._remoteIdent.c_str(), subpath.c_str());
	}

	if (ServeDir) {
		// We have a request on a valid dir
		if (_onGETIndex) {
			ESPWS_DEBUGVV("[%s] Dir onIndex\n", request._remoteIdent.c_str());
			_onGETIndex(request);
			return;
		} else {
			if (_GET_indexFile) {
				// Need to look up index file
				subpath+= _GET_indexFile;
			} else {
				// No index file lookup
				subpath.clear(true);
			}
		}
	}

	File CWF;
	bool gzEncode = _GET_gzLookup;
	if (gzEncode) {
		auto Header = request.getHeader("Accept-Encoding");
		gzEncode = (Header != nullptr) && Header->values.get_if([](String const &v) {
			return v.indexOf("gzip")>=0;
		}) != nullptr;
	}

	// Handle file request path
	if (subpath) {
		ESPWS_DEBUGVV("[%s] File lookup: '%s'\n",
			request._remoteIdent.c_str(), subpath.c_str());
		if (gzEncode) {
			String gzPath = subpath + ".gz";
			if (_GET_gzFirst) {
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
				_onGETPathNotFound(request);
				return;
			}
		}
	}

	if (!CWF) {
		// Dir index file not found
		ESPWS_DEBUGVV("[%s] Dir index not found\n", request._remoteIdent.c_str());
		_onGETIndexNotFound(request);
		return;
	}

	// We can serve a data file
	String etag;
	if (_cache_control){
		time_t fm = CWF.mtime();
		size_t fs = CWF.size();
		etag = "W/\""+String(fs)+'@'+String(fm,16)+'"';
		auto Header = request.getHeader("If-None-Match");
		if (Header != nullptr && Header->values.contains(etag)) {
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

void AsyncStaticWebHandler::_GET_sendDirList(AsyncWebRequest &request) {
	String subpath = request.url().substring(path.length());
	Dir CWD = subpath? _dir.openDir(subpath.c_str()) : _dir;
	if (!CWD) {
		request.send(500);
		return;
	}

	uint32_t startTS = millis();
	ESPWS_DEBUGV("[%s] Sending dir listing of '%s'\n", request._remoteIdent.c_str(), CWD.name());
	String OvfBuf;
	OvfBuf.concat(F("<html><head><title>Directory content of '"));
	OvfBuf.concat(request.url());
	OvfBuf.concat(F("'</title><style>table{width:100%;border-collapse:collapse}"
		"th{background:#DDD;text-align:right}th:first-child{text-align:left}"
		"td{text-align:right}td:first-child{text-align:left}"
		"div.footnote{font-size:small;text-align:right}</style><head>"
		"<body><h1>Directory '"));
	OvfBuf.concat(request.url());
	OvfBuf.concat(F("'</h1><hr><table><thead>"
		"<tr><th>Name</th><th>Size (bytes)</th><th>Modification Time</th></tr>"
		"</thead><tbody>"));
	if (subpath)
		OvfBuf.concat(F("<tr><td><a href='..'>(Parent directory)</a></td><td></td><td></td></tr>"));
	CWD.next(true);

	auto response = request.beginChunkedResponse(200,
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
						OvfBuf.concat("<tr><td><a href='",17);
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
					OvfBuf.concat(F("</tbody></table><hr><div class='footnote'>Served by "));
					OvfBuf.concat(request._server.VERTOKEN);
					OvfBuf.concat(F(" ("));
					OvfBuf.concat(GetPlatformAnnotation());
					OvfBuf.concat(F(") ["));
					OvfBuf.concat(endTS-startTS);
					OvfBuf.concat(F("ms]</span></body></html>"));
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
	request.send(response);
}

void AsyncStaticWebHandler::_pathNotFound(AsyncWebRequest &request) {
	request.send(404); // File not found
}

void AsyncStaticWebHandler::_handleWrite(AsyncWebRequest &request) {
	// TODO
	request.send(501);
}

void AsyncStaticWebHandler::_handleDelete(AsyncWebRequest &request) {
	// TODO
	request.send(501);
}
