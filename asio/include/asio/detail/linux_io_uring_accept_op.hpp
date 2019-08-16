//
// detail/linux_io_uring_accept_op.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2019 Christopher M. Kohlhoff (chris at kohlhoff dot com)
// Copyright (c) 2019 George Shramov (goxash at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_LINUX_IO_URING_ACCEPT_OP_HPP
#define ASIO_DETAIL_LINUX_IO_URING_ACCEPT_OP_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"

#if defined(ASIO_HAS_IO_URING)

#include "asio/detail/bind_handler.hpp"
#include "asio/detail/fenced_block.hpp"
#include "asio/detail/linux_io_uring_operation.hpp"
#include "asio/detail/memory.hpp"
#include "asio/detail/socket_holder.hpp"
#include "asio/detail/socket_ops.hpp"

#include "asio/detail/push_options.hpp"

namespace asio {
namespace detail {

template <typename Socket, typename Protocol>
class linux_io_uring_accept_op_base : public linux_io_uring_operation
{
public:
  linux_io_uring_accept_op_base(socket_type socket,
      socket_ops::state_type state, Socket& peer, const Protocol& protocol,
      typename Protocol::endpoint* peer_endpoint, func_type complete_func)
    : linux_io_uring_operation(complete_func,
        &linux_io_uring_accept_op_base::do_prepare,
        &linux_io_uring_accept_op_base::do_perform),
      socket_(socket),
      state_(state),
      peer_(peer),
      protocol_(protocol),
      peer_endpoint_(peer_endpoint),
      addrlen_(peer_endpoint ? peer_endpoint->capacity() : 0)
  {
  }

  static void do_prepare(linux_io_uring_operation *base, io_uring_sqe *sqe)
  {
    linux_io_uring_accept_op_base* o(
        static_cast<linux_io_uring_accept_op_base*>(base));

    std::memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_POLL_ADD;
    sqe->fd = o->socket_;
    sqe->poll_events = EPOLLIN;
  }

  static bool do_perform(linux_io_uring_operation* base)
  {
    linux_io_uring_accept_op_base* o(
        static_cast<linux_io_uring_accept_op_base*>(base));

    socket_type new_socket = invalid_socket;
    bool result = socket_ops::non_blocking_accept(o->socket_,
        o->state_, o->peer_endpoint_ ? o->peer_endpoint_->data() : 0,
        o->peer_endpoint_ ? &o->addrlen_ : 0, o->ec_, new_socket);
    o->new_socket_.reset(new_socket);

    return result;
  }

  void do_assign()
  {
    if (new_socket_.get() != invalid_socket)
    {
      if (peer_endpoint_)
        peer_endpoint_->resize(addrlen_);
      peer_.assign(protocol_, new_socket_.get(), ec_);
      if (!ec_)
        new_socket_.release();
    }
  }

private:
  socket_type socket_;
  socket_ops::state_type state_;
  socket_holder new_socket_;
  Socket& peer_;
  Protocol protocol_;
  typename Protocol::endpoint* peer_endpoint_;
  std::size_t addrlen_;
};

template <typename Socket, typename Protocol,
    typename Handler, typename IoExecutor>
class linux_io_uring_accept_op :
  public linux_io_uring_accept_op_base<Socket, Protocol>
{
public:
  ASIO_DEFINE_HANDLER_PTR(linux_io_uring_accept_op);

  linux_io_uring_accept_op(socket_type socket,
      socket_ops::state_type state, Socket& peer, const Protocol& protocol,
      typename Protocol::endpoint* peer_endpoint, Handler& handler,
      const IoExecutor& io_ex)
    : linux_io_uring_accept_op_base<Socket, Protocol>(socket, state, peer,
        protocol, peer_endpoint, &linux_io_uring_accept_op::do_complete),
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
    linux_io_uring_accept_op* o(static_cast<linux_io_uring_accept_op*>(base));
    ptr p = { asio::detail::addressof(o->handler_), o, o };
    handler_work<Handler, IoExecutor> w(o->handler_, o->io_executor_);

    // On success, assign new connection to peer socket object.
    if (owner)
      o->do_assign();

    ASIO_HANDLER_COMPLETION((*o));

    // Make a copy of the handler so that the memory can be deallocated before
    // the upcall is made. Even if we're not about to make an upcall, a
    // sub-object of the handler may be the true owner of the memory associated
    // with the handler. Consequently, a local copy of the handler is required
    // to ensure that any owning sub-object remains valid until after we have
    // deallocated the memory here.
    detail::binder1<Handler, asio::error_code>
      handler(o->handler_, o->ec_);
    p.h = asio::detail::addressof(handler.handler_);
    p.reset();

    // Make the upcall if required.
    if (owner)
    {
      fenced_block b(fenced_block::half);
      ASIO_HANDLER_INVOCATION_BEGIN((handler.arg1_));
      w.complete(handler, handler.handler_);
      ASIO_HANDLER_INVOCATION_END;
    }
  }

private:
  Handler handler_;
  IoExecutor io_executor_;
};

#if defined(ASIO_HAS_MOVE)

template <typename Protocol, typename PeerIoExecutor,
    typename Handler, typename IoExecutor>
class linux_io_uring_move_accept_op :
  private Protocol::socket::template rebind_executor<PeerIoExecutor>::other,
  public linux_io_uring_accept_op_base<
    typename Protocol::socket::template rebind_executor<PeerIoExecutor>::other,
    Protocol>
{
public:
  ASIO_DEFINE_HANDLER_PTR(linux_io_uring_move_accept_op);

  linux_io_uring_move_accept_op(const PeerIoExecutor& peer_io_ex,
      socket_type socket, socket_ops::state_type state,
      const Protocol& protocol, typename Protocol::endpoint* peer_endpoint,
      Handler& handler, const IoExecutor& io_ex)
    : peer_socket_type(peer_io_ex),
      linux_io_uring_accept_op_base<peer_socket_type, Protocol>(
        socket, state, *this, protocol, peer_endpoint,
        &linux_io_uring_move_accept_op::do_complete),
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
    linux_io_uring_move_accept_op* o(
        static_cast<linux_io_uring_move_accept_op*>(base));
    ptr p = { asio::detail::addressof(o->handler_), o, o };
    handler_work<Handler, IoExecutor> w(o->handler_, o->io_executor_);

    // On success, assign new connection to peer socket object.
    if (owner)
      o->do_assign();

    ASIO_HANDLER_COMPLETION((*o));

    // Make a copy of the handler so that the memory can be deallocated before
    // the upcall is made. Even if we're not about to make an upcall, a
    // sub-object of the handler may be the true owner of the memory associated
    // with the handler. Consequently, a local copy of the handler is required
    // to ensure that any owning sub-object remains valid until after we have
    // deallocated the memory here.
    detail::move_binder2<Handler,
      asio::error_code, peer_socket_type>
        handler(0, ASIO_MOVE_CAST(Handler)(o->handler_), o->ec_,
          ASIO_MOVE_CAST(peer_socket_type)(*o));
    p.h = asio::detail::addressof(handler.handler_);
    p.reset();

    // Make the upcall if required.
    if (owner)
    {
      fenced_block b(fenced_block::half);
      ASIO_HANDLER_INVOCATION_BEGIN((handler.arg1_, "..."));
      w.complete(handler, handler.handler_);
      ASIO_HANDLER_INVOCATION_END;
    }
  }

private:
  typedef typename Protocol::socket::template
    rebind_executor<PeerIoExecutor>::other peer_socket_type;

  Handler handler_;
  IoExecutor io_executor_;
};

#endif // defined(ASIO_HAS_MOVE)

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // defined(ASIO_HAS_IO_URING)

#endif // ASIO_DETAIL_LINUX_IO_URING_ACCEPT_OP_HPP
