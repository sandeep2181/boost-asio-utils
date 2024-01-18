#ifndef _UDP_CLIENT_HPP_
#define _UDP_CLIENT_HPP_

#include <boost/asio/ip/udp.hpp>
#include <set>

namespace asio::utils {

class UdpClient {

public:
    static constexpr size_t MAX_MESSAGE_SIZE = 2 * 1024;

    using data_handler_t = std::function<void(const boost::system::error_code& error, size_t bytes_transferred)>;
    using callback_t     = std::function<void(std::vector<char>& data, size_t size)>;

    static std::unique_ptr<UdpClient> create(boost::asio::io_context& io, const std::string& addr,
                                             uint16_t receive_port, uint16_t send_port);

    virtual ~UdpClient() = default;

    virtual int async_send(const void* data, size_t size, data_handler_t&& handler = nullptr) = 0;

    virtual int register_callback(const char* id, callback_t&& callback) = 0;

    virtual int unregister_callback(const char* id) = 0;
};
}
#endif
