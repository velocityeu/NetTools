#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
namespace veu::storage {
struct Plan { unsigned active_tool=0; std::map<std::string,std::string> fields; };
std::string encode(const Plan&);
Plan decode(std::string_view);
std::string csv_cell(std::string_view);
std::string read(const std::filesystem::path&);
void safe_save(const std::filesystem::path&, std::string_view);
}
