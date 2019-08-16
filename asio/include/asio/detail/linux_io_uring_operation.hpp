//
// detail/linux_io_uring_operation.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2019 Christopher M. Kohlhoff (chris at kohlhoff dot com)
// Copyright (c) 2019 George Shramov (goxash at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_LINUX_IO_URING_OPERATION_HPP
#define ASIO_DETAIL_LINUX_IO_URING_OPERATION_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"

#if defined(ASIO_HAS_IO_URING)

#include "asio/detail/operation.hpp"

#include "asio/detail/push_options.hpp"

namespace asio {
namespace detail {

class linux_io_uring_operation : public operation
{
public:
  // The error code to be passed to the completion handler.
  asio::error_code ec_;

  // The number of bytes transferred, to be passed to the completion handler.
  std::size_t bytes_transferred_;

  static bool do_perform_default(linux_io_uring_operation*)
  {
    return true;
  }

  void prepare_sqe(io_uring_sqe *sqe)
  {
    prepare_func_(this, sqe);
  }

  bool perform()
  {
    return perform_func_(this);
  }

protected:
  typedef void (*prepare_func_type)(linux_io_uring_operation*, io_uring_sqe*);
  typedef bool (*perform_func_type)(linux_io_uring_operation*);

  linux_io_uring_operation(func_type func, prepare_func_type prepare_func,
      perform_func_type perform_func =
      &linux_io_uring_operation::do_perform_default)
    : operation(func),
      prepare_func_(prepare_func),
      perform_func_(perform_func)
  {
  }

private:
  prepare_func_type prepare_func_;
  perform_func_type perform_func_;
};

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // defined(ASIO_HAS_IO_URING)

#endif // ASIO_DETAIL_LINUX_IO_URING_OPERATION_HPP
