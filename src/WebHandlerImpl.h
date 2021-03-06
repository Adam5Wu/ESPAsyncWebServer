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

class AsyncHostRedirWebHandler: public AsyncWebHandler {
	protected:
		void _redirectHost(AsyncWebRequest &request) {
			String newLocation = "http://"+host;
			if (altPath && !psvPaths.contains(request.oUrl())) {
				newLocation.concat(altPath);
			} else {
				newLocation.concat(request.oUrl());
				if (request.oQuery())
					newLocation.concat(request.oQuery());
			}
			request.redirect(newLocation);
		}

	public:
		String const host;
		WebRequestMethodComposite const method;
		String altPath;
		StringArray psvPaths;

		AsyncHostRedirWebHandler(String const &h, WebRequestMethodComposite m)
			: host(h), method(m) {}
		virtual bool _canHandle(AsyncWebRequest const &request) override;
		virtual bool _checkContinue(AsyncWebRequest &request, bool continueHeader) override;

		virtual void _handleRequest(AsyncWebRequest &request) override {
			// Do not expect to reach here
		}

#ifdef HANDLE_REQUEST_CONTENT
		virtual bool _handleBody(AsyncWebRequest &request,
			size_t offset, void *buf, size_t size) override {
			// Do not expect request body
			return false;
		}

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
		virtual bool _handleParamData(AsyncWebRequest &request, String const& name,
			size_t offset, void *buf, size_t size) override {
			// Do not expect request param
			return false;
		}
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
		virtual bool _handleUploadData(AsyncWebRequest &request, String const& name,
			String const& filename, String const& contentType,
			size_t offset, void *buf, size_t size) override {
			// Do not expect request upload
			return false;
		}
#endif

#endif
};

class AsyncPathURIWebHandler: virtual public AsyncWebHandler {
	protected:
		// May not be compliant with standard (no protocol and server),
		// but seems to work OK with most browsers
		void _redirectDir(AsyncWebRequest &request) {
			String newLocation = request.oUrl()+'/';
			if (request.oQuery())
				newLocation.concat(request.oQuery());
			request.redirect(newLocation);
		}

		bool _checkPathRedirectOrContinue(AsyncWebRequest &request, bool continueHeader);
	public:
		String const path;
		WebRequestMethodComposite const method;

		AsyncPathURIWebHandler(String const &p, WebRequestMethodComposite m)
			: path(normalizePath(p)), method(m) {}

		virtual bool _canHandle(AsyncWebRequest const &request) override;
		virtual bool _checkContinue(AsyncWebRequest &request, bool continueHeader) override;

		static String normalizePath(String const &p) {
			String Ret = p[0]=='/'? p : "/"+p;
			if (p.end()[-1] != '/' && p.end()[-1] != '$') Ret.concat('/');
			return Ret;
		}
};

class AsyncStaticWebHandler: public AsyncPathURIWebHandler {
	protected:
		Dir _dir;
		String _cache_control;
		String _GET_indexFile;
		bool _GET_gzLookup, _GET_gzFirst;

		void _GET_sendDirList(AsyncWebRequest &request);
		void _pathNotFound(AsyncWebRequest &request);

		void _handleRead(AsyncWebRequest &request);

#ifdef STATIC_ADVANCED_WEBHANDLER
		struct UploadRec {
			AsyncWebRequest *req;
			File file;
			size_t pos;
		};
		LinkedList<UploadRec> _uploads;
		bool _checkContinueCanWrite(AsyncWebRequest &request, bool continueHeader);
		bool _checkContinueCanDelete(AsyncWebRequest &request, bool continueHeader);
		//bool _checkContinueDAV(AsyncWebRequest &request, bool continueHeader);

		bool _handleUploadBody(AsyncWebRequest &request,
			size_t offset, void *buf, size_t size);

		void _handleWrite(AsyncWebRequest &request);
		void _handleDelete(AsyncWebRequest &request);
		//void _handleDAVOptions(AsyncWebRequest &request);
		//void _handleDAVList(AsyncWebRequest &request);
		//void _handleDAVWriteAttr(AsyncWebRequest &request);
		//void _handleDAVMakeDir(AsyncWebRequest &request);
		//void _handleDAVCopy(AsyncWebRequest &request);
		//void _handleDAVMove(AsyncWebRequest &request);
#endif

	public:
		ArRequestHandlerFunction _onGETIndex;
		ArRequestHandlerFunction _onGETPathNotFound;
		ArRequestHandlerFunction _onGETIndexNotFound;
		ArRequestHandlerFunction _onDirRedirect;

		AsyncStaticWebHandler(String const &path, Dir const& dir
#ifdef STATIC_ADVANCED_WEBHANDLER
			, bool write_support
#ifdef HANDLE_WEBDAV
			, bool dav_support
#endif
#endif
		);

		virtual bool _isInterestingHeader(AsyncWebRequest const &request, String const& key) override;
#ifdef STATIC_ADVANCED_WEBHANDLER
		virtual bool _checkContinue(AsyncWebRequest &request, bool continueHeader) override;
		virtual void _terminateRequest(AsyncWebRequest &request) override;
#endif

		AsyncStaticWebHandler& setCacheControl(String const &cache_control);
		AsyncStaticWebHandler& setGETLookupGZ(bool gzLookup, bool gzFirst);
		AsyncStaticWebHandler& setGETIndexFile(String const &filename);

		virtual void _handleRequest(AsyncWebRequest &request) override;

#ifdef HANDLE_REQUEST_CONTENT

#ifdef STATIC_ADVANCED_WEBHANDLER
		virtual bool _handleBody(AsyncWebRequest &request,
			size_t offset, void *buf, size_t size) override;
#else
		virtual bool _handleBody(AsyncWebRequest &request,
			size_t offset, void *buf, size_t size) override {
			// Do not expect request body
			return false;
		}
#endif

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
		virtual bool _handleParamData(AsyncWebRequest &request, String const& name,
			size_t offset, void *buf, size_t size) override {
			// Do not expect request param
			return false;
		}
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
		virtual bool _handleUploadData(AsyncWebRequest &request, String const& name,
			String const& filename, String const& contentType,
			size_t offset, void *buf, size_t size) override {
			// Do not expect request upload
			return false;
		}
#endif

#endif
};

class AsyncCallbackWebHandler : virtual public AsyncWebHandler {
	public:
		ArRequestHandlerFunction onRequest;
#ifdef HANDLE_REQUEST_CONTENT
		ArBodyHandlerFunction onBody;

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
		ArParamDataHandlerFunction onParamData;
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
		ArUploadDataHandlerFunction onUploadData;
#endif

#endif

		bool _loaded(void) {
			if (onRequest) return true;
#ifdef HANDLE_REQUEST_CONTENT
			if (onBody) return true;

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
			if (onParamData) return true;
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
			if (onUploadData) return true;
#endif

#endif
			return false;
		}

		virtual void _handleRequest(AsyncWebRequest &request) override {
			if (onRequest) onRequest(request);
			else request.send(500);
		}

#ifdef HANDLE_REQUEST_CONTENT
		virtual bool _handleBody(AsyncWebRequest &request,
			size_t offset, void *buf, size_t size) override
		{ return onBody? onBody(request, offset, buf, size) : true; }

#if defined(HANDLE_REQUEST_CONTENT_SIMPLEFORM) || defined(HANDLE_REQUEST_CONTENT_MULTIPARTFORM)
		virtual bool _handleParamData(AsyncWebRequest &request, String const& name,
			size_t offset, void *buf, size_t size) override
		{ return onParamData? onParamData(request, name, offset, buf, size) : true; }
#endif

#ifdef HANDLE_REQUEST_CONTENT_MULTIPARTFORM
		virtual bool _handleUploadData(AsyncWebRequest &request, String const& name,
			String const& filename, String const& contentType,
			size_t offset, void *buf, size_t size) override {
			return onUploadData? onUploadData(request, name, filename, contentType,
			offset, buf, size) : true;
		}
	#endif

#endif
};

class AsyncPathURICallbackWebHandler: public AsyncPathURIWebHandler, public AsyncCallbackWebHandler {
	public:
		StringArray interestedHeaders;

		AsyncPathURICallbackWebHandler(String const &path, WebRequestMethodComposite method = HTTP_ANY)
			: AsyncPathURIWebHandler(path, method) {}

		virtual bool _isInterestingHeader(AsyncWebRequest const &request, String const& key) override
		{ return interestedHeaders.containsIgnoreCase(key); }
};

#endif /* AsynWebHandlerImpl_H_ */
