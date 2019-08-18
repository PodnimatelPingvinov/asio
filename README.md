Experimental *io_uring* support for Asio library. *io_uring* is fairly new API, which can be used only on recent Linux kernels (v5.3+). Most of the information about *io_uring* I have found on following resources:
* http://kernel.dk/io_uring.pdf
* http://git.kernel.dk/cgit/liburing/

My implementation successfully passes all existing unit tests, however, there are some problems and limitations with it, which are described below. Thus the implementation should be considered as experimental.

Why do we need support for this backend? I suppose there are two main reasons:
1. File support. *io_uring* allows to perform asynchronous operations on regular files. Reactors don't support such feature and always return readiness on file descriptors. Even aio interface support asynchronous file operations only for files, opened with O_DIRECT flag. *io_uring* doesn't have such limitations, so we can implement something like `random_access_handle` from Windows on Linux.
2. Performance (?). *io_uring* described as very fast interface, but I failed to get its advantages with Asio. Check corresponding section below for more information.

The next sections describe Asio with *io_uring* problems and implementation details more precisely.

Building
---

Since the *io_uring* backend support is experimental, it is not enabled by default. You have to enable it by defining `ASIO_ENABLE_IO_URING`. If this name is defined, and user system supports *io_uring*, then corresponding backend will be used. The user also have to link application against liburing for successfull program building. Note that I use `IORING_OP_SEND/RECVMSG` commands, which are introduced only in Linux kernel 5.3+, so new functionality wouldn't work even with 5.2 kernel, which has *io_uring*, but doesn't have send/recv msg commands.

I also modified build system (in separate commit) for easier *io_uring* usage. You can pass `--enable-io-uring` to configure script, which adds appropriate defines, link flags, etc. You can also use `--liburing-dir=` to explicitly specify liburing location, if it is not in system directory.

Concurrency
---

*io_uring* consists of two independent ring buffers: submission queue (SQ) and completion queue (CQ). It is fine to post jobs to SQ and read completions from CQ concurrently. However, you can't concurrently make 2 writes to SQ or 2 reads from CQ. You can concurrently wait for completions in CQ (using `io_uring_wait_cqe` in multiple threads), but in such situation all waiting threads will be woken up when completion event become available. This is not what you want, that's why only one thread should wait for completions in the same time. Therefore I use *io_uring* in the same manner as reactor works, via existing scheduler.

Maximum number of pending operations
---

Since CQ is ring buffer, it has fixed size. Therefore it can overflow, in which case the application has failed. I solved this problem pretty straightforward by not allowing more than CQ ring max size pending operations run simultaneously. CQ ring size is twice SQ ring size and since maximum allowed SQ ring size is 4096, we can't get more than 8192 operations run in tne same time with strong guarantee to not overflow buffer. This is really small value if we need to wait data on many connections. I see two possible improvements on this: use some advanced algorithms/heuristics to control flow or use multiple *io_uring* instances, multiplexed by some reactor.

Operation cancelation
---

*io_uring* supports operation cancelation only for limited cases, which are not suits well Asio API. Thus, socket and descriptor methods `cancel()` and `release()` don't cancel outstanding operations with `asio::error::operation_aborted`. There is also problem with `close()` method: currently *io_uring* doesn't cancel associated operations even if corresponding decriptor has been closed.

Performance
---

I've tested performance with existing test (src/tests/performance) and the results aren't good. There is ~30% worse performance compared to epoll implementation. I think the main reason is that I always use system call (`io_uring_enter`) to start asinchronous IO operation. I don't use any operations grouping and, consequently, there are many context switches caused by often system calls. The first option here is to use `IORING_SETUP_SQPOLL` flag in which case we don't need to call `io_uring_enter()` -- the kernel will poll submission queue for new tasks. However, this requires root privilegues and I failed to make *io_uring* work with this flag. The second option is to group operations before submitting them, but in that case we'll increase operations latency.
