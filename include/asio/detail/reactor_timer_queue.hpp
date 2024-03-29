//
// reactor_timer_queue.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2006 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_REACTOR_TIMER_QUEUE_HPP
#define ASIO_DETAIL_REACTOR_TIMER_QUEUE_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/push_options.hpp"

#include "asio/detail/push_options.hpp"
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <vector>
#include <boost/config.hpp>
#include "asio/detail/pop_options.hpp"

#include "asio/error.hpp"
#include "asio/detail/hash_map.hpp"
#include "asio/detail/noncopyable.hpp"

namespace asio {
namespace detail {

template <typename Time, typename Comparator = std::less<Time> >
class reactor_timer_queue
  : private noncopyable
{
public:
  // Constructor.
  reactor_timer_queue()
    : timers_(),
      heap_()
  {
  }

  // Add a new timer to the queue. Returns true if this is the timer that is
  // earliest in the queue, in which case the reactor's event demultiplexing
  // function call may need to be interrupted and restarted.
  template <typename Handler>
  bool enqueue_timer(const Time& time, Handler handler, void* token)
  {
    // Ensure that there is space for the timer in the heap. We reserve here so
    // that the push_back below will not throw due to a reallocation failure.
    heap_.reserve(heap_.size() + 1);

    // Create a new timer object.
    std::auto_ptr<timer<Handler> > new_timer(
        new timer<Handler>(time, handler, token));

    // Insert the new timer into the hash.
    typedef typename hash_map<void*, timer_base*>::iterator iterator;
    typedef typename hash_map<void*, timer_base*>::value_type value_type;
    std::pair<iterator, bool> result =
      timers_.insert(value_type(token, new_timer.get()));
    if (!result.second)
    {
      result.first->second->prev_ = new_timer.get();
      new_timer->next_ = result.first->second;
      result.first->second = new_timer.get();
    }

    // Put the timer at the correct position in the heap.
    new_timer->heap_index_ = heap_.size();
    heap_.push_back(new_timer.get());
    up_heap(heap_.size() - 1);
    bool is_first = (heap_[0] == new_timer.get());

    // Ownership of the timer is transferred to the timer queue.
    new_timer.release();

    return is_first;
  }

  // Whether there are no timers in the queue.
  bool empty() const
  {
    return heap_.empty();
  }

  // Get the time for the timer that is earliest in the queue.
  void get_earliest_time(Time& time)
  {
    time = heap_[0]->time_;
  }

  // Dispatch the timers that are earlier than the specified time.
  void dispatch_timers(const Time& time)
  {
    Comparator comp;
    while (!heap_.empty() && !comp(time, heap_[0]->time_))
    {
      timer_base* t = heap_[0];
      remove_timer(t);
      t->invoke(0);
    }
  }

  // Cancel the timer with the given token. The handler will be invoked
  // immediately with the result operation_aborted.
  std::size_t cancel_timer(void* timer_token)
  {
    std::size_t num_cancelled = 0;
    typedef typename hash_map<void*, timer_base*>::iterator iterator;
    iterator it = timers_.find(timer_token);
    if (it != timers_.end())
    {
      timer_base* t = it->second;
      while (t)
      {
        timer_base* next = t->next_;
        remove_timer(t);
        t->invoke(asio::error::operation_aborted);
        t = next;
        ++num_cancelled;
      }
    }
    return num_cancelled;
  }

  // Destroy all timers.
  void destroy_timers()
  {
    typename hash_map<void*, timer_base*>::iterator i = timers_.begin();
    typename hash_map<void*, timer_base*>::iterator end = timers_.end();
    while (i != end)
    {
      timer_base* t = i->second;
      typename hash_map<void*, timer_base*>::iterator old_i = i++;
      timers_.erase(old_i);
      t->destroy();
    }
    heap_.clear();
    timers_.clear();
  }

private:
  // Base class for timer operations. Function pointers are used instead of
  // virtual functions to avoid the associated overhead.
  class timer_base
  {
  public:
    // Perform the timer operation and then destroy.
    void invoke(int result)
    {
      invoke_func_(this, result);
    }

    // Destroy the timer operation.
    void destroy()
    {
      destroy_func_(this);
    }

  protected:
    typedef void (*invoke_func_type)(timer_base*, int);
    typedef void (*destroy_func_type)(timer_base*);

    // Constructor.
    timer_base(invoke_func_type invoke_func, destroy_func_type destroy_func,
        const Time& time, void* token)
      : invoke_func_(invoke_func),
        destroy_func_(destroy_func),
        time_(time),
        token_(token),
        next_(0),
        prev_(0),
        heap_index_(
            std::numeric_limits<size_t>::max BOOST_PREVENT_MACRO_SUBSTITUTION())
    {
    }

    // Prevent deletion through this type.
    ~timer_base()
    {
    }

  private:
    friend class reactor_timer_queue<Time, Comparator>;

    // The function to be called to dispatch the handler.
    invoke_func_type invoke_func_;

    // The function to be called to destroy the handler.
    destroy_func_type destroy_func_;

    // The time when the operation should fire.
    Time time_;

    // The token associated with the timer.
    void* token_;

    // The next timer known to the queue.
    timer_base* next_;

    // The previous timer known to the queue.
    timer_base* prev_;

    // The index of the timer in the heap.
    size_t heap_index_;
  };

  // Adaptor class template for using handlers in timers.
  template <typename Handler>
  class timer
    : public timer_base
  {
  public:
    // Constructor.
    timer(const Time& time, Handler handler, void* token)
      : timer_base(&timer<Handler>::invoke_handler,
          &timer<Handler>::destroy_handler, time, token),
        handler_(handler)
    {
    }

    // Invoke the handler and then destroy it.
    static void invoke_handler(timer_base* base, int result)
    {
      std::auto_ptr<timer<Handler> > t(static_cast<timer<Handler>*>(base));
      t->handler_(result);
    }

    // Destroy the handler.
    static void destroy_handler(timer_base* base)
    {
      delete static_cast<timer<Handler>*>(base);
    }

  private:
    Handler handler_;
  };

  // Move the item at the given index up the heap to its correct position.
  void up_heap(size_t index)
  {
    Comparator comp;
    size_t parent = (index - 1) / 2;
    while (index > 0 && comp(heap_[index]->time_, heap_[parent]->time_))
    {
      swap_heap(index, parent);
      index = parent;
      parent = (index - 1) / 2;
    }
  }

  // Move the item at the given index down the heap to its correct position.
  void down_heap(size_t index)
  {
    Comparator comp;
    size_t child = index * 2 + 1;
    while (child < heap_.size())
    {
      size_t min_child = (child + 1 == heap_.size()
          || comp(heap_[child]->time_, heap_[child + 1]->time_))
        ? child : child + 1;
      if (comp(heap_[index]->time_, heap_[min_child]->time_))
        break;
      swap_heap(index, min_child);
      index = min_child;
      child = index * 2 + 1;
    }
  }

  // Swap two entries in the heap.
  void swap_heap(size_t index1, size_t index2)
  {
    timer_base* tmp = heap_[index1];
    heap_[index1] = heap_[index2];
    heap_[index2] = tmp;
    heap_[index1]->heap_index_ = index1;
    heap_[index2]->heap_index_ = index2;
  }

  // Remove a timer from the heap and list of timers.
  void remove_timer(timer_base* t)
  {
    // Remove the timer from the heap.
    size_t index = t->heap_index_;
    if (!heap_.empty() && index < heap_.size())
    {
      if (index == heap_.size() - 1)
      {
        heap_.pop_back();
      }
      else
      {
        swap_heap(index, heap_.size() - 1);
        heap_.pop_back();
        Comparator comp;
        size_t parent = (index - 1) / 2;
        //bug fix for timer mishaps according to asio mailing list
        //if (index > 0 && comp(t->time_, heap_[parent]->time_))
        if (index > 0 && comp(heap_[index]->time_, heap_[parent]->time_))
          up_heap(index);
        else
          down_heap(index);
      }
    }

    // Remove the timer from the hash.
    typedef typename hash_map<void*, timer_base*>::iterator iterator;
    iterator it = timers_.find(t->token_);
    if (it != timers_.end())
    {
      if (it->second == t)
        it->second = t->next_;
      if (t->prev_)
        t->prev_->next_ = t->next_;
      if (t->next_)
        t->next_->prev_ = t->prev_;
      if (it->second == 0)
        timers_.erase(it);
    }
  }

  // A hash of timer token to linked lists of timers.
  hash_map<void*, timer_base*> timers_;

  // The heap of timers, with the earliest timer at the front.
  std::vector<timer_base*> heap_;
};

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // ASIO_DETAIL_REACTOR_TIMER_QUEUE_HPP
