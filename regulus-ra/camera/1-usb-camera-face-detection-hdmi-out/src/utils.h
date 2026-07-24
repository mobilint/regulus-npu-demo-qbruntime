#pragma once

#include <string>

struct YamlConfig {
    std::string camera_device = "";
    int camera_width = 1280;
    int camera_height = 720;
    int camera_fps = 30;
    int display_width = 1920;
    int display_height = 1080;
};

bool loadYamlConfig(const std::string& yaml_path, YamlConfig& cfg);
std::string findVideoDevice();