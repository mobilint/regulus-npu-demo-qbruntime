#ifndef CORE_ARGPARSE_H_
#define CORE_ARGPARSE_H_

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct ParseResult {
    std::vector<std::string> dev_paths;
    uint32_t camera_width;
    uint32_t camera_heigth;
    uint32_t camera_fps;
};

struct ParseResult_udp {
    std::vector<std::string> dev_paths;
    uint32_t camera_width;
    uint32_t camera_heigth;
    uint32_t camera_fps;
    const char* ip;
    int port;
    int bandwidth;
};

struct VisitCount {
    bool visited = false;
    int count = 0;
};

ParseResult parse_args(int argc, char* argv[], int dvec_num, void (*usage_info_fn)());
ParseResult_udp parse_args_udp(int argc, char* argv[], int dvec_num,
                               void (*usage_info_fn)());
ParseResult parse_args_mipi(int argc, char* argv[], int dvec_num,
                            void (*usage_info_fn)());

#endif
