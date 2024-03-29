//
// socket_ops.hpp
// ~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2006 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_SOCKET_OPS_HPP
#define ASIO_DETAIL_SOCKET_OPS_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/push_options.hpp"

#include "asio/detail/push_options.hpp"
#include <boost/config.hpp>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <boost/detail/workaround.hpp>
#include <new>
#include "asio/detail/pop_options.hpp"

#include "asio/error.hpp"
#include "asio/detail/socket_types.hpp"

namespace asio {
namespace detail {
namespace socket_ops {

inline int get_error()
{
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  return WSAGetLastError();
#else // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  return errno;
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
}

inline void set_error(int error)
{
  errno = error;
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  WSASetLastError(error);
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
}

template <typename ReturnType>
inline ReturnType error_wrapper(ReturnType return_value)
{
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  errno = WSAGetLastError();
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  return return_value;
}

inline socket_type accept(socket_type s, socket_addr_type* addr,
    socket_addr_len_type* addrlen)
{
  set_error(0);
#if defined(__MACH__) && defined(__APPLE__)
  socket_type new_s = error_wrapper(::accept(s, addr, addrlen));
  if (new_s == invalid_socket)
    return new_s;

  int optval = 1;
  int result = error_wrapper(::setsockopt(new_s,
        SOL_SOCKET, SO_NOSIGPIPE, &optval, sizeof(optval)));
  if (result != 0)
  {
    ::close(new_s);
    return invalid_socket;
  }

  return new_s;
#else
  return error_wrapper(::accept(s, addr, addrlen));
#endif
}

inline int bind(socket_type s, const socket_addr_type* addr,
    socket_addr_len_type addrlen)
{
  set_error(0);
  return error_wrapper(::bind(s, addr, addrlen));
}

inline int close(socket_type s)
{
  set_error(0);
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  return error_wrapper(::closesocket(s));
#else // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  return error_wrapper(::close(s));
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
}

inline int shutdown(socket_type s, int what)
{
  set_error(0);
  return error_wrapper(::shutdown(s, what));
}

inline int connect(socket_type s, const socket_addr_type* addr,
    socket_addr_len_type addrlen)
{
  set_error(0);
  return error_wrapper(::connect(s, addr, addrlen));
}

inline int listen(socket_type s, int backlog)
{
  set_error(0);
  return error_wrapper(::listen(s, backlog));
}

#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
typedef WSABUF buf;
#else // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
typedef iovec buf;
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)

inline void init_buf(buf& b, void* data, size_t size)
{
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  b.buf = static_cast<char*>(data);
  b.len = static_cast<u_long>(size);
#else // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  b.iov_base = data;
  b.iov_len = size;
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
}

inline void init_buf(buf& b, const void* data, size_t size)
{
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  b.buf = static_cast<char*>(const_cast<void*>(data));
  b.len = static_cast<u_long>(size);
#else // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  b.iov_base = const_cast<void*>(data);
  b.iov_len = size;
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
}

inline int recv(socket_type s, buf* bufs, size_t count, int flags)
{
  set_error(0);
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  // Receive some data.
  DWORD recv_buf_count = static_cast<DWORD>(count);
  DWORD bytes_transferred = 0;
  DWORD recv_flags = flags;
  int result = error_wrapper(::WSARecv(s, bufs,
        recv_buf_count, &bytes_transferred, &recv_flags, 0, 0));
  if (result != 0)
    return -1;
  return bytes_transferred;
#else // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  msghdr msg;
  msg.msg_name = 0;
  msg.msg_namelen = 0;
  msg.msg_iov = bufs;
  msg.msg_iovlen = count;
  msg.msg_control = 0;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;
  return error_wrapper(::recvmsg(s, &msg, flags));
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
}

inline int recvfrom(socket_type s, buf* bufs, size_t count, int flags,
    socket_addr_type* addr, socket_addr_len_type* addrlen)
{
  set_error(0);
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  // Receive some data.
  DWORD recv_buf_count = static_cast<DWORD>(count);
  DWORD bytes_transferred = 0;
  DWORD recv_flags = flags;
  int result = error_wrapper(::WSARecvFrom(s, bufs, recv_buf_count,
        &bytes_transferred, &recv_flags, addr, addrlen, 0, 0));
  if (result != 0)
    return -1;
  return bytes_transferred;
#else // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  msghdr msg;
  msg.msg_name = addr;
  msg.msg_namelen = *addrlen;
  msg.msg_iov = bufs;
  msg.msg_iovlen = count;
  msg.msg_control = 0;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;
  int result = error_wrapper(::recvmsg(s, &msg, flags));
  *addrlen = msg.msg_namelen;
  return result;
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
}

inline int send(socket_type s, const buf* bufs, size_t count, int flags)
{
  set_error(0);
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  // Send the data.
  DWORD send_buf_count = static_cast<DWORD>(count);
  DWORD bytes_transferred = 0;
  DWORD send_flags = flags;
  int result = error_wrapper(::WSASend(s, const_cast<buf*>(bufs),
        send_buf_count, &bytes_transferred, send_flags, 0, 0));
  if (result != 0)
    return -1;
  return bytes_transferred;
#else // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  msghdr msg;
  msg.msg_name = 0;
  msg.msg_namelen = 0;
  msg.msg_iov = const_cast<buf*>(bufs);
  msg.msg_iovlen = count;
  msg.msg_control = 0;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;
#if defined(__linux__)
  flags |= MSG_NOSIGNAL;
#endif // defined(__linux__)
  return error_wrapper(::sendmsg(s, &msg, flags));
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
}

inline int sendto(socket_type s, const buf* bufs, size_t count, int flags,
    const socket_addr_type* addr, socket_addr_len_type addrlen)
{
  set_error(0);
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  // Send the data.
  DWORD send_buf_count = static_cast<DWORD>(count);
  DWORD bytes_transferred = 0;
  int result = ::WSASendTo(s, const_cast<buf*>(bufs), send_buf_count,
      &bytes_transferred, flags, addr, addrlen, 0, 0);
  if (result != 0)
    return -1;
  return bytes_transferred;
#else // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  msghdr msg;
  msg.msg_name = const_cast<socket_addr_type*>(addr);
  msg.msg_namelen = addrlen;
  msg.msg_iov = const_cast<buf*>(bufs);
  msg.msg_iovlen = count;
  msg.msg_control = 0;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;
#if defined(__linux__)
  flags |= MSG_NOSIGNAL;
#endif // defined(__linux__)
  return error_wrapper(::sendmsg(s, &msg, flags));
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
}

inline socket_type socket(int af, int type, int protocol)
{
  set_error(0);
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  return error_wrapper(::WSASocket(af, type, protocol, 0, 0,
        WSA_FLAG_OVERLAPPED));
#elif defined(__MACH__) && defined(__APPLE__)
  socket_type s = error_wrapper(::socket(af, type, protocol));
  if (s == invalid_socket)
    return s;

  int optval = 1;
  int result = error_wrapper(::setsockopt(s,
        SOL_SOCKET, SO_NOSIGPIPE, &optval, sizeof(optval)));
  if (result != 0)
  {
    ::close(s);
    return invalid_socket;
  }

  return s;
#else
  return error_wrapper(::socket(af, type, protocol));
#endif
}

inline int setsockopt(socket_type s, int level, int optname,
    const void* optval, size_t optlen)
{
  set_error(0);
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  return error_wrapper(::setsockopt(s, level, optname,
        reinterpret_cast<const char*>(optval), static_cast<int>(optlen)));
#else // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  return error_wrapper(::setsockopt(s, level, optname, optval,
        static_cast<socklen_t>(optlen)));
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
}

inline int getsockopt(socket_type s, int level, int optname, void* optval,
    size_t* optlen)
{
  set_error(0);
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  int tmp_optlen = static_cast<int>(*optlen);
  int result = error_wrapper(::getsockopt(s, level, optname,
        reinterpret_cast<char*>(optval), &tmp_optlen));
  *optlen = static_cast<size_t>(tmp_optlen);
  return result;
#else // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  socklen_t tmp_optlen = static_cast<socklen_t>(*optlen);
  int result = error_wrapper(::getsockopt(s, level, optname,
        optval, &tmp_optlen));
  *optlen = static_cast<size_t>(tmp_optlen);
  return result;
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
}

inline int getpeername(socket_type s, socket_addr_type* addr,
    socket_addr_len_type* addrlen)
{
  set_error(0);
  return error_wrapper(::getpeername(s, addr, addrlen));
}

inline int getsockname(socket_type s, socket_addr_type* addr,
    socket_addr_len_type* addrlen)
{
  set_error(0);
  return error_wrapper(::getsockname(s, addr, addrlen));
}

inline int ioctl(socket_type s, long cmd, ioctl_arg_type* arg)
{
  set_error(0);
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  return error_wrapper(::ioctlsocket(s, cmd, arg));
#else // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  return error_wrapper(::ioctl(s, cmd, arg));
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
}

inline int select(int nfds, fd_set* readfds, fd_set* writefds,
    fd_set* exceptfds, timeval* timeout)
{
  set_error(0);
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  if (!readfds && !writefds && !exceptfds && timeout)
  {
    DWORD milliseconds = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
    if (milliseconds == 0)
      milliseconds = 1; // Force context switch.
    ::Sleep(milliseconds);
    return 0;
  }

  // The select() call allows timeout values measured in microseconds, but the
  // system clock (as wrapped by boost::posix_time::microsec_clock) typically
  // has a resolution of 10 milliseconds. This can lead to a spinning select
  // reactor, meaning increased CPU usage, when waiting for the earliest
  // scheduled timeout if it's less than 10 milliseconds away. To avoid a tight
  // spin we'll use a minimum timeout of 1 millisecond.
  if (timeout && timeout->tv_sec == 0
      && timeout->tv_usec > 0 && timeout->tv_usec < 1000)
    timeout->tv_usec = 1000;
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  return error_wrapper(::select(nfds, readfds, writefds, exceptfds, timeout));
}

inline int poll_read(socket_type s)
{
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  FD_SET fds;
  FD_ZERO(&fds);
  FD_SET(s, &fds);
  set_error(0);
  return error_wrapper(::select(s, &fds, 0, 0, 0));
#else // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  pollfd fds;
  fds.fd = s;
  fds.events = POLLIN;
  fds.revents = 0;
  set_error(0);
  return error_wrapper(::poll(&fds, 1, -1));
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
}

inline int poll_write(socket_type s)
{
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  FD_SET fds;
  FD_ZERO(&fds);
  FD_SET(s, &fds);
  set_error(0);
  return error_wrapper(::select(s, 0, &fds, 0, 0));
#else // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  pollfd fds;
  fds.fd = s;
  fds.events = POLLOUT;
  fds.revents = 0;
  set_error(0);
  return error_wrapper(::poll(&fds, 1, -1));
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
}

inline const char* inet_ntop(int af, const void* src, char* dest, size_t length,
    unsigned long scope_id = 0)
{
  set_error(0);
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  using namespace std; // For memcpy.

  if (af != AF_INET && af != AF_INET6)
  {
    set_error(asio::error::address_family_not_supported);
    return 0;
  }

  sockaddr_storage_type address;
  DWORD address_length;
  if (af == AF_INET)
  {
    address_length = sizeof(sockaddr_in4_type);
    sockaddr_in4_type* ipv4_address =
      reinterpret_cast<sockaddr_in4_type*>(&address);
    ipv4_address->sin_family = AF_INET;
    ipv4_address->sin_port = 0;
    memcpy(&ipv4_address->sin_addr, src, sizeof(in4_addr_type));
  }
  else // AF_INET6
  {
    address_length = sizeof(sockaddr_in6_type);
    sockaddr_in6_type* ipv6_address =
      reinterpret_cast<sockaddr_in6_type*>(&address);
    ipv6_address->sin6_family = AF_INET6;
    ipv6_address->sin6_port = 0;
    ipv6_address->sin6_flowinfo = 0;
    ipv6_address->sin6_scope_id = scope_id;
    memcpy(&ipv6_address->sin6_addr, src, sizeof(in6_addr_type));
  }

  DWORD string_length = length;
  int result = error_wrapper(::WSAAddressToStringA(
        reinterpret_cast<sockaddr*>(&address),
        address_length, 0, dest, &string_length));

  // Windows may not set an error code on failure.
  if (result == socket_error_retval && get_error() == 0)
    set_error(asio::error::invalid_argument);

  return result == socket_error_retval ? 0 : dest;
#else // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  const char* result = error_wrapper(::inet_ntop(af, src, dest, length));
  if (result == 0 && get_error() == 0)
    set_error(asio::error::invalid_argument);
  if (result != 0 && af == AF_INET6 && scope_id != 0)
  {
    using namespace std; // For strcat and sprintf.
    char if_name[IF_NAMESIZE + 1] = "%";
    const in6_addr_type* ipv6_address = static_cast<const in6_addr_type*>(src);
    bool is_link_local = IN6_IS_ADDR_LINKLOCAL(ipv6_address);
    if (!is_link_local || if_indextoname(scope_id, if_name + 1) == 0)
      sprintf(if_name + 1, "%lu", scope_id);
    strcat(dest, if_name);
  }
  return result;
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
}

inline int inet_pton(int af, const char* src, void* dest,
    unsigned long* scope_id = 0)
{
  set_error(0);
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  using namespace std; // For memcpy and strcmp.

  if (af != AF_INET && af != AF_INET6)
  {
    set_error(asio::error::address_family_not_supported);
    return -1;
  }

  sockaddr_storage_type address;
  int address_length = sizeof(sockaddr_storage_type);
  int result = error_wrapper(::WSAStringToAddressA(
        const_cast<char*>(src), af, 0,
        reinterpret_cast<sockaddr*>(&address),
        &address_length));

  if (af == AF_INET)
  {
    if (result != socket_error_retval)
    {
      sockaddr_in4_type* ipv4_address =
        reinterpret_cast<sockaddr_in4_type*>(&address);
      memcpy(dest, &ipv4_address->sin_addr, sizeof(in4_addr_type));
    }
    else if (strcmp(src, "255.255.255.255") == 0)
    {
      static_cast<in4_addr_type*>(dest)->s_addr = INADDR_NONE;
    }
  }
  else // AF_INET6
  {
    if (result != socket_error_retval)
    {
      sockaddr_in6_type* ipv6_address =
        reinterpret_cast<sockaddr_in6_type*>(&address);
      memcpy(dest, &ipv6_address->sin6_addr, sizeof(in6_addr_type));
      if (scope_id)
        *scope_id = ipv6_address->sin6_scope_id;
    }
  }

  // Windows may not set an error code on failure.
  if (result == socket_error_retval && get_error() == 0)
    set_error(asio::error::invalid_argument);

  return result == socket_error_retval ? -1 : 1;
#else // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  int result = error_wrapper(::inet_pton(af, src, dest));
  if (result <= 0 && get_error() == 0)
    set_error(asio::error::invalid_argument);
  if (result > 0 && af == AF_INET6 && scope_id)
  {
    using namespace std; // For strchr and atoi.
    *scope_id = 0;
    if (const char* if_name = strchr(src, '%'))
    {
      in6_addr_type* ipv6_address = static_cast<in6_addr_type*>(dest);
      bool is_link_local = IN6_IS_ADDR_LINKLOCAL(ipv6_address);
      if (is_link_local)
        *scope_id = if_nametoindex(if_name + 1);
      if (*scope_id == 0)
        *scope_id = atoi(if_name + 1);
    }
  }
  return result;
#endif // defined(BOOST_WINDOWS) || defined(__CYGWIN__)
}

inline int gethostname(char* name, int namelen)
{
  set_error(0);
  return error_wrapper(::gethostname(name, namelen));
}

inline int translate_netdb_error(int error)
{
  switch (error)
  {
  case 0:
    return asio::error::success;
  case HOST_NOT_FOUND:
    return asio::error::host_not_found;
  case TRY_AGAIN:
    return asio::error::host_not_found_try_again;
  case NO_RECOVERY:
    return asio::error::no_recovery;
  case NO_DATA:
    return asio::error::no_data;
  default:
    BOOST_ASSERT(false);
    return get_error();
  }
}

inline hostent* gethostbyaddr(const char* addr, int length, int af,
    hostent* result, char* buffer, int buflength, int* error)
{
  set_error(0);
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  (void)(buffer);
  (void)(buflength);
  hostent* retval = error_wrapper(::gethostbyaddr(addr, length, af));
  *error = get_error();
  if (!retval)
    return 0;
  *result = *retval;
  return retval;
#elif defined(__sun) || defined(__QNX__)
  hostent* retval = error_wrapper(::gethostbyaddr_r(addr, length, af, result,
        buffer, buflength, error));
  *error = translate_netdb_error(*error);
  return retval;
#elif defined(__MACH__) && defined(__APPLE__)
  (void)(buffer);
  (void)(buflength);
  hostent* retval = error_wrapper(::getipnodebyaddr(addr, length, af, error));
  *error = translate_netdb_error(*error);
  if (!retval)
    return 0;
  *result = *retval;
  return retval;
#else
  hostent* retval = 0;
  error_wrapper(::gethostbyaddr_r(addr, length, af, result, buffer,
        buflength, &retval, error));
  *error = translate_netdb_error(*error);
  return retval;
#endif
}

inline hostent* gethostbyname(const char* name, int af, struct hostent* result,
    char* buffer, int buflength, int* error, int ai_flags = 0)
{
  set_error(0);
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
  (void)(buffer);
  (void)(buflength);
  (void)(ai_flags);
  if (af != AF_INET)
  {
    *error = asio::error::address_family_not_supported;
    return 0;
  }
  hostent* retval = error_wrapper(::gethostbyname(name));
  *error = get_error();
  if (!retval)
    return 0;
  *result = *retval;
  return result;
#elif defined(__sun) || defined(__QNX__)
  (void)(ai_flags);
  if (af != AF_INET)
  {
    *error = asio::error::address_family_not_supported;
    return 0;
  }
  hostent* retval = error_wrapper(::gethostbyname_r(name, result, buffer,
        buflength, error));
  *error = translate_netdb_error(*error);
  return retval;
#elif defined(__MACH__) && defined(__APPLE__)
  (void)(buffer);
  (void)(buflength);
  hostent* retval = error_wrapper(::getipnodebyname(
        name, af, ai_flags, error));
  *error = translate_netdb_error(*error);
  if (!retval)
    return 0;
  *result = *retval;
  return retval;
#else
  (void)(ai_flags);
  if (af != AF_INET)
  {
    *error = asio::error::address_family_not_supported;
    return 0;
  }
  hostent* retval = 0;
  error_wrapper(::gethostbyname_r(name, result, buffer, buflength, &retval,
        error));
  *error = translate_netdb_error(*error);
  return retval;
#endif
}

inline void freehostent(hostent* h)
{
#if defined(__MACH__) && defined(__APPLE__)
  if (h)
    ::freehostent(h);
#else
  (void)(h);
#endif
}

// Emulation of getaddrinfo based on implementation in:
// Stevens, W. R., UNIX Network Programming Vol. 1, 2nd Ed., Prentice-Hall 1998.

struct gai_search
{
  const char* host;
  int family;
};

inline int gai_nsearch(const char* host,
    const addrinfo_type* hints, gai_search (&search)[2])
{
  int search_count = 0;
  if (host == 0 || host[0] == '\0')
  {
    if (hints->ai_flags & AI_PASSIVE)
    {
      // No host and AI_PASSIVE implies wildcard bind.
      switch (hints->ai_family)
      {
      case AF_INET:
        search[search_count].host = "0.0.0.0";
        search[search_count].family = AF_INET;
        ++search_count;
        break;
      case AF_INET6:
        search[search_count].host = "0::0";
        search[search_count].family = AF_INET6;
        ++search_count;
        break;
      case AF_UNSPEC:
        search[search_count].host = "0::0";
        search[search_count].family = AF_INET6;
        ++search_count;
        search[search_count].host = "0.0.0.0";
        search[search_count].family = AF_INET;
        ++search_count;
        break;
      default:
        break;
      }
    }
    else
    {
      // No host and not AI_PASSIVE means connect to local host.
      switch (hints->ai_family)
      {
      case AF_INET:
        search[search_count].host = "localhost";
        search[search_count].family = AF_INET;
        ++search_count;
        break;
      case AF_INET6:
        search[search_count].host = "localhost";
        search[search_count].family = AF_INET6;
        ++search_count;
        break;
      case AF_UNSPEC:
        search[search_count].host = "localhost";
        search[search_count].family = AF_INET6;
        ++search_count;
        search[search_count].host = "localhost";
        search[search_count].family = AF_INET;
        ++search_count;
        break;
      default:
        break;
      }
    }
  }
  else
  {
    // Host is specified.
    switch (hints->ai_family)
    {
    case AF_INET:
      search[search_count].host = host;
      search[search_count].family = AF_INET;
      ++search_count;
      break;
    case AF_INET6:
      search[search_count].host = host;
      search[search_count].family = AF_INET6;
      ++search_count;
      break;
    case AF_UNSPEC:
      search[search_count].host = host;
      search[search_count].family = AF_INET6;
      ++search_count;
      search[search_count].host = host;
      search[search_count].family = AF_INET;
      ++search_count;
      break;
    default:
      break;
    }
  }
  return search_count;
}

template <typename T>
inline T* gai_alloc(std::size_t size = sizeof(T))
{
  using namespace std;
  T* p = static_cast<T*>(::operator new(size, std::nothrow));
  if (p)
    memset(p, 0, size);
  return p;
}

inline void gai_free(void* p)
{
  ::operator delete(p);
}

enum { gai_clone_flag = 1 << 30 };

inline int gai_aistruct(addrinfo_type*** next, const addrinfo_type* hints,
    const void* addr, int family)
{
  using namespace std;

  addrinfo_type* ai = gai_alloc<addrinfo_type>();
  if (ai == 0)
    return EAI_MEMORY;

  ai->ai_next = 0;
  **next = ai;
  *next = &ai->ai_next;

  ai->ai_canonname = 0;
  ai->ai_socktype = hints->ai_socktype;
  if (ai->ai_socktype == 0)
    ai->ai_flags |= gai_clone_flag;
  ai->ai_protocol = hints->ai_protocol;
  ai->ai_family = family;

  switch (ai->ai_family)
  {
  case AF_INET:
    {
      sockaddr_in4_type* sinptr = gai_alloc<sockaddr_in4_type>();
      if (sinptr == 0)
        return EAI_MEMORY;
      sinptr->sin_family = AF_INET;
      memcpy(&sinptr->sin_addr, addr, sizeof(in4_addr_type));
      ai->ai_addr = reinterpret_cast<sockaddr*>(sinptr);
      ai->ai_addrlen = sizeof(sockaddr_in4_type);
      break;
    }
  case AF_INET6:
    {
      sockaddr_in6_type* sin6ptr = gai_alloc<sockaddr_in6_type>();
      if (sin6ptr == 0)
        return EAI_MEMORY;
      sin6ptr->sin6_family = AF_INET6;
      memcpy(&sin6ptr->sin6_addr, addr, sizeof(in6_addr_type));
      ai->ai_addr = reinterpret_cast<sockaddr*>(sin6ptr);
      ai->ai_addrlen = sizeof(sockaddr_in6_type);
      break;
    }
  default:
    break;
  }

  return 0;
}

inline addrinfo_type* gai_clone(addrinfo_type* ai)
{
  using namespace std;

  addrinfo_type* new_ai = gai_alloc<addrinfo_type>();
  if (new_ai == 0)
    return new_ai;

  new_ai->ai_next = ai->ai_next;
  ai->ai_next = new_ai;

  new_ai->ai_flags = 0;
  new_ai->ai_family = ai->ai_family;
  new_ai->ai_socktype = ai->ai_socktype;
  new_ai->ai_protocol = ai->ai_protocol;
  new_ai->ai_canonname = 0;
  new_ai->ai_addrlen = ai->ai_addrlen;
  new_ai->ai_addr = gai_alloc<sockaddr>(ai->ai_addrlen);
  memcpy(new_ai->ai_addr, ai->ai_addr, ai->ai_addrlen);

  return new_ai;
}

inline int gai_port(addrinfo_type* aihead, int port, int socktype)
{
  int num_found = 0;

  for (addrinfo_type* ai = aihead; ai; ai = ai->ai_next)
  {
    if (ai->ai_flags & gai_clone_flag)
    {
      if (ai->ai_socktype != 0)
      {
        ai = gai_clone(ai);
        if (ai == 0)
          return -1;
        // ai now points to newly cloned entry.
      }
    }
    else if (ai->ai_socktype != socktype)
    {
      // Ignore if mismatch on socket type.
      continue;
    }

    ai->ai_socktype = socktype;

    switch (ai->ai_family)
    {
    case AF_INET:
      {
        sockaddr_in4_type* sinptr =
          reinterpret_cast<sockaddr_in4_type*>(ai->ai_addr);
        sinptr->sin_port = port;
        ++num_found;
        break;
      }
    case AF_INET6:
      {
        sockaddr_in6_type* sin6ptr =
          reinterpret_cast<sockaddr_in6_type*>(ai->ai_addr);
        sin6ptr->sin6_port = port;
        ++num_found;
        break;
      }
    default:
      break;
    }
  }

  return num_found;
}

inline int gai_serv(addrinfo_type* aihead,
    const addrinfo_type* hints, const char* serv)
{
  using namespace std;

  int num_found = 0;

  if (
#if defined(AI_NUMERICSERV)
      (hints->ai_flags & AI_NUMERICSERV) ||
#endif
      isdigit(serv[0]))
  {
    int port = htons(atoi(serv));
    if (hints->ai_socktype)
    {
      // Caller specifies socket type.
      int rc = gai_port(aihead, port, hints->ai_socktype);
      if (rc < 0)
        return EAI_MEMORY;
      num_found += rc;
    }
    else
    {
      // Caller does not specify socket type.
      int rc = gai_port(aihead, port, SOCK_STREAM);
      if (rc < 0)
        return EAI_MEMORY;
      num_found += rc;
      rc = gai_port(aihead, port, SOCK_DGRAM);
      if (rc < 0)
        return EAI_MEMORY;
      num_found += rc;
    }
  }
  else
  {
    // Try service name with TCP first, then UDP.
    if (hints->ai_socktype == 0 || hints->ai_socktype == SOCK_STREAM)
    {
      servent* sptr = getservbyname(serv, "tcp");
      if (sptr != 0)
      {
        int rc = gai_port(aihead, sptr->s_port, SOCK_STREAM);
        if (rc < 0)
          return EAI_MEMORY;
        num_found += rc;
      }
    }
    if (hints->ai_socktype == 0 || hints->ai_socktype == SOCK_DGRAM)
    {
      servent* sptr = getservbyname(serv, "udp");
      if (sptr != 0)
      {
        int rc = gai_port(aihead, sptr->s_port, SOCK_DGRAM);
        if (rc < 0)
          return EAI_MEMORY;
        num_found += rc;
      }
    }
  }

  if (num_found == 0)
  {
    if (hints->ai_socktype == 0)
    {
      // All calls to getservbyname() failed.
      return EAI_NONAME;
    }
    else
    {
      // Service not supported for socket type.
      return EAI_SERVICE;
    }
  }

  return 0;
}

inline int gai_echeck(const char* host, const char* service,
    int flags, int family, int socktype, int protocol)
{
  (void)(flags);
  (void)(protocol);

  // Host or service must be specified.
  if (host == 0 || host[0] == '\0')
    if (service == 0 || service[0] == '\0')
      return EAI_NONAME;

  // Check combination of family and socket type.
  switch (family)
  {
  case AF_UNSPEC:
    break;
  case AF_INET:
  case AF_INET6:
    if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM)
      return EAI_SOCKTYPE;
    break;
  default:
    return EAI_FAMILY;
  }

  return 0;
}

inline void freeaddrinfo_emulation(addrinfo_type* aihead)
{
  addrinfo_type* ai = aihead;
  while (ai)
  {
    gai_free(ai->ai_addr);
    gai_free(ai->ai_canonname);
    addrinfo_type* ainext = ai->ai_next;
    gai_free(ai);
    ai = ainext;
  }
}

inline int getaddrinfo_emulation(const char* host, const char* service,
    const addrinfo_type* hintsp, addrinfo_type** result)
{
  // Set up linked list of addrinfo structures.
  addrinfo_type* aihead = 0;
  addrinfo_type** ainext = &aihead;
  char* canon = 0;

  // Supply default hints if not specified by caller.
  addrinfo_type hints = addrinfo_type();
  hints.ai_family = AF_UNSPEC;
  if (hintsp)
    hints = *hintsp;

  // If the resolution is not specifically for AF_INET6, remove the AI_V4MAPPED
  // and AI_ALL flags.
#if defined(AI_V4MAPPED)
  if (hints.ai_family != AF_INET6)
    hints.ai_flags &= ~AI_V4MAPPED;
#endif
#if defined(AI_ALL)
  if (hints.ai_family != AF_INET6)
    hints.ai_flags &= ~AI_ALL;
#endif

  // Basic error checking.
  int rc = gai_echeck(host, service, hints.ai_flags, hints.ai_family,
      hints.ai_socktype, hints.ai_protocol);
  if (rc != 0)
  {
    freeaddrinfo_emulation(aihead);
    return rc;
  }

  gai_search search[2];
  int search_count = gai_nsearch(host, &hints, search);
  for (gai_search* sptr = search; sptr < search + search_count; ++sptr)
  {
    // Check for IPv4 dotted decimal string.
    in4_addr_type inaddr;
    if (socket_ops::inet_pton(AF_INET, sptr->host, &inaddr) == 1)
    {
      if (hints.ai_family != AF_UNSPEC && hints.ai_family != AF_INET)
      {
        freeaddrinfo_emulation(aihead);
        gai_free(canon);
        return EAI_FAMILY;
      }
      if (sptr->family == AF_INET)
      {
        rc = gai_aistruct(&ainext, &hints, &inaddr, AF_INET);
        if (rc != 0)
        {
          freeaddrinfo_emulation(aihead);
          gai_free(canon);
          return rc;
        }
      }
      continue;
    }

    // Check for IPv6 hex string.
    in6_addr_type in6addr;
    if (socket_ops::inet_pton(AF_INET6, sptr->host, &in6addr) == 1)
    {
      if (hints.ai_family != AF_UNSPEC && hints.ai_family != AF_INET6)
      {
        freeaddrinfo_emulation(aihead);
        gai_free(canon);
        return EAI_FAMILY;
      }
      if (sptr->family == AF_INET6)
      {
        rc = gai_aistruct(&ainext, &hints, &in6addr, AF_INET6);
        if (rc != 0)
        {
          freeaddrinfo_emulation(aihead);
          gai_free(canon);
          return rc;
        }
      }
      continue;
    }

    // Look up hostname.
    hostent hent;
    char hbuf[8192] = "";
    int herr = 0;
    hostent* hptr = socket_ops::gethostbyname(sptr->host,
        sptr->family, &hent, hbuf, sizeof(hbuf), &herr, hints.ai_flags);
    if (hptr == 0)
    {
      if (search_count == 2)
      {
        // Failure is OK if there are multiple searches.
        continue;
      }
      freeaddrinfo_emulation(aihead);
      gai_free(canon);
      switch (herr)
      {
      case HOST_NOT_FOUND:
        return EAI_NONAME;
      case TRY_AGAIN:
        return EAI_AGAIN;
      case NO_RECOVERY:
        return EAI_FAIL;
      case NO_DATA:
        return EAI_NODATA;
      default:
        return EAI_NONAME;
      }
    }

    // Check for address family mismatch if one was specified.
    if (hints.ai_family != AF_UNSPEC && hints.ai_family != hptr->h_addrtype)
    {
      freeaddrinfo_emulation(aihead);
      gai_free(canon);
      socket_ops::freehostent(hptr);
      return EAI_FAMILY;
    }

    // Save canonical name first time.
    if (host != 0 && host[0] != '\0' && hptr->h_name && hptr->h_name[0]
        && (hints.ai_flags & AI_CANONNAME) && canon == 0)
    {
      canon = gai_alloc<char>(strlen(hptr->h_name) + 1);
      if (canon == 0)
      {
        freeaddrinfo_emulation(aihead);
        socket_ops::freehostent(hptr);
        return EAI_MEMORY;
      }
      strcpy(canon, hptr->h_name);
    }

    // Create an addrinfo structure for each returned address.
    for (char** ap = hptr->h_addr_list; *ap; ++ap)
    {
      rc = gai_aistruct(&ainext, &hints, *ap, hptr->h_addrtype);
      if (rc != 0)
      {
        freeaddrinfo_emulation(aihead);
        gai_free(canon);
        socket_ops::freehostent(hptr);
        return EAI_FAMILY;
      }
    }

    socket_ops::freehostent(hptr);
  }

  // Check if we found anything.
  if (aihead == 0)
  {
    gai_free(canon);
    return EAI_NONAME;
  }

  // Return canonical name in first entry.
  if (host != 0 && host[0] != '\0' && (hints.ai_flags & AI_CANONNAME))
  {
    if (canon)
    {
      aihead->ai_canonname = canon;
      canon = 0;
    }
    else
    {
      aihead->ai_canonname = gai_alloc<char>(strlen(search[0].host) + 1);
      if (aihead->ai_canonname == 0)
      {
        freeaddrinfo_emulation(aihead);
        return EAI_MEMORY;
      }
      strcpy(aihead->ai_canonname, search[0].host);
    }
  }
  gai_free(canon);

  // Process the service name.
  if (service != 0 && service[0] != '\0')
  {
    rc = gai_serv(aihead, &hints, service);
    if (rc != 0)
    {
      freeaddrinfo_emulation(aihead);
      return rc;
    }
  }

  // Return result to caller.
  *result = aihead;
  return 0;
}

inline int translate_addrinfo_error(int error)
{
  switch (error)
  {
  case 0:
    return asio::error::success;
  case EAI_AGAIN:
    return asio::error::host_not_found_try_again;
  case EAI_BADFLAGS:
    return asio::error::invalid_argument;
  case EAI_FAIL:
    return asio::error::no_recovery;
  case EAI_FAMILY:
    return asio::error::address_family_not_supported;
  case EAI_MEMORY:
    return asio::error::no_memory;
  case EAI_NONAME:
    return asio::error::host_not_found;
  case EAI_SERVICE:
    return asio::error::service_not_found;
  case EAI_SOCKTYPE:
    return asio::error::socket_type_not_supported;
  default: // Possibly the non-portable EAI_SYSTEM.
    return get_error();
  }
}

inline int getaddrinfo(const char* host, const char* service,
    const addrinfo_type* hints, addrinfo_type** result)
{
  set_error(0);
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
# if defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0501)
  // Building for Windows XP, Windows Server 2003, or later.
  int error = ::getaddrinfo(host, service, hints, result);
  return translate_addrinfo_error(error);
# else
  // Building for Windows 2000 or earlier.
  typedef int (WSAAPI *gai_t)(const char*,
      const char*, const addrinfo_type*, addrinfo_type**);
  if (HMODULE winsock_module = ::GetModuleHandleA("ws2_32"))
  {
    if (gai_t gai = (gai_t)::GetProcAddress(winsock_module, "getaddrinfo"))
    {
      int error = gai(host, service, hints, result);
      return translate_addrinfo_error(error);
    }
  }
  int error = getaddrinfo_emulation(host, service, hints, result);
  return translate_addrinfo_error(error);
# endif
#elif defined(__MACH__) && defined(__APPLE__)
  int error = getaddrinfo_emulation(host, service, hints, result);
  return translate_addrinfo_error(error);
#else
  int error = ::getaddrinfo(host, service, hints, result);
  return translate_addrinfo_error(error);
#endif
}

inline void freeaddrinfo(addrinfo_type* ai)
{
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
# if defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0501)
  // Building for Windows XP, Windows Server 2003, or later.
  ::freeaddrinfo(ai);
# else
  // Building for Windows 2000 or earlier.
  typedef int (WSAAPI *fai_t)(addrinfo_type*);
  if (HMODULE winsock_module = ::GetModuleHandleA("ws2_32"))
  {
    if (fai_t fai = (fai_t)::GetProcAddress(winsock_module, "freeaddrinfo"))
    {
      fai(ai);
      return;
    }
  }
  freeaddrinfo_emulation(ai);
# endif
#elif defined(__MACH__) && defined(__APPLE__)
  freeaddrinfo_emulation(ai);
#else
  ::freeaddrinfo(ai);
#endif
}

inline int getnameinfo_emulation(const socket_addr_type* sa,
    socket_addr_len_type salen, char* host, std::size_t hostlen,
    char* serv, std::size_t servlen, int flags)
{
  using namespace std;

  const char* addr;
  size_t addr_len;
  unsigned short port;
  switch (sa->sa_family)
  {
  case AF_INET:
    if (salen != sizeof(sockaddr_in4_type))
    {
      set_error(asio::error::invalid_argument);
      return 1;
    }
    addr = reinterpret_cast<const char*>(
        &reinterpret_cast<const sockaddr_in4_type*>(sa)->sin_addr);
    addr_len = sizeof(in4_addr_type);
    port = reinterpret_cast<const sockaddr_in4_type*>(sa)->sin_port;
    break;
  case AF_INET6:
    if (salen != sizeof(sockaddr_in6_type))
    {
      set_error(asio::error::invalid_argument);
      return 1;
    }
    addr = reinterpret_cast<const char*>(
        &reinterpret_cast<const sockaddr_in6_type*>(sa)->sin6_addr);
    addr_len = sizeof(in6_addr_type);
    port = reinterpret_cast<const sockaddr_in6_type*>(sa)->sin6_port;
    break;
  default:
    set_error(asio::error::address_family_not_supported);
    return 1;
  }

  if (host && hostlen > 0)
  {
    if (flags & NI_NUMERICHOST)
    {
      if (socket_ops::inet_ntop(sa->sa_family, addr, host, hostlen) == 0)
      {
        return 1;
      }
    }
    else
    {
      hostent hent;
      char hbuf[8192] = "";
      int herr = 0;
      hostent* hptr = socket_ops::gethostbyaddr(addr, addr_len,
          sa->sa_family, &hent, hbuf, sizeof(hbuf), &herr);
      if (hptr && hptr->h_name && hptr->h_name[0] != '\0')
      {
        if (flags & NI_NOFQDN)
        {
          char* dot = strchr(hptr->h_name, '.');
          if (dot)
          {
            *dot = 0;
          }
        }
        *host = '\0';
        strncat(host, hptr->h_name, hostlen);
        socket_ops::freehostent(hptr);
      }
      else
      {
        socket_ops::freehostent(hptr);
        if (flags & NI_NAMEREQD)
        {
          set_error(asio::error::host_not_found);
          return 1;
        }
        if (socket_ops::inet_ntop(sa->sa_family, addr, host, hostlen) == 0)
        {
          return 1;
        }
      }
    }
  }

  if (serv && servlen > 0)
  {
    if (flags & NI_NUMERICSERV)
    {
      if (servlen < 6)
      {
        set_error(asio::error::no_buffer_space);
        return 1;
      }
      sprintf(serv, "%u", ntohs(port));
    }
    else
    {
#if defined(BOOST_HAS_THREADS) && defined(BOOST_HAS_PTHREADS)
      static ::pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
      ::pthread_mutex_lock(&mutex);
#endif // defined(BOOST_HAS_THREADS) && defined(BOOST_HAS_PTHREADS)
      servent* sptr = ::getservbyport(port, (flags & NI_DGRAM) ? "udp" : 0);
      if (sptr && sptr->s_name && sptr->s_name[0] != '\0')
      {
        *serv = '\0';
        strncat(serv, sptr->s_name, servlen);
      }
      else
      {
        if (servlen < 6)
        {
          set_error(asio::error::no_buffer_space);
          return 1;
        }
        sprintf(serv, "%u", ntohs(port));
      }
#if defined(BOOST_HAS_THREADS) && defined(BOOST_HAS_PTHREADS)
      ::pthread_mutex_unlock(&mutex);
#endif // defined(BOOST_HAS_THREADS) && defined(BOOST_HAS_PTHREADS)
    }
  }

  set_error(0);
  return 0;
}

inline int getnameinfo(const socket_addr_type* addr,
    socket_addr_len_type addrlen, char* host, std::size_t hostlen,
    char* serv, std::size_t servlen, int flags)
{
#if defined(BOOST_WINDOWS) || defined(__CYGWIN__)
# if defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0501)
  // Building for Windows XP, Windows Server 2003, or later.
  set_error(0);
  int error = ::getnameinfo(addr, addrlen, host, hostlen, serv, servlen, flags);
  return translate_addrinfo_error(error);
# else
  // Building for Windows 2000 or earlier.
  typedef int (WSAAPI *gni_t)(const socket_addr_type*,
      socket_addr_len_type, char*, std::size_t, char*, std::size_t, int);
  if (HMODULE winsock_module = ::GetModuleHandleA("ws2_32"))
  {
    if (gni_t gni = (gni_t)::GetProcAddress(winsock_module, "getnameinfo"))
    {
      set_error(0);
      int error = gni(addr, addrlen, host, hostlen, serv, servlen, flags);
      return translate_addrinfo_error(error);
    }
  }
  set_error(0);
  int error = getnameinfo_emulation(addr, addrlen,
      host, hostlen, serv, servlen, flags);
  return translate_addrinfo_error(error);
# endif
#elif defined(__MACH__) && defined(__APPLE__)
  using namespace std; // For memcpy.
  sockaddr_storage_type tmp_addr;
  memcpy(&tmp_addr, addr, addrlen);
  tmp_addr.ss_len = addrlen;
  addr = reinterpret_cast<socket_addr_type*>(&tmp_addr);
  set_error(0);
  int error = getnameinfo_emulation(addr, addrlen,
      host, hostlen, serv, servlen, flags);
  return translate_addrinfo_error(error);
#else
  set_error(0);
  int error = ::getnameinfo(addr, addrlen, host, hostlen, serv, servlen, flags);
  return translate_addrinfo_error(error);
#endif
}

inline u_long_type network_to_host_long(u_long_type value)
{
  return ntohl(value);
}

inline u_long_type host_to_network_long(u_long_type value)
{
  return htonl(value);
}

inline u_short_type network_to_host_short(u_short_type value)
{
  return ntohs(value);
}

inline u_short_type host_to_network_short(u_short_type value)
{
  return htons(value);
}

} // namespace socket_ops
} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // ASIO_DETAIL_SOCKET_OPS_HPP
