#ifndef _UTILS_STRINGUTIL_HPP_
#define _UTILS_STRINGUTIL_HPP_

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace asio::utils::stringUtil {

void ltrim(std::string& s);
void rtrim(std::string& s);
void trim(std::string& s);
std::vector<std::string> split(const std::string& s, char delimiter, bool skip_empty_tokens = true);
bool safe_stoi(const std::string& string, int& number);
int safe_stoi_default(const std::string& string, int default_value);
void clear(std::stringstream& buf);
std::optional<std::pair<std::string, uint32_t>> split_url_into_address_and_port(std::string url);

}

#endif
