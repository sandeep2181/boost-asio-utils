#ifndef _UTILS_ASYNC_IO_CONTEXT_HPP_
#define _UTILS_ASYNC_IO_CONTEXT_HPP_

#include <boost/asio.hpp>
#include <memory>
#include <thread>
#include <vector>

namespace asio::utils {

class AsyncIoContext {
public:
    AsyncIoContext(int pool_size = THREADPOOL_MIN_SIZE);
    AsyncIoContext(const AsyncIoContext& rhs) = delete;
    AsyncIoContext(const AsyncIoContext&& rhs) = delete;
    virtual ~AsyncIoContext();

    boost::asio::io_context& io_ctx();

private:
    static const int THREADPOOL_MIN_SIZE = 16;
    static const int THREADPOOL_MAX_SIZE = 128;

    boost::asio::io_context _async_context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> _work_guard;
    std::vector<std::thread> _thread_pool;
};

}  
#endif 
