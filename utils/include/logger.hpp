#pragma once

#include <fmt/format.h>
#include <optional>
#include <set>
#include <string>


namespace asio::logger {

using CategoryType = uint64_t;

enum class LogLevel : uint8_t {

    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    CRITICAL,
    OFF,
    LEVEL_COUNT = OFF
};

#define DECLARE_LOG_CATEGORY(identifier) extern const asio::logger::CategoryType identifier;
#define DEFINE_LOG_CATEGORY(identifier, name) // TODO

#define LOG_LEVEL(categories, level, fmt_str, ...) log(level, categories, __FILE__, __LINE__, fmt::format(FMT_STRING(fmt_str) __VA_OPT__(,) __VA_ARGS__))
#define LOG_TRACE(categories, ...) LOG_LEVEL(categories, asio::logger::LogLevel::TRACE, __VA_ARGS__)
#define LOG_DEBUG(categories, ...) LOG_LEVEL(categories, asio::logger::LogLevel::DEBUG, __VA_ARGS__)
#define LOG_INFO(categories, ...) LOG_LEVEL(categories, asio::logger::LogLevel::INFO, __VA_ARGS__)
#define LOG_WARN(categories, ...) LOG_LEVEL(categories, asio::logger::LogLevel::WARN, __VA_ARGS__)
#define LOG_ERROR(categories, ...) LOG_LEVEL(categories, asio::logger::LogLevel::ERROR, __VA_ARGS__)
#define LOG_CRITICAL(categories, ...) LOG_LEVEL(categories, asio::logger::LogLevel::CRITICAL, __VA_ARGS__)
#define LOG_HEX(categories, level, title, data, len) logHex(level, categories, __FILE__, __LINE__, title, data, len)


void log(LogLevel level, asio::logger::CategoryType categories, const char* filename, unsigned int line_no,
         const std::string& msg);

void logHex(LogLevel level, asio::logger::CategoryType categories, const char* filename, unsigned int line_no,
            const std::string& title, const void* data, size_t len);

}

#pragma once
DECLARE_LOG_CATEGORY(L_ASIOUTIL);
DEFINE_LOG_CATEGORY(L_ASIOUTIL, "AsioUtil");
