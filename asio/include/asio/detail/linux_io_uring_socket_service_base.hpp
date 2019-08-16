//
// detail/linux_io_uring_socket_service_base.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2019 Christopher M. Kohlhoff (chris at kohlhoff dot com)
// Copyright (c) 2019 George Shramov (goxash at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_LINUX_IO_URING_SOCKET_SERVICE_BASE_HPP
#define ASIO_DETAIL_LINUX_IO_URING_SOCKET_SERVICE_BASE_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"

#if defined(ASIO_HAS_IO_URING)

#include "asio/buffer.hpp"
#include "asio/error.hpp"
#include "asio/execution_context.hpp"
#include "asio/socket_base.hpp"
#include "asio/detail/buffer_sequence_adapter.hpp"
#include "asio/detail/linux_io_uring_connect_op.hpp"
#include "asio/detail/linux_io_uring_manager.hpp"
#include "asio/detail/linux_io_uring_null_buffers_op.hpp"
#include "asio/detail/linux_io_uring_recv_op.hpp"
#include "asio/detail/linux_io_uring_recvmsg_op.hpp"
#include "asio/detail/linux_io_uring_send_op.hpp"
#include "asio/detail/linux_io_uring_wait_op.hpp"
#include "asio/detail/memory.hpp"
#include "asio/detail/socket_holder.hpp"
#include "asio/detail/socket_ops.hpp"
#include "asio/detail/socket_types.hpp"

#include "asio/detail/push_options.hpp"

namespace asio {
namespace detail {

class linux_io_uring_socket_service_base
{
public:
  // The native type of a socket.
  typedef socket_type native_handle_type;

  // The implementation type of the socket.
  struct base_implementation_type
  {
    // The native socket representation.
    socket_type socket_;

    // The current state of the socket.
    socket_ops::state_type state_;
  };

  // Constructor.
  ASIO_DECL linux_io_uring_socket_service_base(execution_context& context);

  // Destroy all user-defined handler objects owned by the service.
  ASIO_DECL void base_shutdown();

  // Construct a new socket implementation.
  ASIO_DECL void construct(base_implementation_type& impl);

  // Move-construct a new socket implementation.
  ASIO_DECL void base_move_construct(base_implementation_type& impl,
      base_implementation_type& other_impl);

  // Move-assign from another socket implementation.
  ASIO_DECL void base_move_assign(base_implementation_type& impl,
      linux_io_uring_socket_service_base& other_service,
      base_implementation_type& other_impl);

  // Destroy a socket implementation.
  ASIO_DECL void destroy(base_implementation_type& impl);

  // Determine whether the socket is open.
  bool is_open(const base_implementation_type& impl) const
  {
    return impl.socket_ != invalid_socket;
  }

  // Destroy a socket implementation.
  ASIO_DECL asio::error_code close(
      base_implementation_type& impl, asio::error_code& ec);

  // Release ownership of the socket.
  ASIO_DECL socket_type release(
      base_implementation_type& impl, asio::error_code& ec);

  // Get the native socket representation.
  native_handle_type native_handle(base_implementation_type& impl)
  {
    return impl.socket_;
  }

  // Cancel all operations associated with the socket.
  ASIO_DECL asio::error_code cancel(
      base_implementation_type& impl, asio::error_code& ec);

  // Determine whether the socket is at the out-of-band data mark.
  bool at_mark(const base_implementation_type& impl,
      asio::error_code& ec) const
  {
    return socket_ops::sockatmark(impl.socket_, ec);
  }

  // Determine the number of bytes available for reading.
  std::size_t available(const base_implementation_type& impl,
      asio::error_code& ec) const
  {
    return socket_ops::available(impl.socket_, ec);
  }

  // Place the socket into the state where it will listen for new connections.
  asio::error_code listen(base_implementation_type& impl,
      int backlog, asio::error_code& ec)
  {
    socket_ops::listen(impl.socket_, backlog, ec);
    return ec;
  }

  // Perform an IO control command on the socket.
  template <typename IO_Control_Command>
  asio::error_code io_control(base_implementation_type& impl,
      IO_Control_Command& command, asio::error_code& ec)
  {
    socket_ops::ioctl(impl.socket_, impl.state_, command.name(),
        static_cast<ioctl_arg_type*>(command.data()), ec);
    return ec;
  }

  // Gets the non-blocking mode of the socket.
  bool non_blocking(const base_implementation_type& impl) const
  {
    return (impl.state_ & socket_ops::user_set_non_blocking) != 0;
  }

  // Sets the non-blocking mode of the socket.
  asio::error_code non_blocking(base_implementation_type& impl,
      bool mode, asio::error_code& ec)
  {
    socket_ops::set_user_non_blocking(impl.socket_, impl.state_, mode, ec);
    return ec;
  }

  // Gets the non-blocking mode of the native socket implementation.
  bool native_non_blocking(const base_implementation_type& impl) const
  {
    return (impl.state_ & socket_ops::internal_non_blocking) != 0;
  }

  // Sets the non-blocking mode of the native socket implementation.
  asio::error_code native_non_blocking(base_implementation_type& impl,
      bool mode, asio::error_code& ec)
  {
    socket_ops::set_internal_non_blocking(impl.socket_, impl.state_, mode, ec);
    return ec;
  }

  // Wait for the socket to become ready to read, ready to write, or to have
  // pending error conditions.
  asio::error_code wait(base_implementation_type& impl,
      socket_base::wait_type w, asio::error_code& ec)
  {
    switch (w)
    {
    case socket_base::wait_read:
      socket_ops::poll_read(impl.socket_, impl.state_, -1, ec);
      break;
    case socket_base::wait_write:
      socket_ops::poll_write(impl.socket_, impl.state_, -1, ec);
      break;
    case socket_base::wait_error:
      socket_ops::poll_error(impl.socket_, impl.state_, -1, ec);
      break;
    default:
      ec = asio::error::invalid_argument;
      break;
    }

    return ec;
  }

  // Asynchronously wait for the socket to become ready to read, ready to
  // write, or to have pending error conditions.
  template <typename Handler, typename IoExecutor>
  void async_wait(base_implementation_type& impl,
      socket_base::wait_type w, Handler& handler, const IoExecutor& io_ex)
  {
    bool is_continuation =
      asio_handler_cont_helpers::is_continuation(handler);

    short poll_events;
    switch (w)
    {
    case socket_base::wait_read:
      poll_events = EPOLLIN;
      break;
    case socket_base::wait_write:
      poll_events = EPOLLOUT;
      break;
    case socket_base::wait_error:
      poll_events = EPOLLPRI;
      break;
    default:
      poll_events = 0;
      break;
    }

    // Allocate and construct an operation to wrap the handler.
    typedef linux_io_uring_wait_op<Handler, IoExecutor> op;
    typename op::ptr p = { asio::detail::addressof(handler),
      op::ptr::allocate(handler), 0 };
    p.p = new (p.v) op(impl.socket_, poll_events, handler, io_ex);

    ASIO_HANDLER_CREATION((io_uring_manager_.context(), *p.p, "socket",
          &impl, impl.socket_, "async_wait"));

    if (poll_events == 0)
    {
      p.p->ec_ = asio::error::invalid_argument;
      io_uring_manager_.post_immediate_completion(p.p, is_continuation);
    }
    else
      io_uring_manager_.start_op(p.p, is_continuation);
    p.v = p.p = 0;
  }

  // Send the given data to the peer.
  template <typename ConstBufferSequence>
  size_t send(base_implementation_type& impl,
      const ConstBufferSequence& buffers,
      socket_base::message_flags flags, asio::error_code& ec)
  {
    buffer_sequence_adapter<asio::const_buffer,
        ConstBufferSequence> bufs(buffers);

    return socket_ops::sync_send(impl.socket_, impl.state_,
        bufs.buffers(), bufs.count(), flags, bufs.all_empty(), ec);
  }

  // Wait until data can be sent without blocking.
  size_t send(base_implementation_type& impl, const null_buffers&,
      socket_base::message_flags, asio::error_code& ec)
  {
    // Wait for socket to become ready.
    socket_ops::poll_write(impl.socket_, impl.state_, -1, ec);

    return 0;
  }

  // Start an asynchronous send. The data being sent must be valid for the
  // lifetime of the asynchronous operation.
  template <typename ConstBufferSequence, typename Handler, typename IoExecutor>
  void async_send(base_implementation_type& impl,
      const ConstBufferSequence& buffers, socket_base::message_flags flags,
      Handler& handler, const IoExecutor& io_ex)
  {
    bool is_continuation =
      asio_handler_cont_helpers::is_continuation(handler);

    // Allocate and construct an operation to wrap the handler.
    typedef linux_io_uring_send_op<
      ConstBufferSequence, Handler, IoExecutor> op;
    typename op::ptr p = { asio::detail::addressof(handler),
      op::ptr::allocate(handler), 0 };
    p.p = new (p.v) op(impl.socket_, impl.state_, buffers, flags,
      handler, io_ex);

    ASIO_HANDLER_CREATION((io_uring_manager_.context(), *p.p, "socket",
          &impl, impl.socket_, "async_send"));

    if ((impl.state_ & socket_ops::stream_oriented)
          && buffer_sequence_adapter<asio::const_buffer,
            ConstBufferSequence>::all_empty(buffers))
      io_uring_manager_.post_immediate_completion(p.p, is_continuation);
    else
      io_uring_manager_.start_op(p.p, is_continuation);
    p.v = p.p = 0;
  }

  // Start an asynchronous wait until data can be sent without blocking.
  template <typename Handler, typename IoExecutor>
  void async_send(base_implementation_type& impl, const null_buffers&,
      socket_base::message_flags, Handler& handler, const IoExecutor& io_ex)
  {
    bool is_continuation =
      asio_handler_cont_helpers::is_continuation(handler);

    // Allocate and construct an operation to wrap the handler.
    typedef linux_io_uring_null_buffers_op<Handler, IoExecutor> op;
    typename op::ptr p = { asio::detail::addressof(handler),
      op::ptr::allocate(handler), 0 };
    p.p = new (p.v) op(impl.socket_, EPOLLOUT, handler, io_ex);

    ASIO_HANDLER_CREATION((io_uring_manager_.context(), *p.p, "socket",
          &impl, impl.socket_, "async_send(null_buffers)"));

    io_uring_manager_.start_op(p.p, is_continuation);
    p.v = p.p = 0;
  }

  // Receive some data from the peer. Returns the number of bytes received.
  template <typename MutableBufferSequence>
  size_t receive(base_implementation_type& impl,
      const MutableBufferSequence& buffers,
      socket_base::message_flags flags, asio::error_code& ec)
  {
    buffer_sequence_adapter<asio::mutable_buffer,
        MutableBufferSequence> bufs(buffers);

    return socket_ops::sync_recv(impl.socket_, impl.state_,
        bufs.buffers(), bufs.count(), flags, bufs.all_empty(), ec);
  }

  // Wait until data can be received without blocking.
  size_t receive(base_implementation_type& impl, const null_buffers&,
      socket_base::message_flags, asio::error_code& ec)
  {
    // Wait for socket to become ready.
    socket_ops::poll_read(impl.socket_, impl.state_, -1, ec);

    return 0;
  }

  // Start an asynchronous receive. The buffer for the data being received
  // must be valid for the lifetime of the asynchronous operation.
  template <typename MutableBufferSequence,
      typename Handler, typename IoExecutor>
  void async_receive(base_implementation_type& impl,
      const MutableBufferSequence& buffers, socket_base::message_flags flags,
      Handler& handler, const IoExecutor& io_ex)
  {
    bool is_continuation =
      asio_handler_cont_helpers::is_continuation(handler);

    // Allocate and construct an operation to wrap the handler.
    typedef linux_io_uring_recv_op<
        MutableBufferSequence, Handler, IoExecutor> op;
    typename op::ptr p = { asio::detail::addressof(handler),
      op::ptr::allocate(handler), 0 };
    p.p = new (p.v) op(impl.socket_, impl.state_, buffers, flags,
        handler, io_ex);

    ASIO_HANDLER_CREATION((io_uring_manager_.context(), *p.p, "socket",
          &impl, impl.socket_, "async_receive"));

    if ((impl.state_ & socket_ops::stream_oriented)
          && buffer_sequence_adapter<asio::mutable_buffer,
            MutableBufferSequence>::all_empty(buffers))
      io_uring_manager_.post_immediate_completion(p.p, is_continuation);
    else
      io_uring_manager_.start_op(p.p, is_continuation);
    p.v = p.p = 0;
  }

  // Wait until data can be received without blocking.
  template <typename Handler, typename IoExecutor>
  void async_receive(base_implementation_type& impl,
      const null_buffers&, socket_base::message_flags flags,
      Handler& handler, const IoExecutor& io_ex)
  {
    bool is_continuation =
      asio_handler_cont_helpers::is_continuation(handler);

    // Allocate and construct an operation to wrap the handler.
    typedef linux_io_uring_null_buffers_op<Handler, IoExecutor> op;
    typename op::ptr p = { asio::detail::addressof(handler),
      op::ptr::allocate(handler), 0 };
    p.p = new (p.v) op(impl.socket_,
      (flags & socket_base::message_out_of_band) ? EPOLLPRI : EPOLLIN,
      handler, io_ex);

    ASIO_HANDLER_CREATION((io_uring_manager_.context(), *p.p, "socket",
          &impl, impl.socket_, "async_receive(null_buffers)"));

    io_uring_manager_.start_op(p.p, is_continuation);
    p.v = p.p = 0;
  }

  // Receive some data with associated flags. Returns the number of bytes
  // received.
  template <typename MutableBufferSequence>
  size_t receive_with_flags(base_implementation_type& impl,
      const MutableBufferSequence& buffers,
      socket_base::message_flags in_flags,
      socket_base::message_flags& out_flags, asio::error_code& ec)
  {
    buffer_sequence_adapter<asio::mutable_buffer,
        MutableBufferSequence> bufs(buffers);

    return socket_ops::sync_recvmsg(impl.socket_, impl.state_,
        bufs.buffers(), bufs.count(), in_flags, out_flags, ec);
  }

  // Wait until data can be received without blocking.
  size_t receive_with_flags(base_implementation_type& impl,
      const null_buffers&, socket_base::message_flags,
      socket_base::message_flags& out_flags, asio::error_code& ec)
  {
    // Wait for socket to become ready.
    socket_ops::poll_read(impl.socket_, impl.state_, -1, ec);

    // Clear out_flags, since we cannot give it any other sensible value when
    // performing a null_buffers operation.
    out_flags = 0;

    return 0;
  }

  // Start an asynchronous receive. The buffer for the data being received
  // must be valid for the lifetime of the asynchronous operation.
  template <typename MutableBufferSequence,
      typename Handler, typename IoExecutor>
  void async_receive_with_flags(base_implementation_type& impl,
      const MutableBufferSequence& buffers, socket_base::message_flags in_flags,
      socket_base::message_flags& out_flags, Handler& handler,
      const IoExecutor& io_ex)
  {
    bool is_continuation =
      asio_handler_cont_helpers::is_continuation(handler);

    // Allocate and construct an operation to wrap the handler.
    typedef linux_io_uring_recvmsg_op<
        MutableBufferSequence, Handler, IoExecutor> op;
    typename op::ptr p = { asio::detail::addressof(handler),
      op::ptr::allocate(handler), 0 };
    p.p = new (p.v) op(impl.socket_, buffers, in_flags, out_flags,
        handler, io_ex);

    ASIO_HANDLER_CREATION((io_uring_manager_.context(), *p.p, "socket",
          &impl, impl.socket_, "async_receive_with_flags"));

    io_uring_manager_.start_op(p.p, is_continuation);
    p.v = p.p = 0;
  }

  // Wait until data can be received without blocking.
  template <typename Handler, typename IoExecutor>
  void async_receive_with_flags(base_implementation_type& impl,
      const null_buffers&, socket_base::message_flags in_flags,
      socket_base::message_flags& out_flags, Handler& handler,
      const IoExecutor& io_ex)
  {
    bool is_continuation =
      asio_handler_cont_helpers::is_continuation(handler);

    // Allocate and construct an operation to wrap the handler.
    typedef linux_io_uring_null_buffers_op<Handler, IoExecutor> op;
    typename op::ptr p = { asio::detail::addressof(handler),
      op::ptr::allocate(handler), 0 };
    p.p = new (p.v) op(impl.socket_,
      (in_flags & socket_base::message_out_of_band) ? EPOLLPRI : EPOLLIN,
      handler, io_ex);

    ASIO_HANDLER_CREATION((io_uring_manager_.context(), *p.p, "socket",
          &impl, impl.socket_, "async_receive_with_flags(null_buffers)"));

    // Clear out_flags, since we cannot give it any other sensible value when
    // performing a null_buffers operation.
    out_flags = 0;

    io_uring_manager_.start_op(p.p, is_continuation);
    p.v = p.p = 0;
  }

protected:
  // Open a new socket implementation.
  ASIO_DECL asio::error_code do_open(
      base_implementation_type& impl, int af,
      int type, int protocol, asio::error_code& ec);

  // Assign a native socket to a socket implementation.
  ASIO_DECL asio::error_code do_assign(
      base_implementation_type& impl, int type,
      const native_handle_type& native_socket, asio::error_code& ec);

  // Start the asynchronous accept operation.
  ASIO_DECL void start_accept_op(linux_io_uring_operation* op,
      bool is_continuation, bool peer_is_open);

  // Start the asynchronous connect operation.
  ASIO_DECL void start_connect_op(base_implementation_type& impl,
      linux_io_uring_operation* op, bool is_continuation,
      const socket_addr_type* addr, size_t addrlen);

  // The selector that performs event demultiplexing for the service.
  linux_io_uring_manager& io_uring_manager_;
};

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#if defined(ASIO_HEADER_ONLY)
# include "asio/detail/impl/linux_io_uring_socket_service_base.ipp"
#endif // defined(ASIO_HEADER_ONLY)

#endif // defined(ASIO_HAS_IO_URING)

#endif // ASIO_DETAIL_LINUX_IO_URING_SOCKET_SERVICE_BASE_HPP
