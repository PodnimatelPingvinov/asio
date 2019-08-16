//
// detail/linux_io_uring_descriptor_service.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2019 Christopher M. Kohlhoff (chris at kohlhoff dot com)
// Copyright (c) 2019 George Shramov (goxash at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_LINUX_IO_URING_DESCRIPTOR_SERVICE_HPP
#define ASIO_DETAIL_LINUX_IO_URING_DESCRIPTOR_SERVICE_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"

#if defined(ASIO_HAS_IO_URING)

#include "asio/buffer.hpp"
#include "asio/execution_context.hpp"
#include "asio/detail/bind_handler.hpp"
#include "asio/detail/buffer_sequence_adapter.hpp"
#include "asio/detail/fenced_block.hpp"
#include "asio/detail/linux_io_uring_manager.hpp"
#include "asio/detail/linux_io_uring_null_buffers_op.hpp"
#include "asio/detail/linux_io_uring_read_op.hpp"
#include "asio/detail/linux_io_uring_wait_op.hpp"
#include "asio/detail/linux_io_uring_write_op.hpp"
#include "asio/detail/memory.hpp"
#include "asio/detail/noncopyable.hpp"
#include "asio/detail/descriptor_ops.hpp"
#include "asio/posix/descriptor_base.hpp"

#include "asio/detail/push_options.hpp"

namespace asio {
namespace detail {

class linux_io_uring_descriptor_service :
  public execution_context_service_base<linux_io_uring_descriptor_service>
{
public:
  // The native type of a descriptor.
  typedef int native_handle_type;

  // The implementation type of the descriptor.
  class implementation_type
    : private asio::detail::noncopyable
  {
  public:
    // Default constructor.
    implementation_type()
      : descriptor_(-1),
        state_(0)
    {
    }

  private:
    // Only this service will have access to the internal values.
    friend class linux_io_uring_descriptor_service;

    // The native descriptor representation.
    int descriptor_;

    // The current state of the descriptor.
    descriptor_ops::state_type state_;
  };

  // Constructor.
  ASIO_DECL linux_io_uring_descriptor_service(execution_context& context);

  // Destroy all user-defined handler objects owned by the service.
  ASIO_DECL void shutdown();

  // Construct a new descriptor implementation.
  ASIO_DECL void construct(implementation_type& impl);

  // Move-construct a new descriptor implementation.
  ASIO_DECL void move_construct(implementation_type& impl,
      implementation_type& other_impl);

  // Move-assign from another descriptor implementation.
  ASIO_DECL void move_assign(implementation_type& impl,
      linux_io_uring_descriptor_service& other_service,
      implementation_type& other_impl);

  // Destroy a descriptor implementation.
  ASIO_DECL void destroy(implementation_type& impl);

  // Assign a native descriptor to a descriptor implementation.
  ASIO_DECL asio::error_code assign(implementation_type& impl,
      const native_handle_type& native_descriptor,
      asio::error_code& ec);

  // Determine whether the descriptor is open.
  bool is_open(const implementation_type& impl) const
  {
    return impl.descriptor_ != -1;
  }

  // Destroy a descriptor implementation.
  ASIO_DECL asio::error_code close(implementation_type& impl,
      asio::error_code& ec);

  // Get the native descriptor representation.
  native_handle_type native_handle(const implementation_type& impl) const
  {
    return impl.descriptor_;
  }

  // Release ownership of the native descriptor representation.
  ASIO_DECL native_handle_type release(implementation_type& impl);

  // Cancel all operations associated with the descriptor.
  ASIO_DECL asio::error_code cancel(implementation_type& impl,
      asio::error_code& ec);

  // Perform an IO control command on the descriptor.
  template <typename IO_Control_Command>
  asio::error_code io_control(implementation_type& impl,
      IO_Control_Command& command, asio::error_code& ec)
  {
    descriptor_ops::ioctl(impl.descriptor_, impl.state_,
        command.name(), static_cast<ioctl_arg_type*>(command.data()), ec);
    return ec;
  }

  // Gets the non-blocking mode of the descriptor.
  bool non_blocking(const implementation_type& impl) const
  {
    return (impl.state_ & descriptor_ops::user_set_non_blocking) != 0;
  }

  // Sets the non-blocking mode of the descriptor.
  asio::error_code non_blocking(implementation_type& impl,
      bool mode, asio::error_code& ec)
  {
    descriptor_ops::set_user_non_blocking(
        impl.descriptor_, impl.state_, mode, ec);
    return ec;
  }

  // Gets the non-blocking mode of the native descriptor implementation.
  bool native_non_blocking(const implementation_type& impl) const
  {
    return (impl.state_ & descriptor_ops::internal_non_blocking) != 0;
  }

  // Sets the non-blocking mode of the native descriptor implementation.
  asio::error_code native_non_blocking(implementation_type& impl,
      bool mode, asio::error_code& ec)
  {
    descriptor_ops::set_internal_non_blocking(
        impl.descriptor_, impl.state_, mode, ec);
    return ec;
  }

  // Wait for the descriptor to become ready to read, ready to write, or to have
  // pending error conditions.
  asio::error_code wait(implementation_type& impl,
      posix::descriptor_base::wait_type w, asio::error_code& ec)
  {
    switch (w)
    {
    case posix::descriptor_base::wait_read:
      descriptor_ops::poll_read(impl.descriptor_, impl.state_, ec);
      break;
    case posix::descriptor_base::wait_write:
      descriptor_ops::poll_write(impl.descriptor_, impl.state_, ec);
      break;
    case posix::descriptor_base::wait_error:
      descriptor_ops::poll_error(impl.descriptor_, impl.state_, ec);
      break;
    default:
      ec = asio::error::invalid_argument;
      break;
    }

    return ec;
  }

  // Asynchronously wait for the descriptor to become ready to read, ready to
  // write, or to have pending error conditions.
  template <typename Handler, typename IoExecutor>
  void async_wait(implementation_type& impl,
      posix::descriptor_base::wait_type w,
      Handler& handler, const IoExecutor& io_ex)
  {
    bool is_continuation =
      asio_handler_cont_helpers::is_continuation(handler);

    short poll_events;
    switch (w)
    {
    case posix::descriptor_base::wait_read:
      poll_events = EPOLLIN;
      break;
    case posix::descriptor_base::wait_write:
      poll_events = EPOLLOUT;
      break;
    case posix::descriptor_base::wait_error:
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
    p.p = new (p.v) op(impl.descriptor_, poll_events, handler, io_ex);

    ASIO_HANDLER_CREATION((io_uring_manager_.context(), *p.p, "descriptor",
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

  // Write some data to the descriptor.
  template <typename ConstBufferSequence>
  size_t write_some(implementation_type& impl,
      const ConstBufferSequence& buffers, asio::error_code& ec)
  {
    buffer_sequence_adapter<asio::const_buffer,
        ConstBufferSequence> bufs(buffers);

    return descriptor_ops::sync_write(impl.descriptor_, impl.state_,
        bufs.buffers(), bufs.count(), bufs.all_empty(), ec);
  }

  // Wait until data can be written without blocking.
  size_t write_some(implementation_type& impl,
      const null_buffers&, asio::error_code& ec)
  {
    // Wait for descriptor to become ready.
    descriptor_ops::poll_write(impl.descriptor_, impl.state_, ec);

    return 0;
  }

  // Start an asynchronous write. The data being sent must be valid for the
  // lifetime of the asynchronous operation.
  template <typename ConstBufferSequence, typename Handler, typename IoExecutor>
  void async_write_some(implementation_type& impl,
      const ConstBufferSequence& buffers, Handler& handler,
      const IoExecutor& io_ex)
  {
    bool is_continuation =
      asio_handler_cont_helpers::is_continuation(handler);

    // Allocate and construct an operation to wrap the handler.
    typedef linux_io_uring_write_op<
      ConstBufferSequence, Handler, IoExecutor> op;
    typename op::ptr p = { asio::detail::addressof(handler),
      op::ptr::allocate(handler), 0 };
    p.p = new (p.v) op(impl.descriptor_, buffers, handler, io_ex);

    ASIO_HANDLER_CREATION((io_uring_manager_.context(), *p.p, "descriptor",
          &impl, impl.socket_, "async_write_some"));

    if (buffer_sequence_adapter<asio::const_buffer,
        ConstBufferSequence>::all_empty(buffers))
      io_uring_manager_.post_immediate_completion(p.p, is_continuation);
    else
      io_uring_manager_.start_op(p.p, is_continuation);
    p.v = p.p = 0;
  }

  // Start an asynchronous wait until data can be written without blocking.
  template <typename Handler, typename IoExecutor>
  void async_write_some(implementation_type& impl,
      const null_buffers&, Handler& handler, const IoExecutor& io_ex)
  {
    bool is_continuation =
      asio_handler_cont_helpers::is_continuation(handler);

    // Allocate and construct an operation to wrap the handler.
    typedef linux_io_uring_null_buffers_op<Handler, IoExecutor> op;
    typename op::ptr p = { asio::detail::addressof(handler),
      op::ptr::allocate(handler), 0 };
    p.p = new (p.v) op(impl.descriptor_, EPOLLOUT, handler, io_ex);

    ASIO_HANDLER_CREATION((io_uring_manager_.context(), *p.p, "descriptor",
          &impl, impl.socket_, "async_write_some(null_buffers)"));

    io_uring_manager_.start_op(p.p, is_continuation);
    p.v = p.p = 0;
  }

  // Read some data from the stream. Returns the number of bytes read.
  template <typename MutableBufferSequence>
  size_t read_some(implementation_type& impl,
      const MutableBufferSequence& buffers, asio::error_code& ec)
  {
    buffer_sequence_adapter<asio::mutable_buffer,
        MutableBufferSequence> bufs(buffers);

    return descriptor_ops::sync_read(impl.descriptor_, impl.state_,
        bufs.buffers(), bufs.count(), bufs.all_empty(), ec);
  }

  // Wait until data can be read without blocking.
  size_t read_some(implementation_type& impl,
      const null_buffers&, asio::error_code& ec)
  {
    // Wait for descriptor to become ready.
    descriptor_ops::poll_read(impl.descriptor_, impl.state_, ec);

    return 0;
  }

  // Start an asynchronous read. The buffer for the data being read must be
  // valid for the lifetime of the asynchronous operation.
  template <typename MutableBufferSequence,
      typename Handler, typename IoExecutor>
  void async_read_some(implementation_type& impl,
      const MutableBufferSequence& buffers,
      Handler& handler, const IoExecutor& io_ex)
  {
    bool is_continuation =
      asio_handler_cont_helpers::is_continuation(handler);

    // Allocate and construct an operation to wrap the handler.
    typedef linux_io_uring_read_op<
        MutableBufferSequence, Handler, IoExecutor> op;
    typename op::ptr p = { asio::detail::addressof(handler),
      op::ptr::allocate(handler), 0 };
    p.p = new (p.v) op(impl.descriptor_, buffers, handler, io_ex);

    ASIO_HANDLER_CREATION((io_uring_manager_.context(), *p.p, "descriptor",
          &impl, impl.socket_, "async_read_some"));

    if (buffer_sequence_adapter<asio::mutable_buffer,
        MutableBufferSequence>::all_empty(buffers))
      io_uring_manager_.post_immediate_completion(p.p, is_continuation);
    else
      io_uring_manager_.start_op(p.p, is_continuation);
    p.v = p.p = 0;
  }

  // Wait until data can be read without blocking.
  template <typename Handler, typename IoExecutor>
  void async_read_some(implementation_type& impl,
      const null_buffers&, Handler& handler, const IoExecutor& io_ex)
  {
    bool is_continuation =
      asio_handler_cont_helpers::is_continuation(handler);

    // Allocate and construct an operation to wrap the handler.
    typedef linux_io_uring_null_buffers_op<Handler, IoExecutor> op;
    typename op::ptr p = { asio::detail::addressof(handler),
      op::ptr::allocate(handler), 0 };
    p.p = new (p.v) op(impl.descriptor_, EPOLLIN, handler, io_ex);

    ASIO_HANDLER_CREATION((io_uring_manager_.context(), *p.p, "descriptor",
          &impl, impl.socket_, "async_read_some(null_buffers)"));

    io_uring_manager_.start_op(p.p, is_continuation);
    p.v = p.p = 0;
  }

private:
  // The selector that performs event demultiplexing for the service.
  linux_io_uring_manager& io_uring_manager_;
};

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#if defined(ASIO_HEADER_ONLY)
# include "asio/detail/impl/linux_io_uring_descriptor_service.ipp"
#endif // defined(ASIO_HEADER_ONLY)

#endif // defined(ASIO_HAS_IO_URING)

#endif // ASIO_DETAIL_LINUX_IO_URING_DESCRIPTOR_SERVICE_HPP
