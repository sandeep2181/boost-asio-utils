#ifndef _UTILS_TIMER_HPP_
#define _UTILS_TIMER_HPP_

#include <boost/asio.hpp>
#include <functional>
#include <string>

namespace asio::utils {

static const char* TIMER_DEFAULT_NAME = "Timer";

struct TimerConfig {

    std::string name{TIMER_DEFAULT_NAME};

    std::function<void()> callback_fn;
    std::chrono::milliseconds start_interval_msec    = std::chrono::milliseconds(0);
    std::chrono::milliseconds periodic_interval_msec = std::chrono::milliseconds(0);
};


class Timer : public std::enable_shared_from_this<Timer> {
public:
    static std::shared_ptr<Timer> create(const TimerConfig& config,
                                         boost::asio::io_context& _io_context);

    // destructor
    virtual ~Timer();

    // delete copy constructor
    Timer(const Timer& rhs) = delete;

    // delete move constructor
    Timer(const Timer&& rhs) = delete;

    void start();

    void restart();

    void restart(std::function<void()> callback_fn,
                 std::chrono::milliseconds start_interval_msec    = std::chrono::milliseconds(0),
                 std::chrono::milliseconds periodic_interval_msec = std::chrono::milliseconds(0));

    bool is_started();

    void stop();

    void set_start_interval_msec(std::chrono::milliseconds interval_msec);

    std::chrono::milliseconds get_start_interval_msec() const;

    void set_periodic_interval_msec(std::chrono::milliseconds msec);

    std::chrono::milliseconds get_periodic_interval_msec() const;

    void set_callback(std::function<void()> callback_fn);

protected:
    Timer(const TimerConfig& config, boost::asio::io_context& _io_context);

private:
    void timer_async_wait(bool first_run);

    void timer_callback(const std::error_code& ec);
    void start_noLock();

    void stop_no_lock();
    void call_callback();

    std::function<void()> _callback;
    std::chrono::milliseconds _start_interval_msec;     
    std::chrono::milliseconds _periodic_interval_msec;  
    std::unique_ptr<boost::asio::steady_timer> _timer;  
    mutable std::recursive_mutex _mutex;
    boost::asio::io_context& _io_context;
    boost::asio::strand<boost::asio::io_context::executor_type> _strand;
};

}

#endif
