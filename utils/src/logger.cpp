#include "logger.hpp"

#include <algorithm>
#include <cassert>

using namespace asio::logger;

static const char* levelNames[] = {"trace", "debug", "info", "warn", "error", "critical", "off"};


void asio::logger::log(LogLevel level, CategoryType categories, const char* filename, unsigned int line_no,
                       const std::string& msg) {
}


void asio::logger::logHex(LogLevel level, CategoryType categories, const char* filename, unsigned int line_no,
                          const std::string& title, const void* data, size_t len) {
}
