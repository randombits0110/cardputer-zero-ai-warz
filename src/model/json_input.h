#pragma once

#include "economy.h"

#include <nlohmann/json.hpp>

#include <array>
#include <fstream>
#include <stdexcept>

namespace wars {

using Json = nlohmann::json;

namespace json_input {

inline constexpr std::size_t economy_bytes = 4 * 1024 * 1024;
inline constexpr std::size_t profile_bytes = 5 * 1024 * 1024;

class InvalidDocument : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline std::string read(const std::filesystem::path& path, std::size_t limit) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("Expected a regular JSON file: " + path.string());
    }
    if (std::filesystem::file_size(path) > limit) {
        throw InvalidDocument("JSON file exceeds its size limit");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open JSON file: " + path.string());

    std::string document;
    char chunk[4096];
    while (input.read(chunk, sizeof(chunk)) || input.gcount() != 0) {
        const auto size = static_cast<std::size_t>(input.gcount());
        if (size > limit - document.size()) throw InvalidDocument("JSON file exceeds its size limit");
        document.append(chunk, size);
    }
    if (input.bad() || !input.eof()) throw std::runtime_error("Cannot read JSON file: " + path.string());
    return document;
}

inline Json parse(const std::string& document, std::size_t limit) {
    if (document.size() > limit || document.find('\0') != std::string::npos) {
        throw InvalidDocument("Invalid JSON file size or encoding");
    }
    constexpr int max_depth = 16;
    std::array<std::size_t, max_depth + 1> members{};
    std::size_t events = 0;
    return Json::parse(document, [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > max_depth || ++events > 50'000) {
            throw InvalidDocument("JSON structure exceeds its limits");
        }
        const bool container = event == Json::parse_event_t::array_start
            || event == Json::parse_event_t::object_start;
        if (depth > 0 && (container || event == Json::parse_event_t::value)
            && ++members[depth - 1] > 1024) {
            throw InvalidDocument("JSON collection exceeds its size limit");
        }
        if (container) members[depth] = 0;
        if ((event == Json::parse_event_t::key || event == Json::parse_event_t::value)
            && value.is_string() && value.get_ref<const std::string&>().size() > 1024) {
            throw InvalidDocument("JSON string exceeds its size limit");
        }
        return true;
    });
}

} // namespace json_input

// Take ownership of the JSON tree to avoid serializing and reparsing an embedded economy.
Economy decode_economy(Json data);

} // namespace wars
