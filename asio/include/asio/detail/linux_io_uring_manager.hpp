//
// detail/linux_io_uring_manager.hpp
// ~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2019 Christopher M. Kohlhoff (chris at kohlhoff dot com)
// Copyright (c) 2019 George Shramov (goxash at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_LINUX_IO_URING_MANAGER_HPP
#define ASIO_DETAIL_LINUX_IO_URING_MANAGER_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"

#if defined(ASIO_HAS_IO_URING)

#include "asio/error_code.hpp"
#include "asio/execution_context.hpp"
#include "asio/detail/atomic_count.hpp"
#include "asio/detail/conditionally_enabled_mutex.hpp"
#include "asio/detail/hash_map.hpp"
#include "asio/detail/linux_io_uring_operation.hpp"
#include "asio/detail/op_queue.hpp"
#include "asio/detail/reactor_op.hpp"
#include "asio/detail/reactor_op_queue.hpp"
#include "asio/detail/thread.hpp"
#include "asio/detail/thread_context.hpp"
#include "asio/detail/timer_queue_base.hpp"
#include "asio/detail/timer_queue_set.hpp"
#include "asio/detail/wait_op.hpp"

#include <sys/timerfd.h>

#include "asio/detail/push_options.hpp"

namespace asio {
namespace detail {

class linux_io_uring_manager
  : public execution_context_service_base<linux_io_uring_manager>
{
public:
  // Constructor.
  ASIO_DECL linux_io_uring_manager(asio::execution_context& ctx);

  // Destructor.
  ASIO_DECL ~linux_io_uring_manager();

  // Destroy all user-defined handler objects owned by the service.
  ASIO_DECL void shutdown();

  // Recreate internal descriptors following a fork.
  ASIO_DECL void notify_fork(asio::execution_context::fork_event fork_ev);

  // Initialise the task.
  ASIO_DECL void init_task();

  // Start a new operation.
  ASIO_DECL void start_op(linux_io_uring_operation* op, bool is_continuation);

  // Register a descriptor with an associated single read operation. Returns
  // 0 on success, system error code on failure.
  ASIO_DECL void register_internal_read_descriptor(int descriptor,
      reactor_op* op);

  // Remove the descriptor's registration.
  ASIO_DECL void deregister_internal_read_descriptor(int descriptor);

  // Post an operation for immediate completion.
  void post_immediate_completion(linux_io_uring_operation* op,
      bool is_continuation)
  {
    scheduler_.post_immediate_completion(op, is_continuation);
  }

  // Add a new timer queue to the service.
  template <typename Time_Traits>
  void add_timer_queue(timer_queue<Time_Traits>& timer_queue);

  // Remove a timer queue from the service.
  template <typename Time_Traits>
  void remove_timer_queue(timer_queue<Time_Traits>& timer_queue);

  // Schedule a new operation in the given timer queue to expire at the
  // specified absolute time.
  template <typename Time_Traits>
  void schedule_timer(timer_queue<Time_Traits>& queue,
      const typename Time_Traits::time_type& time,
      typename timer_queue<Time_Traits>::per_timer_data& timer, wait_op* op);

  // Cancel the timer associated with the given token. Returns the number of
  // handlers that have been posted or dispatched.
  template <typename Time_Traits>
  std::size_t cancel_timer(timer_queue<Time_Traits>& queue,
      typename timer_queue<Time_Traits>::per_timer_data& timer,
      std::size_t max_cancelled = (std::numeric_limits<std::size_t>::max)());

  // Move the timer operations associated with the given timer.
  template <typename Time_Traits>
  void move_timer(timer_queue<Time_Traits>& queue,
      typename timer_queue<Time_Traits>::per_timer_data& to,
      typename timer_queue<Time_Traits>::per_timer_data& from);

  // Run io_uring once until interrupted or events are ready to be dispatched.
  ASIO_DECL void run(long usec, op_queue<operation>& ops);

  // Interrupt the event processing loop.
  ASIO_DECL void interrupt();

private:
  // Internal operations
  enum op_types { regular_op = -3, timer_op = -2, interrupt_op = -1,
    poll_cancel_op = 0, internal_op = 1 };

  // The size of the io_uring, which is passed to its constructor.
  enum { io_uring_size = 4096 };

  // The mutex type used by this io_uring_context.
  typedef conditionally_enabled_mutex mutex;

  // Create the timerfd file descriptor. Does not throw.
  ASIO_DECL static int do_timerfd_create();

  // Helper function to add a new timer queue.
  ASIO_DECL void do_add_timer_queue(timer_queue_base& queue);

  // Helper function to remove a timer queue.
  ASIO_DECL void do_remove_timer_queue(timer_queue_base& queue);

  // Called to recalculate and update the timeout.
  ASIO_DECL void update_timeout(long min_usec = 0);

  // Helper function to post timerfd poll operation.
  ASIO_DECL void post_timer_operation();

  // Helper function to post poll read operation.
  ASIO_DECL void post_poll_read_operation(int descriptor);

  // Start as much as possible operations from pending_ops_ queue. Assumes
  // that mutex was previously locked.
  ASIO_DECL void start_pending_operations();

  // Post operation to io_uring. Assumes that mutex was previously locked and
  // there is enough resources for operation (i.e. we haven't reach
  // max_cq_entries_ or max_sq_entries_)
  ASIO_DECL void enqueue_operation_unlocked(linux_io_uring_operation *op);

  // Handle one completion event. Assumes that mutex was previously locked.
  // Does not call io_uring_cqe_seen, so it must be called after that method.
  ASIO_DECL void process_cqe(io_uring_cqe *cqe, op_queue<operation>& ops);

  // The scheduler implementation used to post completions.
  scheduler& scheduler_;

  // The io_uring used for queueing operations.
  io_uring io_uring_;

  // The queue of operations which should be performed (i.e. enqueued to
  // io_uring), but can't because io_uring is full (i.e. submission queue or
  // completion queue is full). Note the difference with outstanding_ops_:
  // outstanding operations are the ones that were submitted to io_uring.
  op_queue<linux_io_uring_operation> pending_ops_;

  // The map of operations that were enqueued to io_uring. It serves two
  // purposes. The first one is to get completion handler (which is inside
  // operation object) from operation id (we can get id from io_uring's cqe
  // user_data field). The second purpose is to cancel operations on service
  // shutdown.
  hash_map<long, linux_io_uring_operation*> outstanding_ops_;

  // The queue of reactive-style internal read operations.
  reactor_op_queue<int> internal_read_ops_;

  // Mutex to protect access to internal data.
  mutable mutex mutex_;

  // The timer queues.
  timer_queue_set timer_queues_;

  // The timer file descriptor.
  int timer_fd_;

  // Whether the interrupt event has been posted. This flag helps to be sure
  // that no more then one interrupt operation has been submitted.
  bool interrupted_;

  // Whether the service has been shut down.
  bool shutdown_;

  // Contains operation id, which will be assigned to the next operation.
  // This variable is only decremented and must be <= regular_op.
  long operation_id_;

  // Maximum number of entries in io_uring submission queue.
  long max_sq_entries_;

  // Maximum number of entries in io_uring completion queue.
  long max_cq_entries_;
};

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

# include "asio/detail/impl/linux_io_uring_manager.hpp"
#if defined(ASIO_HEADER_ONLY)
# include "asio/detail/impl/linux_io_uring_manager.ipp"
#endif // defined(ASIO_HEADER_ONLY)

#endif // defined(ASIO_HAS_IO_URING)

#endif // ASIO_DETAIL_LINUX_IO_URING_MANAGER_HPP
