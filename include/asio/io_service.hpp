//
// io_service.hpp
// ~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2006 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_IO_SERVICE_HPP
#define ASIO_IO_SERVICE_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/push_options.hpp"

#include "asio/detail/push_options.hpp"
#include <cstddef>
#include <stdexcept>
#include <typeinfo>
#include <boost/config.hpp>
#include <boost/throw_exception.hpp>
#include "asio/detail/pop_options.hpp"

#include "asio/detail/epoll_reactor_fwd.hpp"
#include "asio/detail/kqueue_reactor_fwd.hpp"
#include "asio/detail/noncopyable.hpp"
#include "asio/detail/select_reactor_fwd.hpp"
#include "asio/detail/service_registry.hpp"
#include "asio/detail/signal_init.hpp"
#include "asio/detail/task_io_service_fwd.hpp"
#include "asio/detail/win_iocp_io_service_fwd.hpp"
#include "asio/detail/winsock_init.hpp"
#include "asio/detail/wrapped_handler.hpp"

namespace asio {

/// Provides core I/O functionality.
/**
 * The io_service class provides the core I/O functionality for users of the
 * asynchronous I/O objects, including:
 *
 * @li asio::ip::tcp::socket
 * @li asio::ip::tcp::acceptor
 * @li asio::ip::udp::socket
 * @li asio::deadline_timer.
 *
 * The io_service class also includes facilities intended for developers of
 * custom asynchronous services.
 *
 * @par Thread Safety:
 * @e Distinct @e objects: Safe.@n
 * @e Shared @e objects: Safe, with the exception that calling reset()
 * while there are unfinished run() calls results in undefined behaviour.
 *
 * @par Concepts:
 * Dispatcher.
 *
 * @sa @ref io_service_handler_exception
 */
class io_service
  : private noncopyable
{
private:
  // The type of the platform-specific implementation.
#if defined(ASIO_HAS_IOCP)
  typedef detail::win_iocp_io_service impl_type;
#elif defined(ASIO_HAS_EPOLL)
  typedef detail::task_io_service<detail::epoll_reactor<false> > impl_type;
#elif defined(ASIO_HAS_KQUEUE)
  typedef detail::task_io_service<detail::kqueue_reactor<false> > impl_type;
#else
  typedef detail::task_io_service<detail::select_reactor<false> > impl_type;
#endif

public:
  class work;
  friend class work;

  class service;

  /// Default constructor.
  io_service();

  /// Run the io_service's event processing loop.
  /**
   * The run() function blocks until all work has finished and there are no
   * more handlers to be dispatched, or until the io_service has been
   * interrupted.
   *
   * Multiple threads may call the run() function to set up a pool of threads
   * from which the io_service may execute handlers.
   *
   * The run() function may be safely called again once it has completed only
   * after a call to reset().
   */
  void run();

  /// Interrupt the io_service's event processing loop.
  /**
   * This function does not block, but instead simply signals to the io_service
   * that all invocations of its run() member function should return as soon as
   * possible.
   *
   * Note that if the run() function is interrupted and is not called again
   * later then its work may not have finished and handlers may not be
   * delivered. In this case an io_service implementation is not required to
   * make any guarantee that the resources associated with unfinished work will
   * be cleaned up.
   */
  void interrupt();

  /// Reset the io_service in preparation for a subsequent run() invocation.
  /**
   * This function must be called prior to any second or later set of
   * invocations of the run() function. It allows the io_service to reset any
   * internal state, such as an interrupt flag.
   *
   * This function must not be called while there are any unfinished calls to
   * the run() function.
   */
  void reset();

  /// Request the io_service to invoke the given handler.
  /**
   * This function is used to ask the io_service to execute the given handler.
   *
   * The io_service guarantees that the handler will only be called in a thread
   * in which the run() member function is currently being invoked. The handler
   * may be executed inside this function if the guarantee can be met.
   *
   * @param handler The handler to be called. The io_service will make
   * a copy of the handler object as required. The function signature of the
   * handler must be: @code void handler(); @endcode
   */
  template <typename Handler>
  void dispatch(Handler handler);

  /// Request the io_service to invoke the given handler and return immediately.
  /**
   * This function is used to ask the io_service to execute the given handler,
   * but without allowing the io_service to call the handler from inside this
   * function.
   *
   * The io_service guarantees that the handler will only be called in a thread
   * in which the run() member function is currently being invoked.
   *
   * @param handler The handler to be called. The io_service will make
   * a copy of the handler object as required. The function signature of the
   * handler must be: @code void handler(); @endcode
   */
  template <typename Handler>
  void post(Handler handler);

  /// Create a new handler that automatically dispatches the wrapped handler
  /// on the io_service.
  /**
   * This function is used to create a new handler function object that, when
   * invoked, will automatically pass the wrapped handler to the io_service's
   * dispatch function.
   *
   * @param handler The handler to be wrapped. The io_service will make a copy
   * of the handler object as required. The function signature of the handler
   * must be: @code void handler(A1 a1, ... An an); @endcode
   *
   * @return A function object that, when invoked, passes the wrapped handler to
   * the io_service's dispatch function. Given a function object with the
   * signature:
   * @code R f(A1 a1, ... An an); @endcode
   * If this function object is passed to the wrap function like so:
   * @code io_service.wrap(f); @endcode
   * then the return value is a function object with the signature
   * @code void g(A1 a1, ... An an); @endcode
   * that, when invoked, executes code equivalent to:
   * @code io_service.dispatch(boost::bind(f, a1, ... an)); @endcode
   */
  template <typename Handler>
#if defined(GENERATING_DOCUMENTATION)
  unspecified
#else
  detail::wrapped_handler<io_service, Handler>
#endif
  wrap(Handler handler);

  /// Obtain the service object corresponding to the given type.
  /**
   * This function is used to locate a service object that corresponds to
   * the given service type. If there is no existing implementation of the
   * service, then the io_service will create a new instance of the service.
   *
   * @param ios The io_service object that owns the service.
   *
   * @return The service interface implementing the specified service type.
   * Ownership of the service interface is not transferred to the caller.
   */
  template <typename Service>
  friend Service& use_service(io_service& ios);

  /// Add a service object to the io_service.
  /**
   * This function is used to add a service to the io_service.
   *
   * @param ios The io_service object that owns the service.
   *
   * @param svc The service object. On success, ownership of the service object
   * is transferred to the io_service. When the io_service object is destroyed,
   * it will destroy the service object by performing:
   * @code delete static_cast<io_service::service*>(svc) @endcode
   *
   * @throws asio::service_already_exists Thrown if a service of the
   * given type is already present in the io_service.
   *
   * @throws asio::invalid_service_owner Thrown if the service's owning
   * io_service is not the io_service object specified by the ios parameter.
   */
  template <typename Service>
  friend void add_service(io_service& ios, Service* svc);

  /// Determine if an io_service contains a specified service type.
  /**
   * This function is used to determine whether the io_service contains a
   * service object corresponding to the given service type.
   *
   * @param ios The io_service object that owns the service.
   *
   * @return A boolean indicating whether the io_service contains the service.
   */
  template <typename Service>
  friend bool has_service(io_service& ios);

private:
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  detail::winsock_init<> init_;
#elif defined(__sun) || defined(__QNX__)
  detail::signal_init<> init_;
#endif

  // The service registry.
  detail::service_registry<io_service> service_registry_;

  // The implementation.
  impl_type& impl_;
};

/// Class to inform the io_service when it has work to do.
/**
 * The work class is used to inform the io_service when work starts and
 * finishes. This ensures that the io_service's run() function will not exit
 * while work is underway, and that it does exit when there is no unfinished
 * work remaining.
 *
 * The work class is copy-constructible so that it may be used as a data member
 * in a handler class. It is not assignable.
 */
class io_service::work
{
public:
  /// Constructor notifies the io_service that work is starting.
  /**
   * The constructor is used to inform the io_service that some work has begun.
   * This ensures that the io_service's run() function will not exit while the
   * work is underway.
   */
  explicit work(io_service& io_service);

  /// Copy constructor notifies the io_service that work is starting.
  /**
   * The constructor is used to inform the io_service that some work has begun.
   * This ensures that the io_service's run() function will not exit while the
   * work is underway.
   */
  work(const work& other);

  /// Destructor notifies the io_service that the work is complete.
  /**
   * The destructor is used to inform the io_service that some work has
   * finished. Once the count of unfinished work reaches zero, the io_service's
   * run() function is permitted to exit.
   */
  ~work();

private:
  // Prevent assignment.
  void operator=(const work& other);

  // The io_service's implementation.
  io_service::impl_type& impl_;
};

/// Base class for all io_service services.
class io_service::service
  : private noncopyable
{
public:
  /// Get the io_service object that owns the service.
  io_service& owner();

protected:
  /// Constructor.
  /**
   * @param owner The io_service object that owns the service.
   */
  service(io_service& owner);

  /// Destructor.
  virtual ~service();

private:
  /// Destroy all user-defined handler objects owned by the service.
  virtual void shutdown_service() = 0;

  friend class detail::service_registry<io_service>;
  io_service& owner_;
  const std::type_info* type_info_;
  service* next_;
};

/// Exception thrown when trying to add a duplicate service to an io_service.
class service_already_exists
  : public std::logic_error
{
public:
  service_already_exists()
    : std::logic_error("Service already exists.")
  {
  }
};

/// Exception thrown when trying to add a service object to an io_service where
/// the service has a different owner.
class invalid_service_owner
  : public std::logic_error
{
public:
  invalid_service_owner()
    : std::logic_error("Invalid service owner.")
  {
  }
};

/**
 * @page io_service_handler_exception Effect of exceptions thrown from handlers
 *
 * If an exception is thrown from a handler, the exception is allowed to
 * propagate through the throwing thread's invocation of
 * asio::io_service::run(). No other threads that are calling
 * asio::io_service::run() are affected. It is then the responsibility of
 * the application to catch the exception.
 *
 * After the exception has been caught, the asio::io_service::run() call
 * may be restarted @em without the need for an intervening call to
 * asio::io_service::reset(). This allows the thread to rejoin the
 * io_service's thread pool without impacting any other threads in the
 * pool.
 *
 * @par Example:
 * @code
 * asio::io_service io_service;
 * ...
 * for (;;)
 * {
 *   try
 *   {
 *     io_service.run();
 *     break; // run() exited normally
 *   }
 *   catch (my_exception& e)
 *   {
 *     // Deal with exception as appropriate.
 *   }
 * }
 * @endcode
 */

} // namespace asio

#include "asio/impl/io_service.ipp"

#include "asio/detail/pop_options.hpp"

#endif // ASIO_IO_SERVICE_HPP
