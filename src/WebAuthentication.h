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

#ifndef WebAuthentication_H_
#define WebAuthentication_H_

#include <ESPAsyncWebServer.h>

bool checkBasicAuthentication(const char * header, const char * username, const char * password);
String requestDigestAuthentication(const char * realm);
bool checkDigestAuthentication(const char * header, const char * method, const char * username, const char * password,
                               const char * realm, bool passwordIsHash, const char * nonce, const char * opaque, const char * uri);

//for storing hashed versions on the device that can be authenticated against
String generateDigestHash(const char * username, const char * password, const char * realm);

#endif
