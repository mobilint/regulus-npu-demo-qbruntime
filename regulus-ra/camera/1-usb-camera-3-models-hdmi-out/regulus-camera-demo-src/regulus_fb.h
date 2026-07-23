#ifndef REGULUS_FRAMEBUFFER_H
#define REGULUS_FRAMEBUFFER_H

#include <linux/fb.h>

#include <cstdint>
#include <cstdlib>
#include <opencv2/core.hpp>

enum class RegulusDisplayType { LCD, HDMI };

class RegulusFramebuffer {
public:
    RegulusFramebuffer() = delete;
    RegulusFramebuffer(RegulusDisplayType display_type);
    ~RegulusFramebuffer();

    int draw_frame(cv::Mat frame, int fb_offset_x, int fb_offset_y);
    cv::Mat get_display_mat();

private:
    struct fb_var_screeninfo v_screen_info;
    uint32_t screen_xres;
    uint32_t screen_yres;

    void* fb_bgrx = NULL;
    size_t fb_bgrx_size = 0;
    cv::Mat fb_bgrx_mat;

    int fd;
};

#endif
