//
// basic_io_object.hpp
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2006 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_BASIC_IO_OBJECT_HPP
#define ASIO_BASIC_IO_OBJECT_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/push_options.hpp"

#include "asio/io_service.hpp"
#include "asio/detail/noncopyable.hpp"

namespace asio {

/// Base class for all I/O objects.
template <typename Service>
class basic_io_object
  : private noncopyable
{
public:
  /// The type of the service that will be used to provide I/O operations.
  typedef Service service_type;

  /// The underlying implementation type of I/O object.
  typedef typename service_type::implementation_type implementation_type;

  /// Construct a basic_io_object.
  explicit basic_io_object(asio::io_service& io_service)
    : service(asio::use_service<Service>(io_service))
  {
    service.construct(implementation);
  }

  /// Get the io_service associated with the object.
  /**
   * This function may be used to obtain the io_service object that the I/O
   * object uses to dispatch handlers for asynchronous operations.
   *
   * @return A reference to the io_service object that the I/O object will use
   * to dispatch handlers. Ownership is not transferred to the caller.
   */
  asio::io_service& io_service()
  {
    return service.owner();
  }

protected:
  /// Protected destructor to prevent deletion through this type.
  ~basic_io_object()
  {
    service.destroy(implementation);
  }

  // The backend service implementation.
  service_type& service;

  // The underlying native implementation.
  implementation_type implementation;
};

} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // ASIO_BASIC_IO_OBJECT_HPP
