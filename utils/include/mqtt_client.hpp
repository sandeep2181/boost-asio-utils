/**
 * @file mqtt_client.hpp
 * Header file for MQTT Client
 *
 */

#ifndef MQTT_CLIENT_HPP
#define MQTT_CLIENT_HPP

#include "async_io_context.hpp"
#include "timer.hpp"
#include <cstdlib>
#include <memory>
#include <mosquitto.h>
#include <time.h>

namespace asio::utils {

/**
 * An enumeration representing Quality Of Service
 */
typedef enum _MqttQos {
    MqttQos0   = 0,         // Fastest speed and lowest quality of service
    MqttQos1   = 1,         // Medium speed and quality of service
    MqttQos2   = 2,         // Slowest speed and highest quality of service
    MqttQosMin = MqttQos0,  // Minimum Qos supported
    MqttQosMax = MqttQos2   // Maximum Qos supported
} MqttQos;

/*
 * Class: MqttClient
 */

class MqttClient {
public:
    static std::unique_ptr<MqttClient> create(boost::asio::io_context& io, const char* client_id = nullptr,
                                              bool clean_session = true);

    static std::unique_ptr<MqttClient> create_with_address(boost::asio::io_context& io, const std::string& broker_addr,
                                                           const uint32_t mqtt_port, const char* client_id = nullptr,
                                                           bool clean_session = true);

    virtual ~MqttClient() = default;

    using MessageCallback = std::function<void(const char* topic, const void* payload, int len)>;

    virtual int publish_data(const char* topic, const void* buf, const int len, MqttQos qos, bool retain = false) = 0;

    virtual int subscribe_topic(const char* topic) = 0;

    virtual int unsubscribe_topic(const char* topic) = 0;

    virtual int register_callback(MessageCallback callback_fn) = 0;

    virtual void unregister_callback() = 0;

    virtual int register_topic_callback(const std::string& topic, MessageCallback callback_fn) = 0;

    virtual void unregister_topic_callback(const std::string& topic) = 0;
};

}

#endif 
