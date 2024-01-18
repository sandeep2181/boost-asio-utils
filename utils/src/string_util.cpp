#include "string_util.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <sstream>

using namespace asio::utils;

namespace asio::utils::stringUtil {

void ltrim(std::string& s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
}

void rtrim(std::string& s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(),
            s.end());
}

void trim(std::string& s)
{
    stringUtil::ltrim(s);
    stringUtil::rtrim(s);
}

template <typename Out>
void split(const std::string& s, char delim, Out result)
{
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        *(result++) = item;
    }
}

std::vector<std::string> split(const std::string& s, char delimiter, bool skip_empty_tokens)
{
    std::vector<std::string> ret;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        if (!skip_empty_tokens || !item.empty()) {
            ret.push_back(item);
        }
    }
    return ret;
}

bool safe_stoi(const std::string& string, int& number)
{
    bool success = false;
    try {
        number  = std::stoi(string);
        success = true;
    }
    catch (...) {
    }
    return success;
}

int safe_stoi_default(const std::string& string, int default_value)
{
    int number = default_value;
    stringUtil::safe_stoi(string, number);
    return number;
}

void clear(std::stringstream& buf)
{
    std::stringstream().swap(buf);
}

std::optional<std::pair<std::string, uint32_t>> split_url_into_address_and_port(std::string url) {
    std::string port_delimiter = ":";
    size_t delimiter_position  = url.find(port_delimiter);
    // Non compliant with url format
    if (delimiter_position == std::string::npos) {
        return std::nullopt;
    }

    std::string address     = url.substr(0, delimiter_position);
    std::string port_string = url.substr(delimiter_position + 1, url.length() - delimiter_position);

    uint32_t port     = 0;
    const char* first = port_string.data();
    const char* last  = port_string.data() + port_string.size();
    auto [ptr, ec]    = std::from_chars(first, last, port);

    if (address.length() && ec == std::errc() && ptr == last) {
        return std::make_pair(address, port);
    } else {
        return std::nullopt;
    }
}

}
