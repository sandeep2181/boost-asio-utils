#include "mqtt_client.hpp"
#include "logger.hpp"

#include "string_util.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mosquitto.h>
#include <string>

namespace asio::utils {


static const std::string DEFAULT_MQTT_BROKER_ADDRESS = "localhost";
static const uint32_t DEFAULT_MQTT_BROKER_PORT       = 1883;

class MqttClientImpl : public asio::utils::MqttClient {
public:
    MqttClientImpl(boost::asio::io_context& io, std::string broker_addr, const uint32_t mqtt_port,
                   const char* client_id = nullptr, bool clean_session = true);

    ~MqttClientImpl();

    int publish_data(const char* topic, const void* buf, const int len, MqttQos qos, bool retain = false) override;

    int subscribe_topic(const char* topic) override;

    int unsubscribe_topic(const char* topic) override;

    int register_callback(MessageCallback callback_fn) override;

    void unregister_callback() override;

    int register_topic_callback(const std::string& topic, MessageCallback callback_fn) override;

    void unregister_topic_callback(const std::string& topic) override;

private:
    friend void on_connect(struct mosquitto* mosq, void* obj, int reason_code);
    friend void on_message(struct mosquitto* mosq, void* obj, const struct mosquitto_message* msg);
    friend void on_disconnect(struct mosquitto* mosq, void* data, int rc);
    friend void on_unsubscribe(struct mosquitto* mosq, void* obj, int mid);

    int setup_mqtt_communicator();

    void schedule_mqtt_rx();

    void on_mqtt_rx(const std::error_code& error_code);

    void schedule_mqtt_tx();

    void on_mqtt_tx(const std::error_code& error_code);

    void set_callbacks();

    static constexpr uint32_t CONNECTION_POLL_INTERVAL = 1000;
    void start_connection_timer();
    void connection_timer_handler();
    void on_disconnection_msg(struct mosquitto* mosq, void* data, int rc);
    static constexpr uint32_t MOSQUITTO_LOOP_MISC_POLL_INTERVAL = 1000;
    void start_loop_misc_timer();
    void loop_misc_timer_handler();

    boost::asio::io_context& _io_ctx;
    std::string _mqtt_broker_addr;

    const uint32_t _mqtt_port;
    bool _clean_session;
    const char* _client_id;

    boost::asio::posix::stream_descriptor _mqtt_socket;
    int _dev_mqtt_fd;
    struct mosquitto* _mosq;
    std::shared_ptr<asio::utils::Timer> _connection_status_timer;
    std::shared_ptr<asio::utils::Timer> _mosquitto_loop_misc_timer;
    std::vector<std::string> _subscribed_topics;
    std::function<void(const char* topic, const void* payload, int len)> _mqtt_data_received_cb;
    std::atomic_bool _connection_status = true;
    std::atomic_bool _reconnect_required = true;
    std::map<std::string, std::function<void(const char* topic, const void* payload, int len)>>
        _mqtt_topic_data_received_cb;
};

MqttClientImpl::MqttClientImpl(boost::asio::io_context& io, std::string broker_addr, const uint32_t mqtt_port,
                               const char* client_id, bool clean_session)
    : _io_ctx(io),
      _mqtt_broker_addr(broker_addr),
      _mqtt_port(mqtt_port),
      _clean_session(clean_session),
      _client_id(client_id),
      _mqtt_socket(io),
      _dev_mqtt_fd(-1) {
    mosquitto_lib_init();

    _mosq = mosquitto_new(_client_id, _clean_session, this);
    if (!_mosq) {
        throw std::system_error(errno, std::generic_category(),
                                fmt::format("Cannot create Mosquitto Client {}", client_id));
    }

    if (MOSQ_ERR_SUCCESS !=
        mosquitto_int_option(_mosq, MOSQ_OPT_PROTOCOL_VERSION, MQTT_PROTOCOL_V31)) {
        mosquitto_destroy(_mosq);
        throw std::system_error(
            errno, std::generic_category(),
            fmt::format("Cannot set to MQTT_PROTOCOL_V31 for Mosquitto Client {}", client_id));
    }

    if (MOSQ_ERR_SUCCESS != mosquitto_connect(_mosq, _mqtt_broker_addr.c_str(), _mqtt_port, 60)) {
        mosquitto_destroy(_mosq);
        throw std::system_error(
            errno, std::generic_category(),
            fmt::format("Connection refused to Mosquitto Client: {}", client_id));
    }

    set_callbacks();

    if (setup_mqtt_communicator()) {
        mosquitto_destroy(_mosq);
        throw std::system_error(
            errno, std::generic_category(),
            fmt::format("Cannot start the Mqtt communication to Mosquitto Client: {}", client_id));
    }

    start_loop_misc_timer();

    LOG_INFO(L_ASIOUTIL, "Mosquitto Client : {} started and configured to V31 protocol", client_id);
}

MqttClientImpl::~MqttClientImpl() {
    _reconnect_required = false;
    _mosquitto_loop_misc_timer->stop();
    mosquitto_disconnect(_mosq);
    mosquitto_destroy(_mosq);
    mosquitto_lib_cleanup();
}

int MqttClientImpl::publish_data(const char* topic, const void* buf, const int len, MqttQos qos, bool retain) {
    if (!topic || !buf || !len || qos < MqttQosMin || qos > MqttQosMax) {
        LOG_ERROR(L_ASIOUTIL, " [{}] Cannot publish the data, invalid parameters provided of length {} and Qos {}",
                  __func__, len, (int)qos);
        return -1;
    }

    int rc = mosquitto_publish(_mosq, nullptr, topic, len, buf, qos, retain);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERROR(L_ASIOUTIL, " [{}] Error in publishing {}", __func__, mosquitto_strerror(rc));
        return -1;
    }

    schedule_mqtt_tx();
    return 0;
}

int MqttClientImpl::subscribe_topic(const char* topic) {
    if (!topic) {
        LOG_ERROR(L_ASIOUTIL, "Invalid topic {}", topic);
        return -1;
    }

    int rc;
    
    rc = mosquitto_subscribe(_mosq, nullptr, topic, 1);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERROR(L_ASIOUTIL, " [{}] Error in subscribing to topic {}", topic, mosquitto_strerror(rc));
        return -1;
    }

    
    schedule_mqtt_tx();

    _subscribed_topics.emplace_back(topic);

    return 0;
}

int MqttClientImpl::unsubscribe_topic(const char* topic) {
    if (!topic) {
        LOG_ERROR(L_ASIOUTIL, "Invalid topic {}", topic);
        return -1;
    }

    int rc = mosquitto_unsubscribe(_mosq, nullptr, topic);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_ERROR(L_ASIOUTIL, " [{}] Error in subscribing to topic {}", topic, mosquitto_strerror(rc));
        return -1;
    }

    schedule_mqtt_tx();

    _subscribed_topics.erase(
        std::remove_if(
            std::begin(_subscribed_topics), std::end(_subscribed_topics),
            [&](const std::string& retreived_topic) { return (retreived_topic == topic); }),
        std::end(_subscribed_topics));

    return 0;
}

int MqttClientImpl::register_callback(
    std::function<void(const char* topic, const void* payload, int len)> callback_fn) {
    if (!callback_fn) {
        LOG_ERROR(L_ASIOUTIL, " [{}] Cannot register as callback function is nullptr", __func__);
        return EINVAL;
    }

    if (_mqtt_data_received_cb != nullptr) {
        LOG_ERROR(L_ASIOUTIL, " [{}] callback is already registered", __func__);
        return EALREADY;
    }

    _mqtt_data_received_cb = callback_fn;
    LOG_INFO(L_ASIOUTIL, "MqttRx Callback set");

    return 0;
}

void MqttClientImpl::unregister_callback() {
    _mqtt_data_received_cb = nullptr;
}

int MqttClientImpl::register_topic_callback(
    const std::string& topic, std::function<void(const char* topic, const void* payload, int len)> callback_fn) {

    if (!callback_fn) {
        LOG_ERROR(L_ASIOUTIL, " [{}] Cannot register as topic callback function is nullptr", __func__);
        return EINVAL;
    }

    if (_mqtt_topic_data_received_cb.find(topic) != std::end(_mqtt_topic_data_received_cb)) {
        LOG_ERROR(L_ASIOUTIL, " [{}] callback for topic {} is already registered", __func__, topic);
        return EALREADY;
    }

    _mqtt_topic_data_received_cb[topic] = std::move(callback_fn);

    return 0;
}

void MqttClientImpl::unregister_topic_callback(const std::string& topic) {
    _mqtt_topic_data_received_cb.erase(topic);
}

int MqttClientImpl::setup_mqtt_communicator() {
    
    _dev_mqtt_fd = mosquitto_socket(_mosq);

    if (-1 == _dev_mqtt_fd) {
        LOG_ERROR(L_ASIOUTIL, "Invalid Mosquitto Socket: {} ", _dev_mqtt_fd);
        return -1;
    }

    _mqtt_socket.assign(_dev_mqtt_fd);
    _mqtt_socket.non_blocking(true);
    
    LOG_INFO(L_ASIOUTIL, "[{}]Schedule async read for Mosquitto Socket number: {} ", __func__, _dev_mqtt_fd);

    schedule_mqtt_rx();

    return 0;
}

void MqttClientImpl::schedule_mqtt_rx() {
    _mqtt_socket.async_read_some(boost::asio::null_buffers(),
                                 std::bind(&MqttClientImpl::on_mqtt_rx, this, std::placeholders::_1));
}

void MqttClientImpl::on_mqtt_rx(const std::error_code& error_code) {
    LOG_TRACE(L_ASIOUTIL, "In [{}] ", __func__);
    if (_connection_status) {
    
        if (error_code) {
            LOG_ERROR(L_ASIOUTIL, "[{}] error {}: {}", __func__, error_code.value(), error_code.message());
        } else {
            auto ev = mosquitto_loop_read(_mosq, 1);
            if (MOSQ_ERR_SUCCESS == ev) {
                LOG_DEBUG(L_ASIOUTIL, "Moquitto Loop read Success .. ");
            } else {
                LOG_WARN(L_ASIOUTIL, "In [{}]Loop read failed with error code: {}", __func__, ev);
            }
        }
    
        schedule_mqtt_rx();
    } else {
        LOG_ERROR(L_ASIOUTIL, "In {}..mosquitto not connected", __func__);
    }
}

void MqttClientImpl::schedule_mqtt_tx() {
    _mqtt_socket.async_write_some(boost::asio::null_buffers(),
                                  std::bind(&MqttClientImpl::on_mqtt_tx, this, std::placeholders::_1));
}

void MqttClientImpl::on_mqtt_tx(const std::error_code& error_code) {
    LOG_DEBUG(L_ASIOUTIL, "In [{}] ", __func__);
    if (_connection_status) {
    
        if (error_code) {
            LOG_ERROR(L_ASIOUTIL, "[{}] error {}: {}", __func__, error_code.value(), error_code.message());
        } else {
            auto ev = mosquitto_loop_write(_mosq, 1);
            if (MOSQ_ERR_SUCCESS == ev) {
                LOG_DEBUG(L_ASIOUTIL, "Moquitto Loop write Success ..");
            } else {
                LOG_WARN(L_ASIOUTIL, "In [{}]Loop write failed with error code: {}", __func__, ev);
            }
        }
    } else {
        LOG_ERROR(L_ASIOUTIL, "In {}..mosquitto not connected", __func__);
    }
}

void MqttClientImpl::start_connection_timer() {
    TimerConfig timer_config;
    timer_config.name = std::string("connection_timer");
    timer_config.start_interval_msec = std::chrono::milliseconds(CONNECTION_POLL_INTERVAL);
    timer_config.periodic_interval_msec = std::chrono::milliseconds(CONNECTION_POLL_INTERVAL);
    timer_config.callback_fn = [&]() { connection_timer_handler(); };
    _connection_status_timer = Timer::create(timer_config, _io_ctx);
    _connection_status_timer->start();
}

void MqttClientImpl::connection_timer_handler() {
    LOG_TRACE(L_ASIOUTIL, "In [{}] ", __func__);
    if (!_connection_status) {
        if (MOSQ_ERR_SUCCESS != mosquitto_connect(_mosq, _mqtt_broker_addr.c_str(), _mqtt_port, 60)) {
            LOG_ERROR(L_ASIOUTIL, "Cannot connect to mosquitto broker..try again");
        } else {
            int rc = MOSQ_ERR_UNKNOWN;
            for (auto& topic : _subscribed_topics) {
                rc = mosquitto_subscribe(_mosq, nullptr, topic.c_str(), 1);
                if (rc != MOSQ_ERR_SUCCESS) {
                    LOG_ERROR(L_ASIOUTIL, " [{}] Error in subscribing to topic {}", topic, mosquitto_strerror(rc));
                }
            }
            if (rc == MOSQ_ERR_SUCCESS) {
                setup_mqtt_communicator();
                _connection_status = true;
                _connection_status_timer->stop();
            }
        }
    } else {
        LOG_INFO(L_ASIOUTIL, "Connected to mosquitto broker");
        _connection_status_timer->stop();
    }
}

void MqttClientImpl::start_loop_misc_timer() {
    TimerConfig timer_config;
    timer_config.name = std::string("mosquitto_loop_misc_timer");
    timer_config.start_interval_msec = std::chrono::milliseconds(MOSQUITTO_LOOP_MISC_POLL_INTERVAL);
    timer_config.periodic_interval_msec = std::chrono::milliseconds(
        MOSQUITTO_LOOP_MISC_POLL_INTERVAL);
    timer_config.callback_fn = [&]() { loop_misc_timer_handler(); };
    _mosquitto_loop_misc_timer = Timer::create(timer_config, _io_ctx);
    _mosquitto_loop_misc_timer->start();
}

void MqttClientImpl::loop_misc_timer_handler() {
    LOG_TRACE(L_ASIOUTIL, "In [{}] ", __func__);
    if (MOSQ_ERR_SUCCESS == mosquitto_loop_misc(_mosq)) {
        LOG_DEBUG(L_ASIOUTIL, "In [{}] mosquitto_loop_misc SUCCESS", __func__);
    } else {
        LOG_WARN(L_ASIOUTIL, "In [{}] mosquitto_loop_misc FAILS... looks like mosquitto broker is not running",
                 __func__);
    }
}

void on_connect(struct mosquitto* mosq, void* obj, int reason_code) {
    auto* self = (asio::utils::MqttClientImpl*)obj;
    if (reason_code != 0) {
        mosquitto_disconnect(mosq);
        return;
    }
    LOG_DEBUG(L_ASIOUTIL, "on connect code {}", reason_code);
    self->_connection_status = true;
}

void on_disconnect(struct mosquitto* mosq, void* obj, int rc) {
    auto* self = (asio::utils::MqttClientImpl*)obj;

    self->_connection_status = false;
    self->on_disconnection_msg(mosq, obj, rc);
}

static void on_publish(struct mosquitto* mosq, void* obj, int mid) {
    // TODO
}

void on_message(struct mosquitto* mosq, void* obj, const struct mosquitto_message* msg) {
    auto* self = (asio::utils::MqttClientImpl*)obj;

    if (auto it = self->_mqtt_topic_data_received_cb.find(msg->topic);
        it != std::end(self->_mqtt_topic_data_received_cb)) {
        (it->second)(msg->topic, msg->payload, msg->payloadlen);
    } else if (self->_mqtt_data_received_cb) {
        self->_mqtt_data_received_cb(msg->topic, msg->payload, msg->payloadlen);
    }
}

static void
on_subscribe(struct mosquitto* mosq, void* obj, int mid, int qos_count, const int* granted_qos)
{
    // TODO
}

void on_unsubscribe(struct mosquitto* mosq, void* obj, int mid) {
    auto* self = (asio::utils::MqttClientImpl*)obj;
    LOG_DEBUG(L_ASIOUTIL, "on_unsubscribe");
}

static void
on_log(struct mosquitto* mosq, void* userdata, int level, const char* str)
{
    // TODO
}

void MqttClientImpl::set_callbacks() {
    mosquitto_connect_callback_set(_mosq, on_connect);
    mosquitto_publish_callback_set(_mosq, on_publish);
    mosquitto_disconnect_callback_set(_mosq, on_disconnect);
    mosquitto_message_callback_set(_mosq, on_message);
    mosquitto_subscribe_callback_set(_mosq, on_subscribe);
    mosquitto_unsubscribe_callback_set(_mosq, on_unsubscribe);
    mosquitto_log_callback_set(_mosq, on_log);
}

void MqttClientImpl::on_disconnection_msg(struct mosquitto* mosq, void* data, int rc) {
    LOG_INFO(L_ASIOUTIL, "In [{}] ", __func__);
    if (!_connection_status) {
        _mqtt_socket.release();

        if (_reconnect_required) {
            start_connection_timer();
        }
    }
}

std::unique_ptr<MqttClient> MqttClient::create_with_address(boost::asio::io_context& io, const std::string& broker_addr,
                                                            const uint32_t mqtt_port, const char* client_id,
                                                            bool clean_session) {
    return std::make_unique<MqttClientImpl>(io, broker_addr, mqtt_port, client_id, clean_session);
}

std::unique_ptr<MqttClient> MqttClient::create(boost::asio::io_context& io, const char* client_id, bool clean_session) {

    std::string address = DEFAULT_MQTT_BROKER_ADDRESS;
    uint32_t port       = DEFAULT_MQTT_BROKER_PORT;

    return create_with_address(io, address, port, client_id, clean_session);
}

}
