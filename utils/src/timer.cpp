#include "timer.hpp"
#include "logger.hpp"

#include "async_io_context.hpp"

using namespace asio::utils;

Timer::Timer(const TimerConfig& timer_config, boost::asio::io_context& _io_context)
    : _callback(timer_config.callback_fn),
      _start_interval_msec(timer_config.start_interval_msec),
      _periodic_interval_msec(timer_config.periodic_interval_msec),
      _io_context(_io_context),
      _strand(boost::asio::make_strand(_io_context)) {}

Timer::~Timer()
{

    stop();
}

struct TimerStruct : public Timer {
    TimerStruct(const TimerConfig& config, boost::asio::io_context& _io_context)
        : Timer(config, _io_context)
    {
    }
};

std::shared_ptr<Timer> Timer::create(const TimerConfig& config,
                                     boost::asio::io_context& _io_context)
{
    return std::make_shared<TimerStruct>(config, _io_context);
}

// caller must hold _mutex before calling this method
void Timer::timer_async_wait(bool first_run)
{
    _timer->expires_after(
        std::chrono::milliseconds(first_run ? _start_interval_msec : _periodic_interval_msec));
    _timer->async_wait(boost::asio::bind_executor(
        _strand, std::bind(&Timer::timer_callback, shared_from_this(), std::placeholders::_1)));
}

void Timer::start()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    start_noLock();
}

void Timer::start_noLock()
{
    if (!_timer) {
        _timer = std::make_unique<boost::asio::steady_timer>(_io_context);

        timer_async_wait(true);
    }
}

void Timer::stop()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    stop_no_lock();
}

void Timer::stop_no_lock()
{
    if (_timer) {
        _timer->cancel();
        _timer = nullptr;
    }
}

void Timer::restart()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    stop_no_lock();
    start_noLock();
}

void Timer::restart(std::function<void()> callback_fn,
                    std::chrono::milliseconds start_interval_msec,
                    std::chrono::milliseconds periodic_interval_msec)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    stop_no_lock();
    _callback               = callback_fn;
    _start_interval_msec    = start_interval_msec;
    _periodic_interval_msec = periodic_interval_msec;
    start_noLock();
}

void Timer::set_start_interval_msec(std::chrono::milliseconds msec)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _start_interval_msec = msec;
}

void Timer::set_periodic_interval_msec(std::chrono::milliseconds msec)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _periodic_interval_msec = msec;
}

std::chrono::milliseconds Timer::get_start_interval_msec() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _start_interval_msec;
}

std::chrono::milliseconds Timer::get_periodic_interval_msec() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _periodic_interval_msec;
}

void Timer::set_callback(std::function<void()> callback_fn)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _callback = callback_fn;
}

void Timer::timer_callback(const std::error_code& ec)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (ec) {
        if (ec.value() == boost::asio::error::operation_aborted) {
            LOG_ERROR(L_ASIOUTIL, "In {} operation aborted..{} {}", __func__, ec.value(), ec.message());
            return;
        }
        else {
            LOG_ERROR(L_ASIOUTIL, "timer_callback error {} {}", ec.value(), ec.message());
        }
    }
    if (_timer) {
        call_callback();
        
        if (_periodic_interval_msec > std::chrono::milliseconds(0) && _timer) {
            timer_async_wait(false);
        }
        else {
            _timer = nullptr;
        }
    }
    else {
        LOG_TRACE(L_ASIOUTIL, "Timer was stopped");
    }
}

void Timer::call_callback()
{
    if (_callback) {
        _callback();
    }
    else {
        LOG_WARN(L_ASIOUTIL, "Undefined callback");
    }
}

bool Timer::is_started()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_timer) {
        return true;
    }
    return false;
}
