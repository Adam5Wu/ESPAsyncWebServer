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

#include <Units.h>

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

bool AsyncPathURIWebHandler::_checkPathRedirectOrContinue(AsyncWebRequest &request,
	bool continueHeader) {
	if (request.url().length()+1 == path.length() && path.end()[-1] == '/') {
		ESPWS_DEBUGVV("[%s] Path re-dir: '%s'\n",
			request._remoteIdent.c_str(), request.url().c_str());
		_redirectDir(request);
		return false;
	}
	return true;
}

bool AsyncPathURIWebHandler::_checkContinue(AsyncWebRequest &request, bool continueHeader) {
	if (!_checkPathRedirectOrContinue(request, continueHeader)) return false;
	return AsyncWebHandler::_checkContinue(request, continueHeader);
}

/*
 * Static Directory & File handler
 * */

AsyncStaticWebHandler::AsyncStaticWebHandler(String const &path, Dir const & dir,
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
#ifdef ADVANCED_STATIC_WEBHANDLER
	, _uploads(nullptr)
#endif
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

bool AsyncStaticWebHandler::_isInterestingHeader(AsyncWebRequest const &request, String const & key) {
	switch (request.method()) {
		case HTTP_GET:
		case HTTP_HEAD:
			return key.equalsIgnoreCase("If-None-Match");
			break;
	}
	return false;
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
		Dir CWD = _dir.openDir(subpath);
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

	bool gzEncode = _GET_gzLookup && (request.acceptEncoding().indexOf("gzip") >= 0);

	File CWF;
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
			if (_dir.isDir(subpath)) {
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
		auto Header = request.getHeader(F("If-None-Match"));
		if (Header != nullptr && Header->values.contains(etag)) {
			request.send(304); // Not modified
			return;
		}
	}

	ESPWS_DEBUGVV("[%s] Serving '%s'\n", request._remoteIdent.c_str(), CWF.name());
	AsyncWebResponse * response = new AsyncFileResponse(CWF, subpath);
	if (_cache_control) {
		response->addHeader(F("Cache-Control"), _cache_control);
		response->addHeader(F("ETag"), etag);
	}
	if (gzEncode) {
		response->addHeader(F("Content-Encoding"), F("gzip"));
	}
	request.send(response);
}

void AsyncStaticWebHandler::_GET_sendDirList(AsyncWebRequest &request) {
	String subpath = request.url().substring(path.length());
	Dir CWD = subpath? _dir.openDir(subpath) : _dir;
	if (!CWD) {
		ESPWS_DEBUGV("[%s] Unable to locate dir '%s'\n",
			request._remoteIdent.c_str(), subpath.c_str());
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
		".footnote{font-size:small}.left{float:left}.right{float:right}</style><head>"
		"<body><h1>Directory '"));
	OvfBuf.concat(request.url());
	OvfBuf.concat(F("'</h1><hr><table><thead>"
		"<tr><th>Name</th><th>Content</th><th>Modification Time</th></tr>"
		"</thead><tbody>"));
	if (subpath)
		OvfBuf.concat(F("<tr><td><a href='..'>(Parent folder)</a></td><td></td><td></td></tr>"));
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
						OvfBuf.concat(F("<tr><td><a href='"));
						OvfBuf.concat(EntryRef);
						OvfBuf.concat(F("'>"));
						OvfBuf.concat(EntryRef);
					}
					OvfBuf.concat(F("</a></td><td>"));
					if (CWD.isEntryDir()) {
						// Feed the dog before it bites
						ESP.wdtFeed();
						OvfBuf.concat(F("&lt;"));
						Dir _subdir = CWD.openEntryDir();
						if (_subdir) {
							size_t file_count = 0, dir_count = 0;
							while (_subdir.next()) {
								if (_subdir.isEntryDir()) ++dir_count;
								else ++file_count;
							}
							if (file_count+dir_count) {
								if (file_count) {
									OvfBuf.concat(file_count);
									OvfBuf.concat(F(" file"));
									if (file_count>1) OvfBuf.concat('s');
								}
								if (dir_count) {
									if (file_count) OvfBuf.concat(F(", "));
									OvfBuf.concat(dir_count);
									OvfBuf.concat(F(" folder"));
									if (dir_count>1) OvfBuf.concat('s');
								}
							} else {
								OvfBuf.concat(F("empty"));
							}
						} else {
							OvfBuf.concat(F("inaccessible"));
						}
						OvfBuf.concat(F("&gt;"));
					} else {
						OvfBuf.concat(ToString(CWD.entrySize(),SizeUnit::BYTE,true));
					}
					OvfBuf.concat(F("</td><td>"));
					time_t mtime = CWD.entryMtime();
					char strbuf[30];
					OvfBuf.concat(ctime_r(&mtime, strbuf));
					OvfBuf.concat(F("</td></tr>"));
					CWD.next();
				} else {
					uint32_t endTS = millis();
					OvfBuf.concat(F("</tbody></table><hr><div class='footnote'>"
						"<span class='left'>Served by "));
					OvfBuf.concat(FPSTR(AsyncWebServer::VERTOKEN));
					OvfBuf.concat(F(" ("));
					OvfBuf.concat(GetPlatformSignature());
					OvfBuf.concat(F(")</span><span class='right'>Generated in "));
					OvfBuf.concat(endTS-startTS);
					OvfBuf.concat(F("ms</span></div></body></html>"));
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

#ifdef ADVANCED_STATIC_WEBHANDLER

bool AsyncStaticWebHandler::_checkContinue(AsyncWebRequest &request, bool continueHeader) {
	if (!_checkPathRedirectOrContinue(request, continueHeader)) return false;

	switch (request.method()) {
		case HTTP_GET:
		case HTTP_HEAD:
			break;

		case HTTP_PUT:
			if (!_checkContinueCanWrite(request, continueHeader)) return false;
			break;

		case HTTP_DELETE:
			if (!_checkContinueCanDelete(request, continueHeader)) return false;
			break;

		default:
			ESPWS_DEBUG("WARNING: Unimplemented method '%s'\n",
			request._server.mapMethod(request.method()));
			request.send(501);
			return false;
	}
	return AsyncWebHandler::_checkContinue(request, continueHeader);
}

void AsyncStaticWebHandler::_terminateRequest(AsyncWebRequest &request) {
	if (request.method() == HTTP_PUT) {
		_uploads.remove_if([&](UploadRec const &r){
			return r.req == &request;
		});
	}
}

bool AsyncStaticWebHandler::_checkContinueCanWrite(AsyncWebRequest &request, bool continueHeader) {
	String subpath = request.url().substring(path.length());

	// Check if the path is pointing at a directory
	bool ServeDir = !subpath || subpath.end()[-1] == '/' || _dir.isDir(subpath);
	if (ServeDir) {
		ESPWS_DEBUGVV("[%s] Cannot upload a dir: '%s'\n",
			request._remoteIdent.c_str(), subpath? subpath.c_str() : "/");
		request.send(400);
		return false;
	}

	// Check content length
	if (request.contentLength() == -1) {
		ESPWS_DEBUGVV("[%s] Missing content-length header\n", request._remoteIdent.c_str());
		request.send(411);
		return false;
	}

	// Check if parent directory exists
	String ParentPath = pathGetParent(subpath);
	if (ParentPath && !_dir.isDir(ParentPath)) {
		ESPWS_DEBUGVV("[%s] Unsatisfied parent dir: '%s'\n",
			request._remoteIdent.c_str(), ParentPath.c_str());
		request.send(412);
		return false;
	}

	// Check if we already have a record
	if (_uploads.get_if([&](UploadRec const &r){
		return r.req == &request;
		})) {
		ESPWS_DEBUGVV("[%s] Upload record collision\n", request._remoteIdent.c_str());
		request.send(500);
		return false;
	}

	// Try create temporary upload file
	String upload_path = subpath;
	upload_path.concat(F("._upload_"));
	File _file = _dir.openFile(upload_path.c_str(),"w");
	if (!_file) {
		ESPWS_DEBUGVV("[%s] Unable to create upload file: '%s'\n",
			request._remoteIdent.c_str(), upload_path.c_str());
		request.send(500);
		return false;
	}
	// Stash file record
	_uploads.append({&request, _file, 0});
	return true;
}

bool AsyncStaticWebHandler::_handleBody(AsyncWebRequest &request,
	size_t offset, void *buf, size_t size) {
	if (request.method() == HTTP_PUT) {
		// Check if upload record has been established
		UploadRec* pRec = _uploads.get_if([&](UploadRec const &r){
			return r.req == &request;
		});
		if (!pRec) {
			ESPWS_DEBUG("[%s] WARNING: Upload record not available\n",
				request._remoteIdent.c_str());
			return false;
		}
		if (pRec->pos != offset) {
			ESPWS_DEBUG("[%s] WARNING: Upload content not aligned (expect %d, got %d)\n",
				request._remoteIdent.c_str(), pRec->pos, offset);
			return false;
		}
		if (pRec->pos + size > request.contentLength()) {
			ESPWS_DEBUG("[%s] WARNING: Upload content in excess (expect %d, got %d)\n",
				request._remoteIdent.c_str(), request.contentLength(), pRec->pos + size);
			return false;
		}
		size_t bufofs = 0;
		while (size > bufofs) {
			size_t outlen = pRec->file.write(((uint8_t*)buf)+bufofs, size-bufofs);
			if (outlen < 0) {
				ESPWS_DEBUG("[%s] WARNING: Upload file write failed!\n",
					request._remoteIdent.c_str());
				return false;
			}
			pRec->pos+= outlen;
			bufofs += outlen;
		}
		ESPWS_DEBUGVV("[%s] Upload written %d ->@%d\n",
			request._remoteIdent.c_str(), size, pRec->pos);
		return true;
	}

	// Do not expect request body
	return false;
}

bool AsyncStaticWebHandler::_checkContinueCanDelete(AsyncWebRequest &request, bool continueHeader) {
	String subpath = request.url().substring(path.length());

	// Check if the path is pointing at root directory
	if (!subpath) {
		ESPWS_DEBUGVV("[%s] Cannot delete root dir\n", request._remoteIdent.c_str());
		request.send(403);
		return false;
	}
	return true;
}

void AsyncStaticWebHandler::_handleWrite(AsyncWebRequest &request) {
	// Check if upload record has been established
	UploadRec rec;
	if (!_uploads.remove_nth_if(0,[&](UploadRec const &r){
			return r.req == &request;
		}, [&](UploadRec const &r){
			return rec = std::move(r), true;
		})) {
		ESPWS_DEBUG("[%s] WARNING: Upload record not available\n",
			request._remoteIdent.c_str());
		request.send(400);
		return;
	}
	if (rec.pos != request.contentLength()) {
		ESPWS_DEBUG("[%s] WARNING: Upload content in-exact (expect %d, got %d)\n",
			request._remoteIdent.c_str(), request.contentLength(), rec.pos);
		request.send(417);
		return;
	}
	String upname = pathGetEntryName(request.url());
	if (rec.file.rename(upname)) {
		request.send(204);
		return;
	} else {
		ESPWS_DEBUG("[%s] WARNING: Upload file rename failed '%s' -> '%s'\n",
			request._remoteIdent.c_str(),
			pathGetEntryName(rec.file.name()).c_str(), upname.c_str());
		request.send(500);
		return;
	}
}

void AsyncStaticWebHandler::_handleDelete(AsyncWebRequest &request) {
	String subpath = request.url().substring(path.length());

	if (_dir.remove(subpath)) {
		request.send(204);
		return;
	} else {
		if (!_dir.exists(subpath)) {
			ESPWS_DEBUG("[%s] WARNING: Entry '%s' does not exist\n",
				request._remoteIdent.c_str(), subpath.c_str());
			request.send(410);
		} else if (!_dir.isDir(subpath)) {
			ESPWS_DEBUG("[%s] WARNING: Directory '%s' not empty\n",
				request._remoteIdent.c_str(), subpath.c_str());
			request.send(412);
		} else {
			ESPWS_DEBUG("[%s] WARNING: File '%s' not accessible\n",
				request._remoteIdent.c_str(), subpath.c_str());
			request.send(500);
		}
		return;
	}
}

#endif
