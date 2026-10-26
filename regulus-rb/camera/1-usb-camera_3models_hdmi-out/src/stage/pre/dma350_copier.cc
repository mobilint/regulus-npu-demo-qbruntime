#include "dma350_copier.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <string>

namespace app {
namespace {
constexpr int OUT = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;   // source (camera)
constexpr int CAP = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;  // dest (CMA ring)

// The dma350 /dev/video node number varies per boot, so find it by driver name at
// runtime.
std::string find_dma350_node() {
    for (int n = 0; n < 64; n++) {
        std::string path = "/dev/video" + std::to_string(n);
        int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        v4l2_capability cap{};
        bool match = (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) &&
                     (strcmp(reinterpret_cast<const char*>(cap.driver), "dma350") == 0);
        close(fd);
        if (match) return path;
    }
    return "";
}

int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do {
        r = ioctl(fd, req, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}
enum class WaitStatus { Ready, Timeout, Error };

WaitStatus wait_ready(int fd, short event) {
    pollfd pfd{fd, event, 0};
    int result;
    do {
        result = poll(&pfd, 1, 500);
    } while (result < 0 && errno == EINTR);
    if (result == 0) return WaitStatus::Timeout;
    if (result < 0) return WaitStatus::Error;
    if ((pfd.revents & event) == 0) {
        errno = EIO;
        return WaitStatus::Error;
    }
    return WaitStatus::Ready;
}
// 3 bytes/px is RGB24 (R,G,B); 4 is 'XB24' = V4L2 RGBX32 (R,G,B,unused). Same channel
// order either way, so switching between them converts nothing -- only the stride
// changes.
unsigned int pix_fmt(int bpp) {
    return bpp == 4 ? v4l2_fourcc('X', 'B', '2', '4') : V4L2_PIX_FMT_RGB24;
}
bool set_fmt(int fd, int type, int w, int h, int bpp) {
    v4l2_format f{};
    f.type = type;
    f.fmt.pix_mp.width = w;
    f.fmt.pix_mp.height = h;
    f.fmt.pix_mp.pixelformat = pix_fmt(bpp);
    f.fmt.pix_mp.field = V4L2_FIELD_NONE;
    f.fmt.pix_mp.num_planes = 1;
    return xioctl(fd, VIDIOC_S_FMT, &f) == 0;
}
int cma_alloc(size_t bytes) {
    int heap = open("/dev/dma_heap/linux,cma", O_RDWR);
    if (heap < 0) return -1;
    dma_heap_allocation_data ad{};
    ad.len = bytes;
    ad.fd_flags = O_RDWR | O_CLOEXEC;
    int r = xioctl(heap, DMA_HEAP_IOCTL_ALLOC, &ad);
    close(heap);
    return r < 0 ? -1 : ad.fd;
}
}  // namespace

Dma350Copier::Dma350Copier(int src_w, int src_h, int dst_w, int dst_h, int crop_x,
                           int crop_y, int bpp, int ring)
    : ring_(ring > kMaxRing ? kMaxRing : ring),
      bpp_(bpp),
      src_bytes_((size_t)src_w * src_h * bpp),
      dst_bytes_((size_t)dst_w * dst_h * bpp) {
    std::string node = find_dma350_node();
    if (node.empty()) {
        fprintf(stderr, "[dma350] node not found (driver 'dma350')\n");
        return;
    }
    fd_ = open(node.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        perror("[dma350] open");
        return;
    }
    if (!set_fmt(fd_, OUT, src_w, src_h, bpp_) ||
        !set_fmt(fd_, CAP, dst_w, dst_h, bpp_)) {
        fprintf(stderr, "[dma350] S_FMT failed\n");
        return;
    }
    // allocate CMA output ring buffers
    for (int i = 0; i < ring_; i++) {
        out_fd_[i] = cma_alloc(dst_bytes_);
        if (out_fd_[i] < 0) {
            perror("[dma350] cma_alloc");
            return;
        }
        out_map_[i] =
            mmap(nullptr, dst_bytes_, PROT_READ | PROT_WRITE, MAP_SHARED, out_fd_[i], 0);
        if (out_map_[i] == MAP_FAILED) {
            perror("[dma350] mmap out");
            out_map_[i] = nullptr;
            return;
        }
    }
    v4l2_requestbuffers rb{};
    rb.count = 1;
    rb.type = OUT;
    rb.memory = V4L2_MEMORY_DMABUF;
    if (xioctl(fd_, VIDIOC_REQBUFS, &rb) < 0) {
        perror("[dma350] REQBUFS out");
        return;
    }
    rb = {};
    rb.count = ring_;
    rb.type = CAP;
    rb.memory = V4L2_MEMORY_DMABUF;
    if (xioctl(fd_, VIDIOC_REQBUFS, &rb) < 0) {
        perror("[dma350] REQBUFS cap");
        return;
    }

    if (xioctl(fd_, VIDIOC_STREAMON, (void*)&OUT) < 0) {
        perror("[dma350] STREAMON out");
        return;
    }
    if (xioctl(fd_, VIDIOC_STREAMON, (void*)&CAP) < 0) {
        perror("[dma350] STREAMON cap");
        return;
    }
    streaming_ = true;

    // Take only the crop window. Set after STREAMON, which is the order the compose path
    // already uses. left/top come back rounded down to a multiple of 2, so a mismatch is
    // reported rather than silently shifting the picture.
    v4l2_selection sel{};
    sel.type = OUT;
    sel.target = V4L2_SEL_TGT_CROP;
    sel.r.left = crop_x;
    sel.r.top = crop_y;
    sel.r.width = dst_w;
    sel.r.height = dst_h;
    if (xioctl(fd_, VIDIOC_S_SELECTION, &sel) < 0) {
        perror("[dma350] S_SELECTION crop");
        return;
    }
    if (sel.r.left != (unsigned)crop_x || sel.r.top != (unsigned)crop_y ||
        sel.r.width != (unsigned)dst_w || sel.r.height != (unsigned)dst_h) {
        fprintf(stderr, "[dma350] crop (%d,%d %dx%d) granted as (%u,%u %ux%u)\n", crop_x,
                crop_y, dst_w, dst_h, sel.r.left, sel.r.top, sel.r.width, sel.r.height);
        return;
    }
    ok_ = true;
    printf("[dma350] copier ready (%dx%d -> %dx%d at (%d,%d) %s, CMA ring=%d)\n", src_w,
           src_h, dst_w, dst_h, crop_x, crop_y, bpp_ == 4 ? "RGBX" : "RGB24", ring_);
}

Dma350Copier::~Dma350Copier() {
    if (fd_ >= 0) {
        if (streaming_) {
            xioctl(fd_, VIDIOC_STREAMOFF, (void*)&OUT);
            xioctl(fd_, VIDIOC_STREAMOFF, (void*)&CAP);
        }
        close(fd_);
    }
    for (int i = 0; i < ring_; i++) {
        if (out_map_[i]) munmap(out_map_[i], dst_bytes_);
        if (out_fd_[i] > 0) close(out_fd_[i]);
    }
}

Dma350Copier::Frame Dma350Copier::copy(int camera_dmabuf_fd) {
    if (!ok_) return {};
    const int idx = next_;
    next_ = (next_ + 1) % ring_;

    v4l2_plane ip{};
    v4l2_buffer ib{};
    ib.type = OUT;
    ib.memory = V4L2_MEMORY_DMABUF;
    ib.index = 0;
    ib.length = 1;
    ib.m.planes = &ip;
    ip.m.fd = camera_dmabuf_fd;
    ip.bytesused = src_bytes_;
    ip.length = src_bytes_;
    if (xioctl(fd_, VIDIOC_QBUF, &ib) < 0) {
        perror("[dma350] QBUF in");
        return {};
    }

    v4l2_plane op{};
    v4l2_buffer ob{};
    ob.type = CAP;
    ob.memory = V4L2_MEMORY_DMABUF;
    ob.index = idx;
    ob.length = 1;
    ob.m.planes = &op;
    op.m.fd = out_fd_[idx];
    op.length = dst_bytes_;
    if (xioctl(fd_, VIDIOC_QBUF, &ob) < 0) {
        perror("[dma350] QBUF out");
        return {};
    }

    v4l2_plane dip{};
    v4l2_buffer dib{};
    dib.type = OUT;
    dib.memory = V4L2_MEMORY_DMABUF;
    dib.length = 1;
    dib.m.planes = &dip;
    const WaitStatus input_ready = wait_ready(fd_, POLLOUT);
    if (input_ready == WaitStatus::Timeout) {
        fprintf(stderr, "[dma350] DQBUF in timed out\n");
        return {};
    }
    if (input_ready == WaitStatus::Error) {
        perror("[dma350] poll in");
        return {};
    }
    if (xioctl(fd_, VIDIOC_DQBUF, &dib) < 0) {
        perror("[dma350] DQBUF in");
        return {};
    }

    v4l2_plane dop{};
    v4l2_buffer dob{};
    dob.type = CAP;
    dob.memory = V4L2_MEMORY_DMABUF;
    dob.length = 1;
    dob.m.planes = &dop;
    const WaitStatus output_ready = wait_ready(fd_, POLLIN);
    if (output_ready == WaitStatus::Timeout) {
        fprintf(stderr, "[dma350] DQBUF out timed out\n");
        return {};
    }
    if (output_ready == WaitStatus::Error) {
        perror("[dma350] poll out");
        return {};
    }
    if (xioctl(fd_, VIDIOC_DQBUF, &dob) < 0) {
        perror("[dma350] DQBUF out");
        return {};
    }

    // cache-invalidate so the CPU sees the DMA350's writes when it reads this CMA buffer.
    dma_buf_sync s{};
    s.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ;
    ioctl(out_fd_[dob.index], DMA_BUF_IOCTL_SYNC, &s);
    return {static_cast<const uint8_t*>(out_map_[dob.index]), out_fd_[dob.index]};
}

}  // namespace app
