#ifndef _UTILS_CAN_HPP_
#define _UTILS_CAN_HPP_

#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <functional>
#include <linux/can.h>
#include <memory>
#include <string>

namespace asio::utils::can {

class Can : public std::enable_shared_from_this<Can> {
public:
    using CanReadHandler = std::function<void(const canfd_frame&)>;
    using CanSendHandler = std::function<void(const boost::system::error_code& err)>;

    static std::shared_ptr<Can> create(boost::asio::io_context& io_ctx, const std::string& can_device_name);

    static std::shared_ptr<Can> create(boost::asio::io_context& io_ctx, int socket_fd);

    virtual void async_send(const canfd_frame& frame, const CanSendHandler& handler) = 0;

    virtual void async_read(CanReadHandler&& can_read_handler) = 0;

    virtual void register_read_callback(CanReadHandler&& can_read_handler) = 0;
    virtual ~Can()                                                   = default;

    Can& operator=(const Can& other) = delete;
    Can& operator=(Can&& other)      = delete;
};

}

#endif
