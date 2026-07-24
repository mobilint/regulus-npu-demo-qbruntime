#include "regulus_fb.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <iostream>

RegulusFramebuffer::RegulusFramebuffer(RegulusDisplayType display_type) {
    fd = open("/dev/fb0", O_RDWR);
    if (fd == -1) {
        perror("Cannot open framebuffer device");
        exit(EXIT_FAILURE);
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &v_screen_info) == -1) {
        perror("ioctl");
        exit(EXIT_FAILURE);
    }

    std::cout << "Framebuffer info: " << v_screen_info.xres_virtual << "x"
              << v_screen_info.yres_virtual << " bpp: " << v_screen_info.bits_per_pixel
              << std::endl;

    screen_xres = v_screen_info.xres_virtual;
    screen_yres = v_screen_info.yres_virtual;

    fb_bgrx_size = v_screen_info.yres_virtual * v_screen_info.xres_virtual *
                   v_screen_info.bits_per_pixel / 8;

    fb_bgrx = mmap(NULL, fb_bgrx_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb_bgrx == MAP_FAILED) {
        perror("mmap framebuffer");
        exit(EXIT_FAILURE);
    }

    fb_bgrx_mat = cv::Mat(screen_yres, screen_xres, CV_8UC4, (uint8_t*)fb_bgrx);
}

RegulusFramebuffer::~RegulusFramebuffer() {
    munmap(fb_bgrx, fb_bgrx_size);
    close(fd);
}

static inline auto get_draw_frame_vars(const int frame_res, const int screen_res,
                                       const int fb_v_offset) {
    struct {
        int fb_offset;
        int frame_offset;
        int draw_res;
    } _;

    if (fb_v_offset >= 0) {
        _.frame_offset = 0;
        if (fb_v_offset < screen_res) {
            _.fb_offset = fb_v_offset;
            if (fb_v_offset + frame_res <= screen_res) {
                _.draw_res = frame_res;
            } else {
                _.draw_res = screen_res - fb_v_offset;
            }
        } else {
            _.fb_offset = 0;
            _.draw_res = 0;
        }
    } else {
        _.fb_offset = 0;
        if (-fb_v_offset < frame_res) {
            _.frame_offset = -fb_v_offset;
            _.draw_res = frame_res + fb_v_offset;
        } else {
            _.frame_offset = 0;
            _.draw_res = 0;
        }
    }

    return _;
}

int RegulusFramebuffer::draw_frame(cv::Mat frame, int fb_v_offset_x, int fb_v_offset_y) {
    int frame_width = frame.cols;
    int frame_height = frame.rows;
    int frame_channels = frame.channels();

    uint32_t fb_row_stride = screen_xres * 4;
    uint32_t frame_row_stride = frame_width * frame_channels;

    auto [fb_offset_x, frame_offset_x, draw_width] =
        get_draw_frame_vars(frame_width, screen_xres, fb_v_offset_x);
    auto [fb_offset_y, frame_offset_y, draw_height] =
        get_draw_frame_vars(frame_height, screen_yres, fb_v_offset_y);

    if (frame_channels == 3) {  // BGR
        for (uint32_t frame_y = 0; frame_y < frame_height; frame_y++) {
            for (uint32_t frame_x = 0; frame_x < frame_width; frame_x++) {
                size_t fb_pos =
                    (fb_offset_y + frame_y) * fb_row_stride + (frame_x + fb_offset_x) * 4;
                size_t frame_pos = frame_y * frame_row_stride + frame_x * 3;

                *(unsigned int*)((uint8_t*)fb_bgrx + fb_pos) =
                    (0xFF << 24) | (frame.data[frame_pos + 2] << 16) |
                    (frame.data[frame_pos + 1] << 8) | (frame.data[frame_pos]);
            }
        }
    } else if (frame_channels == 4) {  // BGRA
        for (uint32_t frame_y = 0; frame_y < frame_height; frame_y++) {
            size_t fb_pos = (fb_offset_y + frame_y) * fb_row_stride + fb_offset_x * 4;
            size_t frame_pos = frame_y * frame_row_stride;

            memcpy((uint8_t*)fb_bgrx + fb_pos, (uint8_t*)frame.data + frame_pos,
                   frame_row_stride);
        }
    } else {
        std::cerr << "Unsupported number of channels: " << frame_channels << std::endl;
        return -1;
    }

    return 0;
}

cv::Mat RegulusFramebuffer::get_display_mat() { return fb_bgrx_mat; }
