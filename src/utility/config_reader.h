#pragma once

#include <fstream>
#include <string>
#include <nlohmann/json.hpp>
#include "logger.h"

struct general_config
{
    std::string app_name;
    std::string working_directory;
    std::string asset_directory;
};

class config_reader
{
private:
    nlohmann::json config_json;
public:
    config_reader(const config_reader&)            = default;
    config_reader(config_reader&&)                 = delete;
    config_reader& operator=(const config_reader&) = default;
    config_reader& operator=(config_reader&&)      = delete;
    config_reader(const std::string& config_file_path);
    ~config_reader();

    bool try_parse_general_config(general_config& result);
};

inline config_reader::config_reader(const std::string& config_file_path)
{
    // Open the file
    std::ifstream config_file(config_file_path);
    if (!config_file.is_open())
    {
        Logger::LogError("Failed to open config file: " + config_file_path);
    }

    // Read the json file
    try
    {
        config_file >> config_json;
    }
    catch (const nlohmann::json::parse_error& e)
    {
        Logger::LogError("Failed to parse config file: " + std::string(e.what()));
        return;
    }

    // Close the file
    config_file.close();
}

inline config_reader::~config_reader()
{
}

/// @brief 尝试获取配置文件中的一般配置项
/// @param result 返回的配置项
/// @return 解析成功返回true，失败返回false
inline bool config_reader::try_parse_general_config(general_config& result)
{
    try
    {
        // Example: Accessing a value from the JSON object
        result.app_name = config_json["general"]["string"]["app_name"];
        result.working_directory = config_json["general"]["string"]["working_directory"];
        result.asset_directory = config_json["general"]["string"]["asset_directory"];
    }
    catch (const nlohmann::json::exception& e)
    {
        Logger::LogError("Failed to access config values: " + std::string(e.what()));
        return false;
    }
    return true;
}