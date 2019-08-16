//
// detail/linux_io_uring_socket_service_base.ipp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2019 Christopher M. Kohlhoff (chris at kohlhoff dot com)
// Copyright (c) 2019 George Shramov (goxash at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_IMPL_LINUX_IO_URING_SOCKET_SERVICE_BASE_IPP
#define ASIO_DETAIL_IMPL_LINUX_IO_URING_SOCKET_SERVICE_BASE_IPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"

#if defined(ASIO_HAS_IO_URING)

#include "asio/detail/linux_io_uring_socket_service_base.hpp"

#include "asio/detail/push_options.hpp"

namespace asio {
namespace detail {

linux_io_uring_socket_service_base::linux_io_uring_socket_service_base(
    execution_context& context)
  : io_uring_manager_(use_service<linux_io_uring_manager>(context))
{
  io_uring_manager_.init_task();
}

void linux_io_uring_socket_service_base::base_shutdown()
{
}

void linux_io_uring_socket_service_base::construct(
    linux_io_uring_socket_service_base::base_implementation_type& impl)
{
  impl.socket_ = invalid_socket;
  impl.state_ = 0;
}

void linux_io_uring_socket_service_base::base_move_construct(
    linux_io_uring_socket_service_base::base_implementation_type& impl,
    linux_io_uring_socket_service_base::base_implementation_type& other_impl)
{
  impl.socket_ = other_impl.socket_;
  other_impl.socket_ = invalid_socket;

  impl.state_ = other_impl.state_;
  other_impl.state_ = 0;
}

void linux_io_uring_socket_service_base::base_move_assign(
    linux_io_uring_socket_service_base::base_implementation_type& impl,
    linux_io_uring_socket_service_base& other_service,
    linux_io_uring_socket_service_base::base_implementation_type& other_impl)
{
  destroy(impl);

  impl.socket_ = other_impl.socket_;
  other_impl.socket_ = invalid_socket;

  impl.state_ = other_impl.state_;
  other_impl.state_ = 0;
}

void linux_io_uring_socket_service_base::destroy(
    linux_io_uring_socket_service_base::base_implementation_type& impl)
{
  if (impl.socket_ != invalid_socket)
  {
    ASIO_HANDLER_OPERATION((io_uring_manager_.context(),
          "socket", &impl, impl.socket_, "close"));

    asio::error_code ignored_ec;
    socket_ops::close(impl.socket_, impl.state_, true, ignored_ec);
  }
}

asio::error_code linux_io_uring_socket_service_base::close(
    linux_io_uring_socket_service_base::base_implementation_type& impl,
    asio::error_code& ec)
{
  if (is_open(impl))
  {
    ASIO_HANDLER_OPERATION((io_uring_manager_.context(),
          "socket", &impl, impl.socket_, "close"));

    socket_ops::close(impl.socket_, impl.state_, false, ec);
  }
  else
  {
    ec = asio::error_code();
  }

  // Add some workaround here because io_uring does not cancel operations
  // after descriptor closed

  // The descriptor is closed by the OS even if close() returns an error.
  //
  // (Actually, POSIX says the state of the descriptor is unspecified. On
  // Linux the descriptor is apparently closed anyway; e.g. see
  //   http://lkml.org/lkml/2005/9/10/129
  // We'll just have to assume that other OSes follow the same behaviour. The
  // known exception is when Windows's closesocket() function fails with
  // WSAEWOULDBLOCK, but this case is handled inside socket_ops::close().
  construct(impl);

  return ec;
}

socket_type linux_io_uring_socket_service_base::release(
    linux_io_uring_socket_service_base::base_implementation_type& impl,
    asio::error_code& ec)
{
  if (!is_open(impl))
  {
    ec = asio::error::bad_descriptor;
    return invalid_socket;
  }

  ASIO_HANDLER_OPERATION((io_uring_manager_.context(),
        "socket", &impl, impl.socket_, "release"));

  socket_type sock = impl.socket_;
  construct(impl);
  ec = asio::error_code();
  return sock;
}

asio::error_code linux_io_uring_socket_service_base::cancel(
    linux_io_uring_socket_service_base::base_implementation_type& impl,
    asio::error_code& ec)
{
  if (!is_open(impl))
  {
    ec = asio::error::bad_descriptor;
    return ec;
  }

  ASIO_HANDLER_OPERATION((io_uring_manager_.context(),
        "socket", &impl, impl.socket_, "cancel"));

  ec = asio::error::operation_not_supported;
  return ec;
}

asio::error_code linux_io_uring_socket_service_base::do_open(
    linux_io_uring_socket_service_base::base_implementation_type& impl,
    int af, int type, int protocol, asio::error_code& ec)
{
  if (is_open(impl))
  {
    ec = asio::error::already_open;
    return ec;
  }

  socket_holder sock(socket_ops::socket(af, type, protocol, ec));
  if (sock.get() == invalid_socket)
    return ec;

  impl.socket_ = sock.release();
  switch (type)
  {
  case SOCK_STREAM: impl.state_ = socket_ops::stream_oriented; break;
  case SOCK_DGRAM: impl.state_ = socket_ops::datagram_oriented; break;
  default: impl.state_ = 0; break;
  }
  ec = asio::error_code();
  return ec;
}

asio::error_code linux_io_uring_socket_service_base::do_assign(
    linux_io_uring_socket_service_base::base_implementation_type& impl,
    int type,
    const linux_io_uring_socket_service_base::native_handle_type& native_socket,
    asio::error_code& ec)
{
  if (is_open(impl))
  {
    ec = asio::error::already_open;
    return ec;
  }

  impl.socket_ = native_socket;
  switch (type)
  {
  case SOCK_STREAM: impl.state_ = socket_ops::stream_oriented; break;
  case SOCK_DGRAM: impl.state_ = socket_ops::datagram_oriented; break;
  default: impl.state_ = 0; break;
  }
  impl.state_ |= socket_ops::possible_dup;
  ec = asio::error_code();
  return ec;
}

void linux_io_uring_socket_service_base::start_accept_op(
    linux_io_uring_operation* op, bool is_continuation, bool peer_is_open)
{
  if (!peer_is_open)
    io_uring_manager_.start_op(op, is_continuation);
  else
  {
    op->ec_ = asio::error::already_open;
    io_uring_manager_.post_immediate_completion(op, is_continuation);
  }
}

void linux_io_uring_socket_service_base::start_connect_op(
    linux_io_uring_socket_service_base::base_implementation_type& impl,
    linux_io_uring_operation* op, bool is_continuation,
    const socket_addr_type* addr, size_t addrlen)
{
  if ((impl.state_ & socket_ops::non_blocking)
      || socket_ops::set_internal_non_blocking(
        impl.socket_, impl.state_, true, op->ec_))
  {
    if (socket_ops::connect(impl.socket_, addr, addrlen, op->ec_) != 0)
    {
      if (op->ec_ == asio::error::in_progress
          || op->ec_ == asio::error::would_block)
      {
        op->ec_ = asio::error_code();
        io_uring_manager_.start_op(op, is_continuation);
        return;
      }
    }
  }

  io_uring_manager_.post_immediate_completion(op, is_continuation);
}

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // defined(ASIO_HAS_IO_URING)

#endif // ASIO_DETAIL_IMPL_LINUX_IO_URING_SOCKET_SERVICE_BASE_IPP
