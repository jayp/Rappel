//
// socket_base.hpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2006 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_SOCKET_BASE_HPP
#define ASIO_SOCKET_BASE_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/push_options.hpp"

#include "asio/detail/push_options.hpp"
#include <boost/config.hpp>
#include <boost/detail/workaround.hpp>
#include "asio/detail/pop_options.hpp"

#include "asio/detail/io_control.hpp"
#include "asio/detail/socket_option.hpp"
#include "asio/detail/socket_types.hpp"

namespace asio {

/// The socket_base class is used as a base for the basic_stream_socket and
/// basic_datagram_socket class templates so that we have a common place to
/// define the shutdown_type and enum.
class socket_base
{
public:
  /// Different ways a socket may be shutdown.
  enum shutdown_type
  {
#if defined(GENERATING_DOCUMENTATION)
    /// Shutdown the receive side of the socket.
    shutdown_receive = implementation_defined,

    /// Shutdown the send side of the socket.
    shutdown_send = implementation_defined,

    /// Shutdown both send and receive on the socket.
    shutdown_both = implementation_defined
#else
    shutdown_receive = asio::detail::shutdown_receive,
    shutdown_send = asio::detail::shutdown_send,
    shutdown_both = asio::detail::shutdown_both
#endif
  };

  /// Bitmask type for flags that can be passed to send and receive operations.
  typedef int message_flags;

#if defined(GENERATING_DOCUMENTATION)
  /// Peek at incoming data without removing it from the input queue.
  static const int message_peek = implementation_defined;

  /// Process out-of-band data.
  static const int message_out_of_band = implementation_defined;

  /// Specify that the data should not be subject to routing.
  static const int message_do_not_route = implementation_defined;
#else
  BOOST_STATIC_CONSTANT(int,
      message_peek = asio::detail::message_peek);
  BOOST_STATIC_CONSTANT(int,
      message_out_of_band = asio::detail::message_out_of_band);
  BOOST_STATIC_CONSTANT(int,
      message_do_not_route = asio::detail::message_do_not_route);
#endif

  /// Socket option to permit sending of broadcast messages.
  /**
   * Implements the SOL_SOCKET/SO_BROADCAST socket option.
   *
   * @par Examples:
   * Setting the option:
   * @code
   * asio::ip::udp::socket socket(io_service); 
   * ...
   * asio::socket_base::broadcast option(true);
   * socket.set_option(option);
   * @endcode
   *
   * @par
   * Getting the current option value:
   * @code
   * asio::ip::udp::socket socket(io_service); 
   * ...
   * asio::socket_base::broadcast option;
   * socket.get_option(option);
   * bool is_set = option.get();
   * @endcode
   *
   * @par Concepts:
   * Socket_Option, Boolean_Socket_Option.
   */
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined broadcast;
#else
  typedef asio::detail::socket_option::boolean<
    SOL_SOCKET, SO_BROADCAST> broadcast;
#endif

  /// Socket option to prevent routing, use local interfaces only.
  /**
   * Implements the SOL_SOCKET/SO_DONTROUTE socket option.
   *
   * @par Examples:
   * Setting the option:
   * @code
   * asio::ip::udp::socket socket(io_service); 
   * ...
   * asio::socket_base::do_not_route option(true);
   * socket.set_option(option);
   * @endcode
   *
   * @par
   * Getting the current option value:
   * @code
   * asio::ip::udp::socket socket(io_service); 
   * ...
   * asio::socket_base::do_not_route option;
   * socket.get_option(option);
   * bool is_set = option.get();
   * @endcode
   *
   * @par Concepts:
   * Socket_Option, Boolean_Socket_Option.
   */
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined do_not_route;
#else
  typedef asio::detail::socket_option::boolean<
    SOL_SOCKET, SO_DONTROUTE> do_not_route;
#endif

  /// Socket option to send keep-alives.
  /**
   * Implements the SOL_SOCKET/SO_KEEPALIVE socket option.
   *
   * @par Examples:
   * Setting the option:
   * @code
   * asio::ip::tcp::socket socket(io_service); 
   * ...
   * asio::socket_base::keep_alive option(true);
   * socket.set_option(option);
   * @endcode
   *
   * @par
   * Getting the current option value:
   * @code
   * asio::ip::tcp::socket socket(io_service); 
   * ...
   * asio::socket_base::keep_alive option;
   * socket.get_option(option);
   * bool is_set = option.get();
   * @endcode
   *
   * @par Concepts:
   * Socket_Option, Boolean_Socket_Option.
   */
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined keep_alive;
#else
  typedef asio::detail::socket_option::boolean<
    SOL_SOCKET, SO_KEEPALIVE> keep_alive;
#endif

  /// Socket option for the send buffer size of a socket.
  /**
   * Implements the SOL_SOCKET/SO_SNDBUF socket option.
   *
   * @par Examples:
   * Setting the option:
   * @code
   * asio::ip::tcp::socket socket(io_service); 
   * ...
   * asio::socket_base::send_buffer_size option(8192);
   * socket.set_option(option);
   * @endcode
   *
   * @par
   * Getting the current option value:
   * @code
   * asio::ip::tcp::socket socket(io_service); 
   * ...
   * asio::socket_base::send_buffer_size option;
   * socket.get_option(option);
   * int size = option.get();
   * @endcode
   *
   * @par Concepts:
   * Socket_Option, Integer_Socket_Option.
   */
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined send_buffer_size;
#else
  typedef asio::detail::socket_option::integer<
    SOL_SOCKET, SO_SNDBUF> send_buffer_size;
#endif

  /// Socket option for the send low watermark.
  /**
   * Implements the SOL_SOCKET/SO_SNDLOWAT socket option.
   *
   * @par Examples:
   * Setting the option:
   * @code
   * asio::ip::tcp::socket socket(io_service); 
   * ...
   * asio::socket_base::send_low_watermark option(1024);
   * socket.set_option(option);
   * @endcode
   *
   * @par
   * Getting the current option value:
   * @code
   * asio::ip::tcp::socket socket(io_service); 
   * ...
   * asio::socket_base::send_low_watermark option;
   * socket.get_option(option);
   * int size = option.get();
   * @endcode
   *
   * @par Concepts:
   * Socket_Option, Integer_Socket_Option.
   */
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined send_low_watermark;
#else
  typedef asio::detail::socket_option::integer<
    SOL_SOCKET, SO_SNDLOWAT> send_low_watermark;
#endif

  /// Socket option for the receive buffer size of a socket.
  /**
   * Implements the SOL_SOCKET/SO_RCVBUF socket option.
   *
   * @par Examples:
   * Setting the option:
   * @code
   * asio::ip::tcp::socket socket(io_service); 
   * ...
   * asio::socket_base::receive_buffer_size option(8192);
   * socket.set_option(option);
   * @endcode
   *
   * @par
   * Getting the current option value:
   * @code
   * asio::ip::tcp::socket socket(io_service); 
   * ...
   * asio::socket_base::receive_buffer_size option;
   * socket.get_option(option);
   * int size = option.get();
   * @endcode
   *
   * @par Concepts:
   * Socket_Option, Integer_Socket_Option.
   */
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined receive_buffer_size;
#else
  typedef asio::detail::socket_option::integer<
    SOL_SOCKET, SO_RCVBUF> receive_buffer_size;
#endif

  /// Socket option for the receive low watermark.
  /**
   * Implements the SOL_SOCKET/SO_RCVLOWAT socket option.
   *
   * @par Examples:
   * Setting the option:
   * @code
   * asio::ip::tcp::socket socket(io_service); 
   * ...
   * asio::socket_base::receive_low_watermark option(1024);
   * socket.set_option(option);
   * @endcode
   *
   * @par
   * Getting the current option value:
   * @code
   * asio::ip::tcp::socket socket(io_service); 
   * ...
   * asio::socket_base::receive_low_watermark option;
   * socket.get_option(option);
   * int size = option.get();
   * @endcode
   *
   * @par Concepts:
   * Socket_Option, Integer_Socket_Option.
   */
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined receive_low_watermark;
#else
  typedef asio::detail::socket_option::integer<
    SOL_SOCKET, SO_RCVLOWAT> receive_low_watermark;
#endif

  /// Socket option to allow the socket to be bound to an address that is
  /// already in use.
  /**
   * Implements the SOL_SOCKET/SO_REUSEADDR socket option.
   *
   * @par Examples:
   * Setting the option:
   * @code
   * asio::ip::tcp::acceptor acceptor(io_service); 
   * ...
   * asio::socket_base::reuse_address option(true);
   * acceptor.set_option(option);
   * @endcode
   *
   * @par
   * Getting the current option value:
   * @code
   * asio::ip::tcp::acceptor acceptor(io_service); 
   * ...
   * asio::socket_base::reuse_address option;
   * acceptor.get_option(option);
   * bool is_set = option.get();
   * @endcode
   *
   * @par Concepts:
   * Socket_Option, Boolean_Socket_Option.
   */
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined reuse_address;
#else
  typedef asio::detail::socket_option::boolean<
    SOL_SOCKET, SO_REUSEADDR> reuse_address;
#endif

  /// Socket option to specify whether the socket lingers on close if unsent
  /// data is present.
  /**
   * Implements the SOL_SOCKET/SO_LINGER socket option.
   *
   * @par Examples:
   * Setting the option:
   * @code
   * asio::ip::tcp::socket socket(io_service); 
   * ...
   * asio::socket_base::linger option(true, 30);
   * socket.set_option(option);
   * @endcode
   *
   * @par
   * Getting the current option value:
   * @code
   * asio::ip::tcp::socket socket(io_service); 
   * ...
   * asio::socket_base::linger option;
   * socket.get_option(option);
   * bool is_set = option.enabled();
   * unsigned short timeout = option.timeout();
   * @endcode
   *
   * @par Concepts:
   * Socket_Option, Linger_Socket_Option.
   */
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined linger;
#else
  typedef asio::detail::socket_option::linger<
    SOL_SOCKET, SO_LINGER> linger;
#endif

  /// Socket option to report aborted connections on accept.
  /**
   * Implements a custom socket option that determines whether or not an accept
   * operation is permitted to fail with asio::error::connection_aborted.
   * By default the option is false.
   *
   * @par Examples:
   * Setting the option:
   * @code
   * asio::ip::tcp::acceptor acceptor(io_service); 
   * ...
   * asio::socket_base::enable_connection_aborted option(true);
   * acceptor.set_option(option);
   * @endcode
   *
   * @par
   * Getting the current option value:
   * @code
   * asio::ip::tcp::acceptor acceptor(io_service); 
   * ...
   * asio::socket_base::enable_connection_aborted option;
   * acceptor.get_option(option);
   * bool is_set = option.get();
   * @endcode
   *
   * @par Concepts:
   * Socket_Option, Boolean_Socket_Option.
   */
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined enable_connection_aborted;
#else
  typedef asio::detail::socket_option::boolean<
    asio::detail::custom_socket_option_level,
    asio::detail::enable_connection_aborted_option>
    enable_connection_aborted;
#endif

  /// IO control command to set the blocking mode of the socket.
  /**
   * Implements the FIONBIO IO control command.
   *
   * @par Example:
   * @code
   * asio::ip::tcp::socket socket(io_service); 
   * ...
   * asio::socket_base::non_blocking_io command(true);
   * socket.io_control(command);
   * @endcode
   *
   * @par Concepts:
   * IO_Control_Command, Boolean_IO_Control_Command.
   */
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined non_blocking_io;
#else
  typedef asio::detail::io_control::boolean<FIONBIO> non_blocking_io;
#endif

  /// IO control command to get the amount of data that can be read without
  /// blocking.
  /**
   * Implements the FIONREAD IO control command.
   *
   * @par Example:
   * @code
   * asio::ip::tcp::socket socket(io_service); 
   * ...
   * asio::socket_base::bytes_readable command(true);
   * socket.io_control(command);
   * std::size_t bytes_readable = command.get();
   * @endcode
   *
   * @par Concepts:
   * IO_Control_Command, Size_IO_Control_Command.
   */
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined bytes_readable;
#else
  typedef asio::detail::io_control::size<FIONREAD> bytes_readable;
#endif

protected:
  /// Protected destructor to prevent deletion through this type.
  ~socket_base()
  {
  }

#if BOOST_WORKAROUND(__BORLANDC__, BOOST_TESTED_AT(0x564))
private:
  // Workaround to enable the empty base optimisation with Borland C++.
  char dummy_;
#endif
};

} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // ASIO_SOCKET_BASE_HPP
