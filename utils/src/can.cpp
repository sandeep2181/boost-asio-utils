#include "can.hpp"

#include "logger.hpp"
#include <boost/asio.hpp>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <memory>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/socket.h>

namespace asio::utils::can {

class CanImpl : public Can {
public:
    CanImpl(boost::asio::io_context& io_ctx, const std::string& can_device_name);
    CanImpl(boost::asio::io_context& io_ctx, int socket_fd);
    void async_read(CanReadHandler&& can_read_handler) override;
    void async_send(const canfd_frame& frame, const CanSendHandler& handler) override;
    void register_read_callback(CanReadHandler&& can_read_handler) override;

    ~CanImpl() override;

private:
    using async_read_internal_handler_t = std::function<void(const boost::system::error_code& err,
                                                             std::size_t bytes_transferred, const canfd_frame& frame)>;
    void async_read_internal(async_read_internal_handler_t&& h);
    void handle_write(const boost::system::error_code& err, std::size_t bytes_transferred,
                      const CanSendHandler& handler);  // Handler for can messages

    void handle_read(const boost::system::error_code& err, std::size_t bytes_transferred, const canfd_frame& frame,
                     const CanReadHandler& can_read_handler);

    void handle_read_repeat(const boost::system::error_code& err, std::size_t bytes_transferred,
                            const canfd_frame& frame, const CanReadHandler& can_read_handler);

    int create_can_socket(const std::string& can_device_name);

    boost::asio::posix::stream_descriptor _can_stream;
    CanReadHandler _can_read_cb;
};

int CanImpl::create_can_socket(const std::string& can_device_name) {
    if (can_device_name.size() >= IFNAMSIZ) {
                throw std::system_error(
            errno, std::generic_category(),
            fmt::format("CAN device name too long: {}", can_device_name));
    }

    struct sockaddr_can addr = {0};
    int s                    = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
                        throw std::system_error(
            errno, std::generic_category(),
            fmt::format("CAN Socket can't be opened: {}", errno));
    }

    struct ifreq ifr = {};
    strncpy(ifr.ifr_name, can_device_name.c_str(), can_device_name.size());
    int ret = ioctl(s, SIOCGIFINDEX, &ifr);
    if (ret < 0) {
        close(s);
                                throw std::system_error(
            errno, std::generic_category(),
            fmt::format("CAN Device: {} .. can't be opened err: {}",can_device_name, errno));
    }

    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    const int canfd_on = 1;
    ret                = setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &canfd_on, sizeof(canfd_on));
    if (ret < 0) {
        close(s);
        throw std::system_error(
            errno, std::generic_category(),
            fmt::format("CAN Set FD exception: {}", can_device_name));
    }

    ret = bind(s, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        close(s);
        throw std::system_error(
            errno, std::generic_category(),
            fmt::format("CAN Socket Bind exception: {}", errno));
    }

    return s;
}

CanImpl::CanImpl(boost::asio::io_context& io_ctx, const std::string& can_device_name)
    : _can_stream(io_ctx) {

    int can_fd = create_can_socket(can_device_name);

    _can_stream.assign(can_fd);
}

CanImpl::CanImpl(boost::asio::io_context& io_ctx, int socket)
    : _can_stream(io_ctx) {

    _can_stream.assign(socket);
}

CanImpl::~CanImpl() {
    _can_stream.close();
}

void CanImpl::async_read(CanReadHandler&& can_read_handler) {
    async_read_internal([self = shared_from_this(), can_read_handler](auto err, auto bt, auto frame) {
        std::dynamic_pointer_cast<CanImpl>(self)->handle_read(err, bt, frame, can_read_handler);
    });
}

void CanImpl::async_read_internal(async_read_internal_handler_t&& h) {
    std::shared_ptr<canfd_frame> frame(new canfd_frame());
    boost::asio::async_read(_can_stream, boost::asio::buffer(frame.get(), sizeof(canfd_frame)),
                            [h, frame](auto err, auto bt) {
                                assert(frame != nullptr);
                                h(err, bt, *frame);
                            });
}

void CanImpl::async_send(const canfd_frame& cf, const CanSendHandler& handler) {
    boost::asio::async_write(_can_stream, boost::asio::buffer(&cf, sizeof(cf)),
                             [self = shared_from_this(), handler](auto err, auto bt) {
                                 std::dynamic_pointer_cast<CanImpl>(self)->handle_write(err, bt, handler);
                             });
}

void CanImpl::handle_read(const boost::system::error_code& err, std::size_t bytes_transferred,
                                const canfd_frame& frame, const CanReadHandler& can_read_handler) {
    if (err != boost::system::errc::success) {
        if (err == boost::system::errc::operation_canceled) {
            LOG_WARN(L_ASIOUTIL, "Operation cancelled, CAN socket");
        } else if (err) {
            LOG_WARN(L_ASIOUTIL, "Failed to read from CAN error={}, explanation={}", err.value(), err.message());
            if (bytes_transferred < sizeof(canfd_frame)) {
                LOG_WARN(L_ASIOUTIL, "Read incomplete CAN FD frame read={} expected={}", bytes_transferred,
                                   sizeof(canfd_frame));
            }
        }
    } else {
        can_read_handler(frame);
    }
}

void CanImpl::handle_read_repeat(const boost::system::error_code& err, std::size_t bytes_transferred,
                                       const canfd_frame& frame, const CanReadHandler& can_read_handler) {
    handle_read(err, bytes_transferred, frame, can_read_handler);

    async_read_internal([self = shared_from_this()](auto err, auto bt, auto frame) {
        auto self_derived = std::dynamic_pointer_cast<CanImpl>(self);
        self_derived->handle_read_repeat(err, bt, frame, self_derived->_can_read_cb);
    });
}

void CanImpl::handle_write(const boost::system::error_code& err, std::size_t /* bytes_transferred */,
                                 const CanSendHandler& handler) {
    if (handler) {
        handler(err);
    }
}

void CanImpl::register_read_callback(CanReadHandler&& can_read_handler) {
    _can_stream.cancel();
    _can_read_cb = std::move(can_read_handler);

    async_read_internal([self = shared_from_this()](auto err, auto bt, auto frame) {
        auto self_derived = std::dynamic_pointer_cast<CanImpl>(self);
        self_derived->handle_read_repeat(err, bt, frame, self_derived->_can_read_cb);
    });
}

}

std::shared_ptr<asio::utils::can::Can> asio::utils::can::Can::create(boost::asio::io_context& io_ctx,
                                                                                 const std::string& can_device_name) {
    return std::make_shared<CanImpl>(io_ctx, can_device_name);
}

std::shared_ptr<asio::utils::can::Can> asio::utils::can::Can::create(boost::asio::io_context& io_ctx,
                                                                                 int socket) {
    return std::make_shared<CanImpl>(io_ctx, socket);
}
