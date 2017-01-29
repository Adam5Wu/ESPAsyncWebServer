/*
  Asynchronous WebServer library for Espressif MCUs

  Copyright (c) 2016 Hristo Gochkov. All rights reserved.
  This file is part of the esp8266 core for Arduino environment.

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
#include "WebAuthentication.h"

#include <libb64/cencode.h>
#include "md5.h"

// Basic Auth hash = base64("username:password")

bool checkBasicAuthentication(const char * hash, const char * username, const char * password){
  if(username == NULL || password == NULL || hash == NULL)
    return false;

  String toEncode(username);
  toEncode.concat(':');
  toEncode.concat(password);

  size_t expectLen = base64_encode_expected_len(toEncode.length());
  if (expectLen != strlen(hash))
    return false;

  String Encoded;
  Encoded.concat(' ', expectLen);
  base64_encode_chars(toEncode.begin(), toEncode.length(), Encoded.begin());
  return memcmp(hash, Encoded.begin(), expectLen) == 0;
}

static void getMD5(uint8_t * data, uint16_t len, char * output) {
  // Output must be 33 bytes or larger
  md5_context_t _ctx;
  MD5Init(&_ctx);

  MD5Update(&_ctx, data, len);

  uint8_t _buf[16];
  MD5Final(_buf, &_ctx);

  for(uint8_t i = 0; i < 16; i++) {
    if (_buf[i] < 0x10) {
      output[i*2] = '0';
      itoa(_buf[i], &output[i*2+1], 16);
    } else itoa(_buf[i], &output[i*2], 16);
  }
}

static String genRandomMD5(){
#ifdef ESP8266
  uint32_t r = RANDOM_REG32;
#else
  uint32_t r = rand();
#endif
  char res[33];
  getMD5((uint8_t*)(&r), 4, res);
  return res;
}

static String stringMD5(const String& in){
  char res[33];
  getMD5((uint8_t*)in.begin(), in.length(), res);
  return res;
}

String generateDigestHash(const char * username, const char * password, const char * realm){
  if(username == NULL || password == NULL || realm == NULL)
    return "";

  String in(username);
  in.concat(':');
  in.concat(realm);
  in.concat(':');
  String res = in;
  in.concat(password);
  res.concat(stringMD5(in));
  return res;
}

String requestDigestAuthentication(const char * realm){
  String header = "realm=\"";
  header.concat(realm);
  header.concat("\", qop=\"auth\", nonce=\"");
  header.concat(genRandomMD5());
  header.concat("\", opaque=\"");
  header.concat(genRandomMD5());
  header.concat('"');
  return header;
}

bool checkDigestAuthentication(const char * header, const char * method, const char * username, const char * realm, const char * password,
                               bool passwordIsHash, const char * nonce, const char * opaque, const char * uri)
{
  if(header == NULL || username == NULL || password == NULL || method == NULL){
    ESPWS_DEBUGV("AUTH FAIL: missing required fields\n");
    return false;
  }

  String myHeader = String(header);
  char *pRealm, *pNonce, *pUri, *pResp, *pQop, *pNc, *pCn;
  char *token, *ptr, *value;

  token = ptr = myHeader.begin();

  do {
    token = ptr;
    while (*ptr != ':') if (!*++ptr) break;
    if (token == ptr) break;
    *ptr++ = '\0';

    value = token;
    while (*value != '=') if (!*++value) break;
    if (!*value) {
      ESPWS_DEBUGV("AUTH FAIL: invalid token\n");
      return false;
    }
    *value++ = '\0';

    if (*value == '"') {
      *value++ = '\0';
      ptr[-2] = '\0';
    }

    if(strcmp(token, "username") == 0){
      if(strcmp(value, username) != 0){
        ESPWS_DEBUGV("AUTH FAIL: username\n");
        return false;
      }
    } else if(strcmp(token, "realm") == 0){
      if(realm != NULL && strcmp(value, realm) != 0){
        ESPWS_DEBUGV("AUTH FAIL: realm\n");
        return false;
      }
      pRealm = value;
    } else if(strcmp(token, "nonce") == 0){
      if(nonce != NULL && strcmp(value, nonce) != 0){
        ESPWS_DEBUGV("AUTH FAIL: nonce\n");
        return false;
      }
      pNonce = value;
    } else if(strcmp(token, "opaque") == 0){
      if(opaque != NULL && strcmp(value, opaque) != 0){
        ESPWS_DEBUGV("AUTH FAIL: opaque\n");
        return false;
      }
    } else if(strcmp(token, "uri") == 0){
      if(uri != NULL && strcmp(value, uri) != 0){
        ESPWS_DEBUGV("AUTH FAIL: uri\n");
        return false;
      }
      pUri = value;
    } else if(strcmp(token, "response") == 0){
      pResp = value;
    } else if(strcmp(token, "qop") == 0){
      pQop = value;
    } else if(strcmp(token, "nc") == 0){
      pNc = value;
    } else if(strcmp(token, "cnonce") == 0){
      pCn = value;
    }
  } while(*ptr);

  String ha1 = (passwordIsHash) ? String(password) : (String(username) + ':' + pRealm + ':' + password);
  String ha2 = String(method) + ':' + pUri;
  String response = stringMD5(ha1) + ':' + pNonce + ':' + pNc + ':' + pCn + ':' + pQop + ':' + stringMD5(ha2);

  if(stringMD5(response).equals(pResp)){
    ESPWS_DEBUGV("AUTH SUCCESS\n");
    return true;
  }

  ESPWS_DEBUGV("AUTH FAIL: password\n");
  return false;
}
