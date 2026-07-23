#include "app_parse_argv.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

ParseResult parse_args(int argc, char* argv[], int dvec_num, void (*usage_info_fn)()) {
    std::vector<std::string> dev_paths;
    uint32_t camera_width = 640, camera_height = 480, camera_fps = 30;

    std::unordered_map<std::string, VisitCount> option_visit_counts{
        {"-d", {}}, {"-w", {}}, {"-h", {}}, {"-f", {}}};

    enum class error_type { WRONG_USAGE };

    std::string state;

    try {
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];

            if (option_visit_counts.contains(arg)) {
                if (option_visit_counts[arg].visited == true) {
                    // error: duplicated -option visit
                    throw error_type::WRONG_USAGE;
                } else {
                    option_visit_counts[arg].visited = true;
                    state = arg;
                }
            } else {
                if (state == std::string("-d")) {
                    dev_paths.push_back(std::string("/dev/video") + std::string(argv[i]));
                } else if (state == std::string("-w")) {
                    camera_width = atoi(argv[i]);
                } else if (state == std::string("-h")) {
                    camera_height = atoi(argv[i]);
                } else if (state == std::string("-f")) {
                    camera_fps = atoi(argv[i]);
                } else {
                    throw error_type::WRONG_USAGE;
                }
                option_visit_counts[state].count++;
            }
        }

        if (option_visit_counts[std::string("-d")].count != dvec_num) {
            throw error_type::WRONG_USAGE;
        }

    } catch (error_type e) {
        usage_info_fn();
        exit(1);
    }

    return {dev_paths, camera_width, camera_height, camera_fps};
}

ParseResult_udp parse_args_udp(int argc, char* argv[], int dvec_num,
                               void (*usage_info_fn)()) {
    std::vector<std::string> dev_paths;
    uint32_t camera_width = 640, camera_height = 480, camera_fps = 30;
    char* ip;
    int port = 20741;
    int bandwidth = 16384;

    std::unordered_map<std::string, VisitCount> option_visit_counts{
        {"-d", {}},  {"-w", {}},    {"-h", {}},        {"-f", {}},
        {"-ip", {}}, {"-port", {}}, {"-bandwidth", {}}};

    enum class error_type { WRONG_USAGE };

    std::string state;

    try {
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];

            if (option_visit_counts.contains(arg)) {
                if (option_visit_counts[arg].visited == true) {
                    // error: duplicated -option visit
                    throw error_type::WRONG_USAGE;
                } else {
                    option_visit_counts[arg].visited = true;
                    state = arg;
                }
            } else {
                if (state == std::string("-d")) {
                    dev_paths.push_back(std::string("/dev/video") + std::string(argv[i]));
                } else if (state == std::string("-w")) {
                    camera_width = atoi(argv[i]);
                } else if (state == std::string("-h")) {
                    camera_height = atoi(argv[i]);
                } else if (state == std::string("-f")) {
                    camera_fps = atoi(argv[i]);
                } else if (state == std::string("-ip")) {
                    ip = argv[i];
                } else if (state == std::string("-port")) {
                    port = atoi(argv[i]);
                } else if (state == std::string("-bandwidth")) {
                    bandwidth = atoi(argv[i]);
                } else {
                    throw error_type::WRONG_USAGE;
                }
                option_visit_counts[state].count++;
            }
        }

        if (option_visit_counts[std::string("-d")].count != dvec_num) {
            throw error_type::WRONG_USAGE;
        }

    } catch (error_type e) {
        usage_info_fn();
        exit(1);
    }

    return {dev_paths, camera_width, camera_height, camera_fps, ip, port, bandwidth};
}
