//
// basic_endpoint.hpp
// ~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2006 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_IP_BASIC_ENDPOINT_HPP
#define ASIO_IP_BASIC_ENDPOINT_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/push_options.hpp"

#include "asio/detail/push_options.hpp"
#include <boost/throw_exception.hpp>
#include <boost/detail/workaround.hpp>
#include <cstring>
#if BOOST_WORKAROUND(__BORLANDC__, BOOST_TESTED_AT(0x564))
# include <iostream>
#endif // BOOST_WORKAROUND(__BORLANDC__, BOOST_TESTED_AT(0x564))
#include "asio/detail/pop_options.hpp"

#include "asio/error.hpp"
#include "asio/ip/address.hpp"
#include "asio/detail/socket_ops.hpp"
#include "asio/detail/socket_types.hpp"

namespace asio {
namespace ip {

/// Describes an endpoint for a version-independent IP socket.
/**
 * The asio::ip::basic_endpoint class template describes an endpoint that
 * may be associated with a particular socket.
 *
 * @par Thread Safety:
 * @e Distinct @e objects: Safe.@n
 * @e Shared @e objects: Unsafe.
 *
 * @par Concepts:
 * Endpoint.
 */
template <typename Protocol>
class basic_endpoint
{
public:
  /// The protocol type associated with the endpoint.
  typedef Protocol protocol_type;

  /// The type of the endpoint structure. This type is dependent on the
  /// underlying implementation of the socket layer.
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined data_type;
#else
  typedef asio::detail::socket_addr_type data_type;
#endif

  /// The type for the size of the endpoint structure. This type is dependent on
  /// the underlying implementation of the socket layer.
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined size_type;
#else
  typedef asio::detail::socket_addr_len_type size_type;
#endif

  /// Default constructor.
  basic_endpoint()
  {
    asio::detail::sockaddr_in4_type& data
      = reinterpret_cast<asio::detail::sockaddr_in4_type&>(data_);
    data.sin_family = AF_INET;
    data.sin_port = 0;
    data.sin_addr.s_addr = INADDR_ANY;
  }

  /// Construct an endpoint using a port number, specified in the host's byte
  /// order. The IP address will be the any address (i.e. INADDR_ANY or
  /// in6addr_any). This constructor would typically be used for accepting new
  /// connections.
  /**
   * @par Examples:
   * To initialise an IPv4 TCP endpoint for port 1234, use:
   * @code
   * asio::ip::tcp::endpoint ep(asio::ip::tcp::v4(), 1234);
   * @endcode
   *
   * To specify an IPv6 UDP endpoint for port 9876, use:
   * @code
   * asio::ip::udp::endpoint ep(asio::ip::udp::v6(), 9876);
   * @endcode
   */
  basic_endpoint(const Protocol& protocol, unsigned short port_num)
  {
    using namespace std; // For memcpy.
    if (protocol.family() == PF_INET)
    {
      asio::detail::sockaddr_in4_type& data
        = reinterpret_cast<asio::detail::sockaddr_in4_type&>(data_);
      data.sin_family = AF_INET;
      data.sin_port =
        asio::detail::socket_ops::host_to_network_short(port_num);
      data.sin_addr.s_addr = INADDR_ANY;
    }
    else
    {
      asio::detail::sockaddr_in6_type& data
        = reinterpret_cast<asio::detail::sockaddr_in6_type&>(data_);
      data.sin6_family = AF_INET6;
      data.sin6_port =
        asio::detail::socket_ops::host_to_network_short(port_num);
      data.sin6_flowinfo = 0;
      asio::detail::in6_addr_type tmp_addr = IN6ADDR_ANY_INIT;
      data.sin6_addr = tmp_addr;
      data.sin6_scope_id = 0;
    }
  }

  /// Construct an endpoint using a port number and an IP address. This
  /// constructor may be used for accepting connections on a specific interface
  /// or for making a connection to a remote endpoint.
  basic_endpoint(const asio::ip::address& addr, unsigned short port_num)
  {
    using namespace std; // For memcpy.
    if (addr.is_v4())
    {
      asio::detail::sockaddr_in4_type& data
        = reinterpret_cast<asio::detail::sockaddr_in4_type&>(data_);
      data.sin_family = AF_INET;
      data.sin_port =
        asio::detail::socket_ops::host_to_network_short(port_num);
      data.sin_addr.s_addr =
        asio::detail::socket_ops::host_to_network_long(
            addr.to_v4().to_ulong());
    }
    else
    {
      asio::detail::sockaddr_in6_type& data
        = reinterpret_cast<asio::detail::sockaddr_in6_type&>(data_);
      data.sin6_family = AF_INET6;
      data.sin6_port =
        asio::detail::socket_ops::host_to_network_short(port_num);
      data.sin6_flowinfo = 0;
      asio::ip::address_v6 v6_addr = addr.to_v6();
      asio::ip::address_v6::bytes_type bytes = v6_addr.to_bytes();
      memcpy(data.sin6_addr.s6_addr, bytes.elems, 16);
      data.sin6_scope_id = v6_addr.scope_id();
    }
  }

  /// Copy constructor.
  basic_endpoint(const basic_endpoint& other)
    : data_(other.data_)
  {
  }

  /// Assign from another endpoint.
  basic_endpoint& operator=(const basic_endpoint& other)
  {
    data_ = other.data_;
    return *this;
  }

  /// The protocol associated with the endpoint.
  protocol_type protocol() const
  {
    if (data_.ss_family == AF_INET)
      return Protocol::v4();
    return Protocol::v6();
  }

  /// Get the underlying endpoint in the native type.
  data_type* data()
  {
    return reinterpret_cast<data_type*>(&data_);
  }

  /// Get the underlying endpoint in the native type.
  const data_type* data() const
  {
    return reinterpret_cast<const data_type*>(&data_);
  }

  /// Get the underlying size of the endpoint in the native type.
  size_type size() const
  {
    if (data_.ss_family == AF_INET)
      return sizeof(asio::detail::sockaddr_in4_type);
    else
      return sizeof(asio::detail::sockaddr_in6_type);
  }

  /// Set the underlying size of the endpoint in the native type.
  void resize(size_type size)
  {
    if (size > size_type(sizeof(data_)))
    {
      asio::error e(asio::error::invalid_argument);
      boost::throw_exception(e);
    }
  }

  /// Get the capacity of the endpoint in the native type.
  size_type capacity() const
  {
    return sizeof(data_);
  }

  /// Get the port associated with the endpoint. The port number is always in
  /// the host's byte order.
  unsigned short port() const
  {
    if (data_.ss_family == AF_INET)
    {
      return asio::detail::socket_ops::network_to_host_short(
          reinterpret_cast<const asio::detail::sockaddr_in4_type&>(
            data_).sin_port);
    }
    else
    {
      return asio::detail::socket_ops::network_to_host_short(
          reinterpret_cast<const asio::detail::sockaddr_in6_type&>(
            data_).sin6_port);
    }
  }

  /// Set the port associated with the endpoint. The port number is always in
  /// the host's byte order.
  void port(unsigned short port_num)
  {
    if (data_.ss_family == AF_INET)
    {
      reinterpret_cast<asio::detail::sockaddr_in4_type&>(data_).sin_port
        = asio::detail::socket_ops::host_to_network_short(port_num);
    }
    else
    {
      reinterpret_cast<asio::detail::sockaddr_in6_type&>(data_).sin6_port
        = asio::detail::socket_ops::host_to_network_short(port_num);
    }
  }

  /// Get the IP address associated with the endpoint.
  asio::ip::address address() const
  {
    using namespace std; // For memcpy.
    if (data_.ss_family == AF_INET)
    {
      const asio::detail::sockaddr_in4_type& data
        = reinterpret_cast<const asio::detail::sockaddr_in4_type&>(
            data_);
      return asio::ip::address_v4(
          asio::detail::socket_ops::network_to_host_long(
            data.sin_addr.s_addr));
    }
    else
    {
      const asio::detail::sockaddr_in6_type& data
        = reinterpret_cast<const asio::detail::sockaddr_in6_type&>(
            data_);
      asio::ip::address_v6::bytes_type bytes;
      memcpy(bytes.elems, data.sin6_addr.s6_addr, 16);
      return asio::ip::address_v6(bytes, data.sin6_scope_id);
    }
  }

  /// Set the IP address associated with the endpoint.
  void address(const asio::ip::address& addr)
  {
    basic_endpoint<Protocol> tmp_endpoint(addr, port());
    data_ = tmp_endpoint.data_;
  }

  /// Compare two endpoints for equality.
  friend bool operator==(const basic_endpoint<Protocol>& e1,
      const basic_endpoint<Protocol>& e2)
  {
    return e1.address() == e2.address() && e1.port() == e2.port();
  }

  /// Compare two endpoints for inequality.
  friend bool operator!=(const basic_endpoint<Protocol>& e1,
      const basic_endpoint<Protocol>& e2)
  {
    return e1.address() != e2.address() || e1.port() != e2.port();
  }

  /// Compare endpoints for ordering.
  friend bool operator<(const basic_endpoint<Protocol>& e1,
      const basic_endpoint<Protocol>& e2)
  {
    if (e1.address() < e2.address())
      return true;
    if (e1.address() != e2.address())
      return false;
    return e1.port() < e2.port();
  }

private:
  // The underlying IP socket address.
  asio::detail::sockaddr_storage_type data_;
};

/// Output an endpoint as a string.
/**
 * Used to output a human-readable string for a specified endpoint.
 *
 * @param os The output stream to which the string will be written.
 *
 * @param endpoint The endpoint to be written.
 *
 * @return The output stream.
 *
 * @relates asio::ip::basic_endpoint
 */
#if BOOST_WORKAROUND(__BORLANDC__, BOOST_TESTED_AT(0x564))
template <typename Protocol>
std::ostream& operator<<(std::ostream& os,
    const basic_endpoint<Protocol>& endpoint)
{
  const address& addr = endpoint.address();
  if (addr.is_v4())
    os << addr.to_string();
  else
    os << '[' << addr.to_string() << ']';
  os << ':' << endpoint.port();
  return os;
}
#else // BOOST_WORKAROUND(__BORLANDC__, BOOST_TESTED_AT(0x564))
template <typename Elem, typename Traits, typename Protocol>
std::basic_ostream<Elem, Traits>& operator<<(
    std::basic_ostream<Elem, Traits>& os,
    const basic_endpoint<Protocol>& endpoint)
{
  const address& addr = endpoint.address();
  if (addr.is_v4())
    os << addr.to_string();
  else
    os << '[' << addr.to_string() << ']';
  os << ':' << endpoint.port();
  return os;
}
#endif // BOOST_WORKAROUND(__BORLANDC__, BOOST_TESTED_AT(0x564))

} // namespace ip
} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // ASIO_IP_BASIC_ENDPOINT_HPP
