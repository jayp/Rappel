//
// openssl_operation.hpp
// ~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2005 Voipster / Indrek dot Juhani at voipster dot com
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_SSL_DETAIL_OPENSSL_OPERATION_HPP
#define ASIO_SSL_DETAIL_OPENSSL_OPERATION_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/push_options.hpp"

#include "asio/detail/push_options.hpp"
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include "asio/detail/pop_options.hpp"

#include "asio/buffer.hpp"
#include "asio/placeholders.hpp"
#include "asio/write.hpp"
#include "asio/ssl/detail/openssl_types.hpp"

namespace asio {
namespace ssl {
namespace detail {

typedef boost::function<int (::SSL*)> ssl_primitive_func; 
typedef boost::function<void (const error&, int)> user_handler_func;

// Network send_/recv buffer implementation
//
//
class net_buffer
{
  static const int  NET_BUF_SIZE = 16*1024 + 256; // SSL record size + spare

  unsigned char buf_[NET_BUF_SIZE];
  unsigned char* data_start_;
  unsigned char* data_end_;

public:
  net_buffer()
  {
    data_start_ = data_end_ = buf_;
  }
  unsigned char* get_unused_start() { return data_end_; }
  unsigned char* get_data_start() { return data_start_; }
  size_t get_unused_len() { return (NET_BUF_SIZE - (data_end_ - buf_)); }    
  size_t get_data_len() { return (data_end_ - data_start_); }    
  void data_added(size_t count)
  { 
    data_end_ += count; 
    data_end_ = data_end_ > (buf_ + NET_BUF_SIZE)? 
      (buf_ + NET_BUF_SIZE):
      data_end_; 
  }
  void data_removed(size_t count) 
  { 
    data_start_ += count; 
    if (data_start_ >= data_end_) reset(); 
  }
  void reset() { data_start_ = buf_; data_end_ = buf_; }               
  bool has_data() { return (data_start_ < data_end_); }
}; // class net_buffer

//
// Operation class
//
//
template <typename Stream>
class openssl_operation
{
public:

  // Constructor for asynchronous operations
  openssl_operation(ssl_primitive_func primitive,
                    Stream& socket,
                    SSL* session,
                    BIO* ssl_bio,
                    user_handler_func  handler
                    )
    : primitive_(primitive)
    , user_handler_(handler)
    , socket_(socket)
    , ssl_bio_(ssl_bio)
    , session_(session)
  {
    write_ = boost::bind(
      &openssl_operation::do_async_write, 
      this, boost::arg<1>(), boost::arg<2>()
    );
    handler_= boost::bind(
      &openssl_operation::async_user_handler, 
      this, boost::arg<1>(), boost::arg<2>()
    );
  }

  // Constructor for synchronous operations
  openssl_operation(ssl_primitive_func primitive,
                    Stream& socket,
                    SSL* session,
                    BIO* ssl_bio)
    : primitive_(primitive)
    , socket_(socket)
    , ssl_bio_(ssl_bio)
    , session_(session)
  {      
    write_ = boost::bind(
      &openssl_operation::do_sync_write, 
      this, boost::arg<1>(), boost::arg<2>()
    );
    handler_ = boost::bind(
      &openssl_operation::sync_user_handler, 
      this, boost::arg<1>(), boost::arg<2>()
      );
  }

  // Start operation
  // In case of asynchronous it returns 0, in sync mode returns success code
  // or throws an error...
  int start()
  {
    int rc = primitive_( session_ );
    bool is_operation_done = (rc > 0);  
                // For connect/accept/shutdown, the operation
                // is done, when return code is 1
                // for write, it is done, when is retcode > 0
                // for read, is is done when retcode > 0

    int error_code =  !is_operation_done ?
          ::SSL_get_error( session_, rc ) :
          0;        
    bool is_read_needed = (error_code == SSL_ERROR_WANT_READ);
    bool is_write_needed = (error_code == SSL_ERROR_WANT_WRITE ||
                              ::BIO_ctrl_pending( ssl_bio_ ));
    bool is_shut_down_received = 
      ((::SSL_get_shutdown( session_ ) & SSL_RECEIVED_SHUTDOWN) == 
          SSL_RECEIVED_SHUTDOWN);
    bool is_shut_down_sent = 
      ((::SSL_get_shutdown( session_ ) & SSL_SENT_SHUTDOWN) ==
            SSL_SENT_SHUTDOWN);

    if (is_shut_down_sent && is_shut_down_received && is_operation_done)
      // SSL connection is shut down cleanly
      return handler_(asio::error(), 1);

    if (is_shut_down_received && !is_write_needed)
      return handler_(asio::error(asio::error::eof), 0);

    if (is_shut_down_received)
      // Shutdown has been requested, while we were reading or writing...
      // abort our action...
      return handler_(asio::error(asio::error::shut_down), 0);

    if (!is_operation_done && !is_read_needed && !is_write_needed 
      && !is_shut_down_sent)
      // The operation has failed... It is not completed and does 
      // not want network communication nor does want to send shutdown out...
      return handler_(asio::error(error_code), rc); 

    // Continue with operation, flush any SSL data out to network...
    return write_(is_operation_done, rc); 
  }

// Private implementation
private:
  typedef boost::function<int (const asio::error&, int)>
    int_handler_func;
  typedef boost::function<int (bool, int)> write_func;

  ssl_primitive_func  primitive_;
  user_handler_func  user_handler_;
  write_func  write_;
  int_handler_func handler_;
    
  net_buffer send_buf_; // buffers for network IO
  net_buffer recv_buf_; 

  Stream& socket_;
  BIO*    ssl_bio_;
  SSL*    session_;

  //
  int sync_user_handler(const asio::error& error, int rc)
  {
    if (!error)
      return rc;

    throw error;
  }
    
  int async_user_handler(const asio::error& error, int rc)
  {
    user_handler_(error, rc);
    return 0;
  }

  // Writes bytes asynchronously from SSL to NET
  int  do_async_write(bool is_operation_done, int rc) 
  {
    int len = ::BIO_ctrl_pending( ssl_bio_ );
    if ( len )
    { 
      // There is something to write into net, do it...
      len = (int)send_buf_.get_unused_len() > len? 
        len: 
        send_buf_.get_unused_len();
        
      if (len == 0)
        // In case our send buffer is full, we have just to wait until 
        // previous send to complete...
        return 0;

      // Read outgoing data from bio
      len = ::BIO_read( ssl_bio_, send_buf_.get_unused_start(), len); 
         
      if (len > 0)
      {
        unsigned char *data_start = send_buf_.get_unused_start();
        send_buf_.data_added(len);
   
        asio::async_write
        ( 
          socket_, 
          asio::buffer(data_start, len),
          boost::bind
          (
            &openssl_operation::async_write_handler, 
            this, 
            is_operation_done,
            rc, 
            asio::placeholders::error, 
            asio::placeholders::bytes_transferred
          )
        );
                  
        return 0;
      }
      else if (!BIO_should_retry(ssl_bio_))
      {
        // Seems like fatal error
        // reading from SSL BIO has failed...
        handler_(asio::error(asio::error::no_recovery), 0);
        return 0;
      }
    }
    
    if (is_operation_done)
    {
      // Finish the operation, with success
      handler_(asio::error(), rc);
      return 0;
    }
    
    // OPeration is not done and writing to net has been made...
    // start reading...
    do_async_read();
          
    return 0;
  }

  void async_write_handler(bool is_operation_done, int rc, 
    const asio::error& error, size_t bytes_sent)
  {
    if (!error)
    {
      // Remove data from send buffer
      send_buf_.data_removed(bytes_sent);

      if (is_operation_done)
        handler_(asio::error(), rc);
      else
        // Since the operation was not completed, try it again...
        start();
    }
    else 
      handler_(error, rc);
  }

  void do_async_read()
  {
    // Wait for new data
    socket_.async_read_some
    ( 
      asio::buffer(recv_buf_.get_unused_start(),
        recv_buf_.get_unused_len()),
      boost::bind
      (
        &openssl_operation::async_read_handler, 
        this, 
        asio::placeholders::error, 
        asio::placeholders::bytes_transferred
      ) 
    );
  }

  void async_read_handler(const asio::error& error, size_t bytes_recvd)
  {
    if (!error)
    {
      recv_buf_.data_added(bytes_recvd);

      // Pass the received data to SSL
      int written = ::BIO_write
      ( 
        ssl_bio_, 
        recv_buf_.get_data_start(), 
        recv_buf_.get_data_len() 
      );

      if (written > 0)
      {
        recv_buf_.data_removed(written);
      }
      else if (written < 0)
      {
        if (!BIO_should_retry(ssl_bio_))
        {
          // Some serios error with BIO....
          handler_(asio::error(asio::error::no_recovery), 0);
          return;
        }
      }

      // and try the SSL primitive again
      start();
    }
    else
    {
      // Error in network level...
      // SSL can't continue either...
      handler_(error, 0);
    }
  }

  // Syncronous functions...
  int do_sync_write(bool is_operation_done, int rc)
  {
    int len = ::BIO_ctrl_pending( ssl_bio_ );
    if ( len )
    { 
      // There is something to write into net, do it...
      len = (int)send_buf_.get_unused_len() > len? 
        len: 
        send_buf_.get_unused_len();
        
      // Read outgoing data from bio
      len = ::BIO_read( ssl_bio_, send_buf_.get_unused_start(), len); 
         
      if (len > 0)
      {
        size_t sent_len = asio::write( 
                  socket_, 
                  asio::buffer(send_buf_.get_unused_start(), len)
                  );

        send_buf_.data_added(len);
        send_buf_.data_removed(sent_len);
      }          
      else if (!BIO_should_retry(ssl_bio_))
      {
        // Seems like fatal error
        // reading from SSL BIO has failed...
        throw asio::error(asio::error::no_recovery);
      }
    }
    
    if (is_operation_done)
      // Finish the operation, with success
      return rc;
                
    // Operation is not finished, read data from net...
    return do_sync_read();
  }

  int do_sync_read()
  {
    size_t len = socket_.read_some
      ( 
        asio::buffer(recv_buf_.get_unused_start(),
          recv_buf_.get_unused_len())
      );

    // Write data to ssl
    recv_buf_.data_added(len);

    // Pass the received data to SSL
    int written = ::BIO_write
    ( 
      ssl_bio_, 
      recv_buf_.get_data_start(), 
      recv_buf_.get_data_len() 
    );

    if (written > 0)
    {
      recv_buf_.data_removed(written);
    }
    else if (written < 0)
    {
      if (!BIO_should_retry(ssl_bio_))
      {
        // Some serios error with BIO....
        throw asio::error(asio::error::no_recovery);
      }
    }

    // Try the operation again
    return start();
  }
}; // class openssl_operation

} // namespace detail
} // namespace ssl
} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // ASIO_SSL_DETAIL_OPENSSL_OPERATION_HPP
