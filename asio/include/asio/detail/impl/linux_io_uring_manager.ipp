//
// detail/impl/linux_io_uring_manager.ipp
// ~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2019 Christopher M. Kohlhoff (chris at kohlhoff dot com)
// Copyright (c) 2019 George Shramov (goxash at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_IMPL_LINUX_IO_URING_MANAGER_IPP
#define ASIO_DETAIL_IMPL_LINUX_IO_URING_MANAGER_IPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"

#if defined(ASIO_HAS_IO_URING)

#include "asio/detail/concurrency_hint.hpp"
#include "asio/detail/limits.hpp"
#include "asio/detail/linux_io_uring_manager.hpp"

#include "asio/detail/push_options.hpp"

namespace asio {
namespace detail {

linux_io_uring_manager::linux_io_uring_manager(asio::execution_context& ctx)
  : asio::detail::execution_context_service_base<linux_io_uring_manager>(ctx),
    scheduler_(use_service<scheduler>(ctx)),
    mutex_(ASIO_CONCURRENCY_HINT_IS_LOCKING(
          REACTOR_REGISTRATION, scheduler_.concurrency_hint())),
    timer_fd_(do_timerfd_create()),
    interrupted_(false),
    shutdown_(false),
    operation_id_(regular_op)
{
  if (::io_uring_queue_init(io_uring_size, &io_uring_, 0) < 0)
  {
    asio::error_code ec(errno, asio::error::get_system_category());
    asio::detail::throw_error(ec, "io_uring");
  }

  if (timer_fd_ < 0)
  {
    asio::error_code ec(errno, asio::error::get_system_category());
    asio::detail::throw_error(ec, "io_uring (timerfd)");
  }

  // Reserve 2 operations for timerfd operation and interruption.
  max_sq_entries_ = *(io_uring_.sq.kring_entries) - 2;
  max_cq_entries_ = *(io_uring_.cq.kring_entries) - 2;

  post_timer_operation();
}

linux_io_uring_manager::~linux_io_uring_manager()
{
  ::io_uring_queue_exit(&io_uring_);
  close(timer_fd_);
}

void linux_io_uring_manager::shutdown()
{
  mutex::scoped_lock lock(mutex_);
  shutdown_ = true;
  lock.unlock();

  op_queue<operation> ops;

  for (hash_map<long, linux_io_uring_operation*>::iterator
    it = outstanding_ops_.begin(); it != outstanding_ops_.end(); ++it)
  {
    ops.push(it->second);
  }
  outstanding_ops_.clear();

  internal_read_ops_.get_all_operations(ops);
  timer_queues_.get_all_timers(ops);

  scheduler_.abandon_operations(ops);
}

void linux_io_uring_manager::notify_fork(
    asio::execution_context::fork_event fork_ev)
{
  if (fork_ev == asio::execution_context::fork_child)
  {
      ::io_uring_queue_exit(&io_uring_);
      ::close(timer_fd_);
      timer_fd_ = do_timerfd_create();

      if (::io_uring_queue_init(io_uring_size, &io_uring_, 0) < 0)
      {
          asio::error_code ec(errno, asio::error::get_system_category());
          asio::detail::throw_error(ec, "io_uring");
      }

      if (timer_fd_ < 0)
      {
          asio::error_code ec(errno, asio::error::get_system_category());
          asio::detail::throw_error(ec, "io_uring (timerfd)");
      }

      update_timeout();
      post_timer_operation();
  }
}

void linux_io_uring_manager::init_task()
{
  scheduler_.init_task();
}

void linux_io_uring_manager::start_op(
    linux_io_uring_operation* op, bool is_continuation)
{
  mutex::scoped_lock lock(mutex_);
  pending_ops_.push(op);
  start_pending_operations();
  scheduler_.work_started();
}

void linux_io_uring_manager::start_pending_operations()
{
  if (!pending_ops_.empty())
  {
    linux_io_uring_operation *op;
    unsigned available_cq_slots = max_cq_entries_ - outstanding_ops_.size();
    unsigned available_sq_slots = max_sq_entries_ - (io_uring_.sq.sqe_tail -
      io_uring_.sq.sqe_head);
    unsigned available_slots = (available_cq_slots < available_sq_slots)
      ? available_cq_slots : available_sq_slots;
    for (unsigned i = 0; i < available_slots && !pending_ops_.empty(); ++i)
    {
      op = pending_ops_.front();
      pending_ops_.pop();
      enqueue_operation_unlocked(op);
    }
  }
}

void linux_io_uring_manager::enqueue_operation_unlocked(
    linux_io_uring_operation* op)
{
  io_uring_sqe *sqe = &io_uring_.sq.sqes[io_uring_.sq.sqe_head &
    *io_uring_.sq.kring_mask];
  ++io_uring_.sq.sqe_tail;
  op->prepare_sqe(sqe);

  sqe->user_data = operation_id_;
  outstanding_ops_.insert(
    hash_map<long, linux_io_uring_operation*>::value_type(operation_id_, op));
  --operation_id_;
  // If we get overflow
  if (operation_id_ > 0)
      operation_id_ = regular_op;

  ::io_uring_submit(&io_uring_);
}

void linux_io_uring_manager::process_cqe(io_uring_cqe *cqe,
    op_queue<operation>& ops)
{
  signed_size_type ret_code = cqe->res;
  long id = static_cast<long>(cqe->user_data);
  linux_io_uring_operation *op;

  if (id <= regular_op)
  {
    hash_map<long, linux_io_uring_operation*>::iterator
      iter = outstanding_ops_.find(id);
    if (iter != outstanding_ops_.end())
    {
      op = iter->second;
      op->ec_ = asio::error_code();
      op->bytes_transferred_ = ret_code;

      if (ret_code < 0)
      {
        op->ec_ = asio::error_code(-ret_code,
          asio::error::get_system_category());
        op->bytes_transferred_ = 0;
      }

      outstanding_ops_.erase(id);

      if (op->ec_ == asio::error::would_block
        || op->ec_ == asio::error::try_again
        || !op->perform())
        pending_ops_.push(op);
      else
        ops.push(op);
    }
  }
  else if (id >= internal_op)
  {
    if (internal_read_ops_.has_operation(id))
      if (internal_read_ops_.perform_operations(id, ops))
        post_poll_read_operation(id);
  }
  else if (id == timer_op)
  {
    timer_queues_.get_ready_timers(ops);
    update_timeout();
    post_timer_operation();
  }
  else if (id == interrupt_op)
    interrupted_ = false;
}

void linux_io_uring_manager::run(long usec, op_queue<operation>& ops)
{
  mutex::scoped_lock lock(mutex_);
  io_uring_cqe *cqe;
  int count = 0;
  update_timeout(usec);

  ::io_uring_peek_cqe(&io_uring_, &cqe);
  while (cqe && count < 128)
  {
    process_cqe(cqe, ops);
    ::io_uring_cqe_seen(&io_uring_, cqe);
    count++;
    ::io_uring_peek_cqe(&io_uring_, &cqe);
  }
  start_pending_operations();
  lock.unlock();

  if (count || usec == 0 || ::io_uring_wait_cqe(&io_uring_, &cqe) < 0)
    return;

  lock.lock();
  process_cqe(cqe, ops);
  ::io_uring_cqe_seen(&io_uring_, cqe);
  start_pending_operations();
}

int linux_io_uring_manager::do_timerfd_create()
{
#if defined(TFD_CLOEXEC)
  int fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
#else // defined(TFD_CLOEXEC)
  int fd = -1;
  errno = EINVAL;
#endif // defined(TFD_CLOEXEC)

  if (fd == -1 && errno == EINVAL)
  {
    fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (fd != -1)
      ::fcntl(fd, F_SETFD, FD_CLOEXEC);
  }

  return fd;
}

void linux_io_uring_manager::do_add_timer_queue(timer_queue_base& queue)
{
  mutex::scoped_lock lock(mutex_);
  timer_queues_.insert(&queue);
}

void linux_io_uring_manager::do_remove_timer_queue(timer_queue_base& queue)
{
  mutex::scoped_lock lock(mutex_);
  timer_queues_.erase(&queue);
}

void linux_io_uring_manager::update_timeout(long min_usec)
{
  itimerspec new_ts;
  itimerspec old_ts;
  long usec = 5 * 60 * 1000 * 1000;
  std::memset(&new_ts, 0, sizeof(new_ts));
  std::memset(&old_ts, 0, sizeof(old_ts));

  timerfd_gettime(timer_fd_, &old_ts);
  long cur_usec = old_ts.it_value.tv_sec * 1000 * 1000 +
    old_ts.it_value.tv_nsec / 1000;
  if (cur_usec > 0 && cur_usec < usec)
    usec = cur_usec;
  if (min_usec > 0 && min_usec < usec)
    usec = min_usec;
  usec = timer_queues_.wait_duration_usec(usec);

  int flags = usec ? 0 : TFD_TIMER_ABSTIME;
  new_ts.it_value.tv_sec = usec / 1000000;
  new_ts.it_value.tv_nsec = usec ? (usec % 1000000) * 1000 : 1;
  timerfd_settime(timer_fd_, flags, &new_ts, &old_ts);
}

void linux_io_uring_manager::interrupt()
{
  mutex::scoped_lock lock(mutex_);

  if (interrupted_)
    return;
  interrupted_ = true;

  io_uring_sqe *sqe = &io_uring_.sq.sqes[io_uring_.sq.sqe_head &
    *io_uring_.sq.kring_mask];
  ++io_uring_.sq.sqe_tail;

  std::memset(sqe, 0, sizeof(*sqe));
  sqe->opcode = IORING_OP_NOP;
  sqe->user_data = interrupt_op;

  ::io_uring_submit(&io_uring_);
}

void linux_io_uring_manager::post_timer_operation()
{
  io_uring_sqe *sqe = &io_uring_.sq.sqes[io_uring_.sq.sqe_head &
    *io_uring_.sq.kring_mask];
  ++io_uring_.sq.sqe_tail;

  std::memset(sqe, 0, sizeof(*sqe));
  sqe->opcode = IORING_OP_POLL_ADD;
  sqe->fd = timer_fd_;
  sqe->poll_events = EPOLLIN;
  sqe->user_data = timer_op;

  ::io_uring_submit(&io_uring_);
}

void linux_io_uring_manager::post_poll_read_operation(int descriptor)
{
  io_uring_sqe *sqe = &io_uring_.sq.sqes[io_uring_.sq.sqe_head &
    *io_uring_.sq.kring_mask];
  ++io_uring_.sq.sqe_tail;

  std::memset(sqe, 0, sizeof(*sqe));
  sqe->opcode = IORING_OP_POLL_ADD;
  sqe->fd = descriptor;
  sqe->poll_events = EPOLLIN;
  sqe->user_data = descriptor;

  ::io_uring_submit(&io_uring_);
}

void linux_io_uring_manager::register_internal_read_descriptor(int descriptor,
    reactor_op *op)
{
  mutex::scoped_lock lock(mutex_);

  bool first = internal_read_ops_.enqueue_operation(descriptor, op);

  // If this is first operation, we need to post it to io_uring.
  if (first)
    post_poll_read_operation(descriptor);
}

void linux_io_uring_manager::deregister_internal_read_descriptor(int descriptor)
{
  mutex::scoped_lock lock(mutex_);

  if (internal_read_ops_.has_operation(descriptor))
  {
    op_queue<operation> ops;
    internal_read_ops_.cancel_operations(descriptor, ops);

    io_uring_sqe *sqe = &io_uring_.sq.sqes[io_uring_.sq.sqe_head &
      *io_uring_.sq.kring_mask];
    ++io_uring_.sq.sqe_tail;

    std::memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_POLL_REMOVE;
    sqe->addr = descriptor;
    sqe->user_data = poll_cancel_op;

    ::io_uring_submit(&io_uring_);
  }
}

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // defined(ASIO_HAS_IO_URING)

#endif // ASIO_DETAIL_IMPL_LINUX_IO_URING_MANAGER_IPP
