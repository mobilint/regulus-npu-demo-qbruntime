#include "cma_display_ring.h"

#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>

namespace app {
namespace {
int cma_alloc(size_t bytes) {
    int heap = open("/dev/dma_heap/linux,cma", O_RDWR);
    if (heap < 0) return -1;
    dma_heap_allocation_data ad{};
    ad.len = bytes;
    ad.fd_flags = O_RDWR | O_CLOEXEC;
    int r = ioctl(heap, DMA_HEAP_IOCTL_ALLOC, &ad);
    close(heap);
    return r < 0 ? -1 : ad.fd;
}
void sync_buf(int fd, bool start) {
    dma_buf_sync s{};
    s.flags = (start ? DMA_BUF_SYNC_START : DMA_BUF_SYNC_END) | DMA_BUF_SYNC_RW;
    ioctl(fd, DMA_BUF_IOCTL_SYNC, &s);
}
}  // namespace

CmaDisplayRing::CmaDisplayRing(int w, int h, int count)
    : w_(w), h_(h), count_(count), bytes_((size_t)w * h * 4) {
    for (int i = 0; i < count_; ++i) {
        int fd = cma_alloc(bytes_);
        if (fd < 0) {
            perror("[cma-ring] alloc");
            return;
        }
        void* m = mmap(nullptr, bytes_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (m == MAP_FAILED) {
            perror("[cma-ring] mmap");
            close(fd);
            return;
        }
        fds_.push_back(fd);
        maps_.push_back(m);
        mats_.emplace_back(h_, w_, CV_8UC4, m);
    }
    ok_ = true;
    printf("[cma-ring] ready (%dx%d RGBX, ring=%d)\n", w_, h_, count_);
}

CmaDisplayRing::~CmaDisplayRing() {
    for (size_t i = 0; i < fds_.size(); ++i) {
        if (maps_[i]) munmap(maps_[i], bytes_);
        if (fds_[i] > 0) close(fds_[i]);
    }
}

void CmaDisplayRing::begin_cpu(int i) { sync_buf(fds_[i], true); }
void CmaDisplayRing::end_cpu(int i) { sync_buf(fds_[i], false); }

}  // namespace app
