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
#ifndef LINKEDLIST_H_
#define LINKEDLIST_H_

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

  private:
    ItemType* _root;
    size_t _count;
    OnRemove _onRemove;

    size_t _add(ItemType *it) {
      if(!_root){
        _root = it;
      } else {
        auto i = _root;
        while(i->next) i = i->next;
        i->next = it;
      }
      _count++;
    }

    typedef std::function<bool(ItemType *)> EnumStep;
    void enumerate(EnumStep const &enumstep) const {
      auto it = _root;
      while(it && enumstep(it)) it = it->next;
    }
  public:
    LinkedList(OnRemove const &onRemove) : _root(NULL), _count(0), _onRemove(onRemove) {}
    ~LinkedList() { clear(); }

#ifdef __GXX_EXPERIMENTAL_CXX0X__
    LinkedList(LinkedList &&src)
    : _root(src._root), _count(src._count), _onRemove(src._onRemove)
    { src._root = NULL; }
#endif

    bool isEmpty() const { return _root == NULL; }
    T& front() const { return _root->value(); }
    ConstIterator begin() const { return ConstIterator(_root); }
    ConstIterator end() const { return ConstIterator(NULL); }

    size_t add(const T& t) { _add(new ItemType(t)); }
#ifdef __GXX_EXPERIMENTAL_CXX0X__
    size_t add(T && t) { _add(new ItemType(std::move(t))); }
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

    bool remove_nth_if(size_t N, Predicate const &predicate) {
      bool Ret = false;
      ItemType* prev = NULL;
      size_t i = 0;
      enumerate([&](ItemType *it){
        if (!predicate || predicate(it->value()))
          if (i++ == N) {
            if (prev) prev->next = it->next;
            else _root = it->next;
            if (_onRemove) _onRemove(it->value());
            delete it;

            Ret = true;
            return false;
          }
        prev = it;
        return true;
      });
      return Ret;
    }

    void clear(){
      if (_root) {
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
        _root = NULL;
      }
    }
};

#endif /* LINKEDLIST_H_ */
