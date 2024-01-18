#include "async_io_context.hpp"
#include "logger.hpp"

#include <iostream>

using namespace asio::utils;

AsyncIoContext::AsyncIoContext(int pool_size)
    : _async_context(), _work_guard(boost::asio::make_work_guard(_async_context)), _thread_pool() {
    
    if (pool_size < THREADPOOL_MIN_SIZE) {
        pool_size = THREADPOOL_MIN_SIZE;
    }
    else if (pool_size > THREADPOOL_MAX_SIZE) {
        pool_size = THREADPOOL_MAX_SIZE;
    }
    
    for (int i = 0; i < pool_size; i++) {
        _thread_pool.emplace_back([this, i]() {
            LOG_INFO(L_ASIOUTIL, "Thread {} Started", i);
            while (true) {
                try {
                    this->_async_context.run();
                    break;
                }
                catch (const std::exception& e) {
                    LOG_ERROR(L_ASIOUTIL, "Thread error: {}", e.what());
                }
            }
            LOG_DEBUG(L_ASIOUTIL, "Thread {} Terminated", i);
        });
    }
}

AsyncIoContext::~AsyncIoContext()
{
    _work_guard.reset();
    _async_context.stop();
    for (auto& thread : _thread_pool) {
        thread.join();
    }
    LOG_DEBUG(L_ASIOUTIL, "Joined All Threads");
}

boost::asio::io_context& AsyncIoContext::io_ctx()
{
    return _async_context;
}
