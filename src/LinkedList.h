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
#ifndef LinkedList_H_
#define LinkedList_H_

#include "stddef.h"

template <typename T>
class LinkedListNode {
    T _value;
  public:
    LinkedListNode<T>* next;

    LinkedListNode(const T &val): _value(val), next(NULL) {}
#ifdef __GXX_EXPERIMENTAL_CXX0X__
    LinkedListNode(T &&val): _value(std::move(val)), next(NULL) {}
#endif

    const T& value() const { return _value; };
    T& value(){ return _value; }
};

template <typename T, template<typename> class ItemT = LinkedListNode>
class LinkedList {
  public:
    typedef ItemT<T> ItemType;

    class Iterator {
        ItemType* _node;
      public:
        Iterator(ItemType* current = NULL) : _node(current) {}
        Iterator(const Iterator& i) : _node(i._node) {}
        Iterator& operator ++() { _node = _node->next; return *this; }
        bool operator != (const Iterator& i) const { return _node != i._node; }
        const T& operator * () const { return _node->value(); }
        const T* operator -> () const { return &_node->value(); }
    };

    typedef const Iterator ConstIterator;

    typedef std::function<void(const T&)> OnRemove;
    typedef std::function<bool(const T&)> Predicate;

  protected:
    ItemType *_head, *_tail;
    size_t _count;
    OnRemove _onRemove;

    size_t _addhead(ItemType *it) {
      it->next = _head;
      _head = it;
      if (!_tail) _tail = it;
      return _count++;
    }

    size_t _addtail(ItemType *it) {
      if (_tail) _tail->next = it;
      _tail = it;
      if (!_head) _head = it;
      return _count++;
    }

    typedef std::function<bool(ItemType *)> EnumStep;
    void enumerate(EnumStep const &enumstep) const {
      auto it = _head;
      while(it && enumstep(it)) it = it->next;
    }
  public:
    LinkedList(OnRemove const &onRemove)
    : _head(NULL), _tail(NULL), _count(0), _onRemove(onRemove) {}
    LinkedList(OnRemove const &onRemove, std::initializer_list<T> items)
    : LinkedList(onRemove) { for (auto& item : items) append(item); }
    virtual ~LinkedList() { clear(); }

#ifdef __GXX_EXPERIMENTAL_CXX0X__
    LinkedList(LinkedList &&src)
    : _head(src._head), _tail(src._tail), _count(src._count), _onRemove(std::move(src._onRemove))
    { src._head = src._tail = NULL; src._count = 0; }
#endif

    bool isEmpty() const { return _head == NULL; }
    T& front() const { return _head->value(); }
    ConstIterator begin() const { return ConstIterator(_head); }
    ConstIterator end() const { return ConstIterator(NULL); }

    size_t prepend(const T& t) { return _addhead(new ItemType(t)); }
#ifdef __GXX_EXPERIMENTAL_CXX0X__
    size_t prepend(T && t) { return _addhead(new ItemType(std::move(t))); }
#endif
    size_t append(const T& t) { return _addtail(new ItemType(t)); }
#ifdef __GXX_EXPERIMENTAL_CXX0X__
    size_t append(T && t) { return _addtail(new ItemType(std::move(t))); }
#endif

    size_t length() const { return _count; }

    size_t count_if(Predicate const &predicate) const {
      size_t i = 0;
      enumerate([&](ItemType *it){
        i+= (!predicate || predicate(it->value()))? 1: 0;
        return true;
      });
      return i;
    }

    T* first(void) const
    { return get_if(NULL); }

    T* get_if(Predicate const &predicate) const
    { return get_nth_if(0, predicate); }

    T* get_nth_if(size_t N, Predicate const &predicate) const {
      T* Ret = NULL;
      size_t i = 0;
      enumerate([&](ItemType *it){
        if (!predicate || predicate(it->value()))
          if (i++ == N) {
            Ret = &it->value();
            return false;
          }
        return true;
      });
      return Ret;
    }

    bool remove(T const& item)
    { return remove_if([&](T const& t){ return item == t; }); }

    bool remove_if(Predicate const &predicate)
    { return remove_nth_if(0, predicate); }

    bool remove_nth_if(size_t N, Predicate const &predicate, Predicate const &takeown=NULL) {
      bool Ret = false;
      ItemType* prev = NULL;
      size_t i = 0;
      enumerate([&](ItemType *it){
        if (!predicate || predicate(it->value()))
          if (i++ == N) {
            if (prev) prev->next = it->next;
            else _head = it->next;
            if (_tail == it) _tail = prev;
            _count--;

            bool handoff = takeown && takeown(it->value());
            if (!handoff && _onRemove) _onRemove(it->value());
            delete it;

            Ret = true;
            return false;
          }
        prev = it;
        return true;
      });
      return Ret;
    }

    bool pop_front(Predicate const &takeown)
    { return remove_nth_if(0, NULL, takeown); }

    void clear(){
      if (_head) {
        ItemType* prev = NULL;
        enumerate([&](ItemType *it){
          if (prev) {
            if (_onRemove) _onRemove(prev->value());
            delete prev;
          }
          prev = it;
          return true;
        });
        if (prev) {
          if (_onRemove) _onRemove(prev->value());
          delete prev;
        }
        _head = _tail = NULL;
        _count = 0;
      }
    }
};

#endif /* LinkedList_H_ */
