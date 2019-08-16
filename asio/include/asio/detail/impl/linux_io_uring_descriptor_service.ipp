//
// detail/impl/linux_io_uring_descriptor_service.ipp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2019 Christopher M. Kohlhoff (chris at kohlhoff dot com)
// Copyright (c) 2019 George Shramov (goxash at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_IMPL_LINUX_IO_URING_DESCRIPTOR_SERVICE_IPP
#define ASIO_DETAIL_IMPL_LINUX_IO_URING_DESCRIPTOR_SERVICE_IPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"

#if defined(ASIO_HAS_IO_URING)

#include "asio/error.hpp"
#include "asio/detail/linux_io_uring_descriptor_service.hpp"

#include "asio/detail/push_options.hpp"

namespace asio {
namespace detail {

linux_io_uring_descriptor_service::linux_io_uring_descriptor_service(
    execution_context& context)
  : execution_context_service_base<linux_io_uring_descriptor_service>(context),
    io_uring_manager_(use_service<linux_io_uring_manager>(context))
{
  io_uring_manager_.init_task();
}

void linux_io_uring_descriptor_service::shutdown()
{
}

void linux_io_uring_descriptor_service::construct(
    linux_io_uring_descriptor_service::implementation_type& impl)
{
  impl.descriptor_ = -1;
  impl.state_ = 0;
}

void linux_io_uring_descriptor_service::move_construct(
    linux_io_uring_descriptor_service::implementation_type& impl,
    linux_io_uring_descriptor_service::implementation_type& other_impl)
{
  impl.descriptor_ = other_impl.descriptor_;
  other_impl.descriptor_ = -1;

  impl.state_ = other_impl.state_;
  other_impl.state_ = 0;
}

void linux_io_uring_descriptor_service::move_assign(
    linux_io_uring_descriptor_service::implementation_type& impl,
    linux_io_uring_descriptor_service& other_service,
    linux_io_uring_descriptor_service::implementation_type& other_impl)
{
  destroy(impl);

  impl.descriptor_ = other_impl.descriptor_;
  other_impl.descriptor_ = -1;

  impl.state_ = other_impl.state_;
  other_impl.state_ = 0;
}

void linux_io_uring_descriptor_service::destroy(
    linux_io_uring_descriptor_service::implementation_type& impl)
{
  if (is_open(impl))
  {
    ASIO_HANDLER_OPERATION((io_uring_manager_.context(),
          "descriptor", &impl, impl.descriptor_, "close"));

    asio::error_code ignored_ec;
    descriptor_ops::close(impl.descriptor_, impl.state_, ignored_ec);
  }
}

asio::error_code linux_io_uring_descriptor_service::assign(
    linux_io_uring_descriptor_service::implementation_type& impl,
    const native_handle_type& native_descriptor, asio::error_code& ec)
{
  if (is_open(impl))
  {
    ec = asio::error::already_open;
    return ec;
  }

  impl.descriptor_ = native_descriptor;
  impl.state_ = descriptor_ops::possible_dup;
  ec = asio::error_code();
  return ec;
}

asio::error_code linux_io_uring_descriptor_service::close(
    linux_io_uring_descriptor_service::implementation_type& impl,
    asio::error_code& ec)
{
  if (is_open(impl))
  {
    ASIO_HANDLER_OPERATION((io_uring_manager_.context(),
          "descriptor", &impl, impl.descriptor_, "close"));

    descriptor_ops::close(impl.descriptor_, impl.state_, ec);
  }
  else
  {
    ec = asio::error_code();
  }

  // The descriptor is closed by the OS even if close() returns an error.
  //
  // (Actually, POSIX says the state of the descriptor is unspecified. On
  // Linux the descriptor is apparently closed anyway; e.g. see
  //   http://lkml.org/lkml/2005/9/10/129
  // We'll just have to assume that other OSes follow the same behaviour.)
  construct(impl);

  return ec;
}

linux_io_uring_descriptor_service::native_handle_type
linux_io_uring_descriptor_service::release(
    linux_io_uring_descriptor_service::implementation_type& impl)
{
  native_handle_type descriptor = impl.descriptor_;

  if (is_open(impl))
  {
    ASIO_HANDLER_OPERATION((io_uring_manager_.context(),
          "descriptor", &impl, impl.descriptor_, "release"));

    construct(impl);
  }

  return descriptor;
}

asio::error_code linux_io_uring_descriptor_service::cancel(
    linux_io_uring_descriptor_service::implementation_type& impl,
    asio::error_code& ec)
{
  if (!is_open(impl))
  {
    ec = asio::error::bad_descriptor;
    return ec;
  }

  ASIO_HANDLER_OPERATION((io_uring_manager_.context(),
        "descriptor", &impl, impl.descriptor_, "cancel"));

  ec = asio::error::operation_not_supported;
  return ec;
}

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // defined(ASIO_HAS_IO_URING)

#endif // ASIO_DETAIL_IMPL_LINUX_IO_URING_DESCRIPTOR_SERVICE_IPP
