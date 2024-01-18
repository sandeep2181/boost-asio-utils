#include "udp_client.hpp"
#include "logger.hpp"
#include <boost/asio.hpp>
#include <functional>
#include <map>
#include <vector>

namespace asio::utils {

// Initial receive message buffer size
static constexpr size_t INIT_MESSAGE_SIZE = 1024;

/**
 * UDP client implementation.
 */
class UdpClientImpl : public UdpClient {
public:
    UdpClientImpl(boost::asio::io_context& io, const std::string& addr, uint16_t receive_port, uint16_t send_port);
    ~UdpClientImpl();
    int async_send(const void* data, size_t size, data_handler_t&& handler = nullptr);
    int register_callback(const char* id, callback_t&& callback);
    int unregister_callback(const char* id);

private:
    void receive_handler(const boost::system::error_code& error, size_t bytes_transferred);
    void receive_loop();
    void handle_send(const boost::system::error_code& error, std::size_t bytes_transferred);
    boost::asio::io_context& _io;
    boost::asio::ip::udp::resolver _resolver;
    std::string _receive_port;
    std::string _send_port;
    boost::asio::ip::udp::endpoint _receive_endpoint;
    boost::asio::ip::udp::endpoint _send_endpoint;
    boost::asio::ip::udp::socket _socket;

    std::map<std::string, callback_t> _callbacks;
    std::vector<char> _rcv_buf;
};

UdpClientImpl::UdpClientImpl(boost::asio::io_context& io, const std::string& addr, uint16_t receive_port,
                             uint16_t send_port)
    : _io(io),
      _resolver(_io),
      _receive_port(std::to_string(receive_port)),
      _send_port(std::to_string(send_port)),
      _receive_endpoint(*_resolver.resolve(boost::asio::ip::udp::v4(), addr, _receive_port.c_str()).begin()),
      _send_endpoint(*_resolver.resolve(boost::asio::ip::udp::v4(), addr, _send_port.c_str()).begin()),
      _socket(io, _receive_endpoint),
      _rcv_buf(INIT_MESSAGE_SIZE) {
    boost::system::error_code ec;
    auto address = boost::asio::ip::make_address(addr, ec);
    if (ec) {
        LOG_ERROR(L_ASIOUTIL, "[{}] Malformed UDP server address: {}", __func__, addr);
        boost::asio::detail::throw_error(ec);
    }

    std::stringstream printable_endpoint;
    printable_endpoint << _receive_endpoint;

    LOG_INFO(L_ASIOUTIL, "[{}] UDP Client Created: Listening on {}", __func__, printable_endpoint.str());

    receive_loop();
}

UdpClientImpl::~UdpClientImpl() {
    // Close socket and cancel all the related async operations
    _socket.close();
}

int UdpClientImpl::async_send(const void* data, size_t size, data_handler_t&& handler) {

    if (data == nullptr) {
        LOG_ERROR(L_ASIOUTIL, "[{}] An attempt to send null data pointer", __func__);
        return EINVAL;
    }
    if (handler == nullptr) {
        handler = [&](const boost::system::error_code& error, size_t bytes_transferred) {
            LOG_TRACE(L_ASIOUTIL, "[{}] Stub send handler executed", __func__);
        };
    }

    _socket.async_send_to(boost::asio::const_buffer(data, size), _send_endpoint,
                          std::bind(&UdpClientImpl::handle_send, this, std::placeholders::_1, std::placeholders::_2));
    return 0;
}

int UdpClientImpl::register_callback(const char* id, callback_t&& callback) {
    if (callback == nullptr) {
        LOG_ERROR(L_ASIOUTIL, "Callback pointer cannot be nullptr");
        return EINVAL;
    }
    const auto [_, success] = _callbacks.insert({id, callback});

    // Convert bool to int return (true -> 0, false -> 1)
    return (int)!success;
}

int UdpClientImpl::unregister_callback(const char* id) {
    auto ret = _callbacks.erase(id);
    return ret == 1 ? 0 : ENOENT;
}

void UdpClientImpl::handle_send(const boost::system::error_code& error, std::size_t bytes_transferred) {
    if (error) {
        LOG_ERROR(L_ASIOUTIL, "Send error: {}", error.message());
    }
}

void UdpClientImpl::receive_handler(const boost::system::error_code& error, size_t bytes_transferred) {
    if (error == boost::asio::error::operation_aborted) {
        // Sending canceled. This means the client was destroyed so do nothing, just return.
        // There is no way to log anything here because _logger is destroyed as well at this moment.
        return;
    }

    if (error) {
        LOG_ERROR(L_ASIOUTIL, "[{}] Socket read error: {} ({})", __func__, error.message(), bytes_transferred);
    } else if (bytes_transferred == 0) {
        LOG_ERROR(L_ASIOUTIL, "[{}] Unexpected empty message received (not in the protocol)", __func__);
    } else if (bytes_transferred > MAX_MESSAGE_SIZE) {
        LOG_ERROR(L_ASIOUTIL, "[{}] Too big message of size {} received. Dropping", __func__, bytes_transferred);
    } else {
        boost::system::error_code ec = {};
        // We'd write data directly to the underlying array, set the actual size first
        _rcv_buf.resize(bytes_transferred);
        auto bytes_read = _socket.receive(boost::asio::buffer(_rcv_buf.data(), bytes_transferred), 0, ec);
        if (ec) {
            LOG_ERROR(L_ASIOUTIL, "[{}] Socket read error: {} ({})", __func__, ec.message(), bytes_read);
        } else if (bytes_read == 0) {
            LOG_ERROR(L_ASIOUTIL, "[{}] Unexpected empty message received (not in the protocol)", __func__);
        } else if (_callbacks.empty()) {
            LOG_DEBUG(L_ASIOUTIL, "[{}] No callback registered. Dropping message", __func__);
        } else {
            if (bytes_read != bytes_transferred) {
                LOG_ERROR(L_ASIOUTIL,
                          " [{}] Unexpected condition! Read data size differs from the lookup result. "
                          "({} != {})",
                          __func__, bytes_read, bytes_transferred);
            }
            // Process the received data with the registered callbacks
            LOG_HEX(L_ASIOUTIL, asio::logger::LogLevel::TRACE, "Received message data", _rcv_buf.data(), bytes_read);
            for (const auto& [key, callback] : _callbacks) {
                // Callbacks are coming from outside, so guard the main loop
                try {
                    callback(_rcv_buf, bytes_read);

                } catch (std::exception& ex) {
                    LOG_ERROR(L_ASIOUTIL, "[{}] Callback \"{}\" threw an exception! {}", __func__, key, ex.what());
                }
            }
        }
    }

    receive_loop();
}

void UdpClientImpl::receive_loop() {

    // Schedule the next async receive first.
    // Passing MSG_PEEK | MSG_TRUNC as a flag results the call to return the received message size keeping the received
    // message untouched. This provides a way to ensure the buffer we have is big enough to keep the entire message.
    // Note: null_buffers is useless here because boost always returns 0 bytes length in this case, so use a real buffer
    // of size 0 to peek the actual message size.
    _socket.async_receive_from(
        boost::asio::buffer(_rcv_buf), _receive_endpoint, MSG_PEEK | MSG_TRUNC,
        std::bind(&UdpClientImpl::receive_handler, this, std::placeholders::_1, std::placeholders::_2));
}

std::unique_ptr<UdpClient> UdpClient::create(boost::asio::io_context& io, const std::string& addr,
                                             uint16_t receive_port, uint16_t send_port) {
    return std::make_unique<UdpClientImpl>(io, addr, receive_port, send_port);
}
}
