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
#ifndef StringArray_H_
#define StringArray_H_

#include "WString.h"
#include "LinkedList.h"

class StringArray : public LinkedList<String> {
  public:
    StringArray() : LinkedList(NULL) {}

    bool contains(const String& str) const {
      return get_if([&](String const &v) {
        return str.equals(v);
      }) != NULL;
    }

    bool containsIgnoreCase(const String& str) const {
      return get_if([&](String const &v) {
        return str.equalsIgnoreCase(v);
      }) != NULL;
    }
};

#endif /* StringArray_H_ */
