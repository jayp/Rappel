//
// stream_socket_service.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2006 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_STREAM_SOCKET_SERVICE_HPP
#define ASIO_STREAM_SOCKET_SERVICE_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/push_options.hpp"

#include "asio/detail/push_options.hpp"
#include <cstddef>
#include <boost/config.hpp>
#include "asio/detail/pop_options.hpp"

#include "asio/io_service.hpp"
#include "asio/detail/epoll_reactor.hpp"
#include "asio/detail/kqueue_reactor.hpp"
#include "asio/detail/select_reactor.hpp"
#include "asio/detail/win_iocp_socket_service.hpp"
#include "asio/detail/reactive_socket_service.hpp"

namespace asio {

/// Default service implementation for a stream socket.
template <typename Protocol>
class stream_socket_service
  : public asio::io_service::service
{
public:
  /// The protocol type.
  typedef Protocol protocol_type;

  /// The endpoint type.
  typedef typename Protocol::endpoint endpoint_type;

private:
  // The type of the platform-specific implementation.
#if defined(ASIO_HAS_IOCP)
  typedef detail::win_iocp_socket_service<Protocol> service_impl_type;
#elif defined(ASIO_HAS_EPOLL)
  typedef detail::reactive_socket_service<
      Protocol, detail::epoll_reactor<false> > service_impl_type;
#elif defined(ASIO_HAS_KQUEUE)
  typedef detail::reactive_socket_service<
      Protocol, detail::kqueue_reactor<false> > service_impl_type;
#else
  typedef detail::reactive_socket_service<
      Protocol, detail::select_reactor<false> > service_impl_type;
#endif

public:
  /// The type of a stream socket implementation.
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined implementation_type;
#else
  typedef typename service_impl_type::implementation_type implementation_type;
#endif

  /// The native socket type.
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined native_type;
#else
  typedef typename service_impl_type::native_type native_type;
#endif

  /// Construct a new stream socket service for the specified io_service.
  explicit stream_socket_service(asio::io_service& io_service)
    : asio::io_service::service(io_service),
      service_impl_(asio::use_service<service_impl_type>(io_service))
  {
  }

  /// Destroy all user-defined handler objects owned by the service.
  void shutdown_service()
  {
  }

  /// Construct a new stream socket implementation.
  void construct(implementation_type& impl)
  {
    service_impl_.construct(impl);
  }

  /// Destroy a stream socket implementation.
  void destroy(implementation_type& impl)
  {
    service_impl_.destroy(impl);
  }

  /// Open a stream socket.
  template <typename Error_Handler>
  void open(implementation_type& impl, const protocol_type& protocol,
      Error_Handler error_handler)
  {
    if (protocol.type() == SOCK_STREAM)
      service_impl_.open(impl, protocol, error_handler);
    else
      error_handler(asio::error(asio::error::invalid_argument));
  }

  /// Assign an existing native socket to a stream socket.
  template <typename Error_Handler>
  void assign(implementation_type& impl, const protocol_type& protocol,
      const native_type& native_socket, Error_Handler error_handler)
  {
    service_impl_.assign(impl, protocol, native_socket, error_handler);
  }

  /// Close a stream socket implementation.
  template <typename Error_Handler>
  void close(implementation_type& impl, Error_Handler error_handler)
  {
    service_impl_.close(impl, error_handler);
  }

  /// Get the native socket implementation.
  native_type native(implementation_type& impl)
  {
    return service_impl_.native(impl);
  }

  /// Bind the stream socket to the specified local endpoint.
  template <typename Error_Handler>
  void bind(implementation_type& impl, const endpoint_type& endpoint,
      Error_Handler error_handler)
  {
    service_impl_.bind(impl, endpoint, error_handler);
  }

  /// Connect the stream socket to the specified endpoint.
  template <typename Error_Handler>
  void connect(implementation_type& impl, const endpoint_type& peer_endpoint,
      Error_Handler error_handler)
  {
    service_impl_.connect(impl, peer_endpoint, error_handler);
  }

  /// Start an asynchronous connect.
  template <typename Handler>
  void async_connect(implementation_type& impl,
      const endpoint_type& peer_endpoint, Handler handler)
  {
    service_impl_.async_connect(impl, peer_endpoint, handler);
  }

  /// Set a socket option.
  template <typename Option, typename Error_Handler>
  void set_option(implementation_type& impl, const Option& option,
      Error_Handler error_handler)
  {
    service_impl_.set_option(impl, option, error_handler);
  }

  /// Get a socket option.
  template <typename Option, typename Error_Handler>
  void get_option(const implementation_type& impl, Option& option,
      Error_Handler error_handler) const
  {
    service_impl_.get_option(impl, option, error_handler);
  }

  /// Perform an IO control command on the socket.
  template <typename IO_Control_Command, typename Error_Handler>
  void io_control(implementation_type& impl, IO_Control_Command& command,
      Error_Handler error_handler)
  {
    service_impl_.io_control(impl, command, error_handler);
  }

  /// Get the local endpoint.
  template <typename Error_Handler>
  endpoint_type local_endpoint(const implementation_type& impl,
      Error_Handler error_handler) const
  {
    endpoint_type endpoint;
    service_impl_.get_local_endpoint(impl, endpoint, error_handler);
    return endpoint;
  }

  /// Get the remote endpoint.
  template <typename Error_Handler>
  endpoint_type remote_endpoint(const implementation_type& impl,
      Error_Handler error_handler) const
  {
    endpoint_type endpoint;
    service_impl_.get_remote_endpoint(impl, endpoint, error_handler);
    return endpoint;
  }

  /// Disable sends or receives on the socket.
  template <typename Error_Handler>
  void shutdown(implementation_type& impl, socket_base::shutdown_type what,
      Error_Handler error_handler)
  {
    service_impl_.shutdown(impl, what, error_handler);
  }

  /// Send the given data to the peer.
  template <typename Const_Buffers, typename Error_Handler>
  std::size_t send(implementation_type& impl, const Const_Buffers& buffers,
      socket_base::message_flags flags, Error_Handler error_handler)
  {
    return service_impl_.send(impl, buffers, flags, error_handler);
  }

  /// Start an asynchronous send.
  template <typename Const_Buffers, typename Handler>
  void async_send(implementation_type& impl, const Const_Buffers& buffers,
      socket_base::message_flags flags, Handler handler)
  {
    service_impl_.async_send(impl, buffers, flags, handler);
  }

  /// Receive some data from the peer.
  template <typename Mutable_Buffers, typename Error_Handler>
  std::size_t receive(implementation_type& impl, const Mutable_Buffers& buffers,
      socket_base::message_flags flags, Error_Handler error_handler)
  {
    return service_impl_.receive(impl, buffers, flags, error_handler);
  }

  /// Start an asynchronous receive.
  template <typename Mutable_Buffers, typename Handler>
  void async_receive(implementation_type& impl, const Mutable_Buffers& buffers,
      socket_base::message_flags flags, Handler handler)
  {
    service_impl_.async_receive(impl, buffers, flags, handler);
  }

private:
  // The service that provides the platform-specific implementation.
  service_impl_type& service_impl_;
};

} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // ASIO_STREAM_SOCKET_SERVICE_HPP
