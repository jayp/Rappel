//
// fd_set_adapter.hpp
// ~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2006 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_FD_SET_ADAPTER_HPP
#define ASIO_DETAIL_FD_SET_ADAPTER_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/push_options.hpp"

#include "asio/detail/socket_types.hpp"

namespace asio {
namespace detail {

// Adapts the FD_SET type to meet the Descriptor_Set concept's requirements.
class fd_set_adapter
{
public:
  fd_set_adapter()
    : max_descriptor_(invalid_socket)
  {
    FD_ZERO(&fd_set_);
  }

  void set(socket_type descriptor)
  {
    if (max_descriptor_ == invalid_socket || descriptor > max_descriptor_)
      max_descriptor_ = descriptor;
    FD_SET(descriptor, &fd_set_);
  }

  bool is_set(socket_type descriptor) const
  {
    return FD_ISSET(descriptor, &fd_set_) != 0;
  }

  operator fd_set*()
  {
    return &fd_set_;
  }

  socket_type max_descriptor() const
  {
    return max_descriptor_;
  }

private:
  fd_set fd_set_;
  socket_type max_descriptor_;
};

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // ASIO_DETAIL_FD_SET_ADAPTER_HPP
