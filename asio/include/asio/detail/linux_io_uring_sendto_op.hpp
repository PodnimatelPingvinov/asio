//
// detail/linux_io_uring_sendto_op.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2019 Christopher M. Kohlhoff (chris at kohlhoff dot com)
// Copyright (c) 2019 George Shramov (goxash at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_LINUX_IO_URING_SENDTO_OP_HPP
#define ASIO_DETAIL_LINUX_IO_URING_SENDTO_OP_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"

#if defined(ASIO_HAS_IO_URING)

#include "asio/detail/bind_handler.hpp"
#include "asio/detail/buffer_sequence_adapter.hpp"
#include "asio/detail/fenced_block.hpp"
#include "asio/detail/linux_io_uring_operation.hpp"
#include "asio/detail/memory.hpp"
#include "asio/detail/socket_ops.hpp"

#include "asio/detail/push_options.hpp"

namespace asio {
namespace detail {

template <typename ConstBufferSequence, typename Endpoint>
class linux_io_uring_sendto_op_base : public linux_io_uring_operation
{
public:
  linux_io_uring_sendto_op_base(int socket,
      const ConstBufferSequence& buffers, const Endpoint& endpoint,
      socket_base::message_flags flags, func_type complete_func)
    : linux_io_uring_operation(complete_func,
        &linux_io_uring_sendto_op_base::do_prepare),
      socket_(socket),
      buffers_(buffers),
      flags_(flags),
      message_()
  {
    message_.msg_name = const_cast<socket_addr_type*>(endpoint.data());
    message_.msg_namelen = endpoint.size();
    message_.msg_iov = buffers_.buffers();
    message_.msg_iovlen = buffers_.count();
  }

  static void do_prepare(linux_io_uring_operation *base, io_uring_sqe *sqe)
  {
    linux_io_uring_sendto_op_base* o(
        static_cast<linux_io_uring_sendto_op_base*>(base));

    std::memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_SENDMSG;
    sqe->fd = o->socket_;
    sqe->addr = reinterpret_cast<uint64_t>(&o->message_);
    sqe->len = 1;
    sqe->msg_flags = o->flags_ | MSG_NOSIGNAL;
  }

private:
  int socket_;
  buffer_sequence_adapter<asio::const_buffer, ConstBufferSequence> buffers_;
  socket_base::message_flags flags_;
  msghdr message_;
};

template <typename ConstBufferSequence, typename Endpoint,
    typename Handler, typename IoExecutor>
class linux_io_uring_sendto_op :
  public linux_io_uring_sendto_op_base<ConstBufferSequence, Endpoint>
{
public:
  ASIO_DEFINE_HANDLER_PTR(linux_io_uring_sendto_op);

  linux_io_uring_sendto_op(int socket, const ConstBufferSequence& buffers,
      const Endpoint& endpoint, socket_base::message_flags flags,
      Handler& handler, const IoExecutor& io_ex)
    : linux_io_uring_sendto_op_base<ConstBufferSequence, Endpoint>(
        socket, buffers, endpoint, flags,
        &linux_io_uring_sendto_op::do_complete),
      handler_(ASIO_MOVE_CAST(Handler)(handler)),
      io_executor_(io_ex)
  {
    handler_work<Handler, IoExecutor>::start(handler_, io_executor_);
  }

  static void do_complete(void* owner, operation* base,
      const asio::error_code& /*ec*/,
      std::size_t /*bytes_transferred*/)
  {
    // Take ownership of the handler object.
    linux_io_uring_sendto_op* o(
        static_cast<linux_io_uring_sendto_op*>(base));
    ptr p = { asio::detail::addressof(o->handler_), o, o };
    handler_work<Handler, IoExecutor> w(o->handler_, o->io_executor_);

    ASIO_HANDLER_COMPLETION((*o));

    // Make a copy of the handler so that the memory can be deallocated before
    // the upcall is made. Even if we're not about to make an upcall, a
    // sub-object of the handler may be the true owner of the memory associated
    // with the handler. Consequently, a local copy of the handler is required
    // to ensure that any owning sub-object remains valid until after we have
    // deallocated the memory here.
    detail::binder2<Handler, asio::error_code, std::size_t>
      handler(o->handler_, o->ec_, o->bytes_transferred_);
    p.h = asio::detail::addressof(handler.handler_);
    p.reset();

    // Make the upcall if required.
    if (owner)
    {
      fenced_block b(fenced_block::half);
      ASIO_HANDLER_INVOCATION_BEGIN((handler.arg1_, handler.arg2_));
      w.complete(handler, handler.handler_);
      ASIO_HANDLER_INVOCATION_END;
    }
  }

private:
  Handler handler_;
  IoExecutor io_executor_;
};

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // defined(ASIO_HAS_IO_URING)

#endif // ASIO_DETAIL_LINUX_IO_URING_SENDTO_OP_HPP
