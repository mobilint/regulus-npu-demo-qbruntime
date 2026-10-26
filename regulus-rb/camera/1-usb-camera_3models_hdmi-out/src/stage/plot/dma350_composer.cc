#include "dma350_composer.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdio>
#include <string>

namespace app {
namespace {
constexpr int OUT = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;   // source (the small frame)
constexpr int CAP = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;  // dest (the display buffer)

// The dma350 /dev/video node number varies per boot, so find it by driver name at
// runtime.
std::string find_dma350_node() {
  for (int n = 0; n < 64; n++) {
    std::string path = "/dev/video" + std::to_string(n);
    int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) continue;
    v4l2_capability cap{};
    bool match =
        (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) && (strcmp(reinterpret_cast<const char*>(cap.driver), "dma350") == 0);
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
unsigned int pix_fmt(int bpp) { return bpp == 4 ? v4l2_fourcc('X', 'B', '2', '4') : V4L2_PIX_FMT_RGB24; }
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
}  // namespace

Dma350Composer::Dma350Composer(int src_w, int src_h, int dst_w, int dst_h, int bpp)
    : src_w_(src_w),
      src_h_(src_h),
      dst_w_(dst_w),
      dst_h_(dst_h),
      win_w_(src_w),
      win_h_(src_h),
      bpp_(bpp),
      src_bytes_((size_t)src_w * src_h * bpp),
      dst_bytes_((size_t)dst_w * dst_h * bpp) {
  std::string node = find_dma350_node();
  if (node.empty()) {
    fprintf(stderr, "[dma350-compose] node not found (driver 'dma350')\n");
    return;
  }
  fd_ = open(node.c_str(), O_RDWR | O_NONBLOCK);
  if (fd_ < 0) {
    perror("[dma350-compose] open");
    return;
  }

  if (!set_fmt(fd_, OUT, src_w, src_h, bpp_) || !set_fmt(fd_, CAP, dst_w, dst_h, bpp_)) {
    fprintf(stderr, "[dma350-compose] S_FMT failed\n");
    return;
  }

  // One slot per queue: every job imports the caller's dmabuf into the same slot, and
  // jobs are serialised by the blocking DQBUF below.
  v4l2_requestbuffers rb{};
  rb.count = 1;
  rb.type = OUT;
  rb.memory = V4L2_MEMORY_DMABUF;
  if (xioctl(fd_, VIDIOC_REQBUFS, &rb) < 0) {
    perror("[dma350-compose] REQBUFS out");
    return;
  }
  rb = {};
  rb.count = 1;
  rb.type = CAP;
  rb.memory = V4L2_MEMORY_DMABUF;
  if (xioctl(fd_, VIDIOC_REQBUFS, &rb) < 0) {
    perror("[dma350-compose] REQBUFS cap");
    return;
  }

  if (xioctl(fd_, VIDIOC_STREAMON, (void*)&OUT) < 0) {
    perror("[dma350-compose] STREAMON out");
    return;
  }
  if (xioctl(fd_, VIDIOC_STREAMON, (void*)&CAP) < 0) {
    perror("[dma350-compose] STREAMON cap");
    return;
  }
  streaming_ = true;
  ok_ = true;
  printf("[dma350-compose] ready (%dx%d %s -> %dx%d, node %s)\n", src_w, src_h, bpp_ == 4 ? "RGBX" : "RGB24", dst_w,
         dst_h, node.c_str());
}

Dma350Composer::~Dma350Composer() {
  if (fd_ >= 0) {
    if (streaming_) {
      xioctl(fd_, VIDIOC_STREAMOFF, (void*)&OUT);
      xioctl(fd_, VIDIOC_STREAMOFF, (void*)&CAP);
    }
    close(fd_);
  }
}

bool Dma350Composer::set_source_crop(int x, int y, int w, int h) {
  if (!ok_) return false;

  v4l2_selection sel{};
  sel.type = OUT;
  sel.target = V4L2_SEL_TGT_CROP;
  sel.r.left = x;
  sel.r.top = y;
  sel.r.width = w;
  sel.r.height = h;
  if (xioctl(fd_, VIDIOC_S_SELECTION, &sel) < 0) {
    perror("[dma350-compose] S_SELECTION crop");
    return false;
  }
  // The crop is clamped to the declared source size, so a window wider than the format
  // would come back smaller. Report what was granted and use that as the compose size.
  if (sel.r.width != (unsigned)w || sel.r.height != (unsigned)h || sel.r.left != x || sel.r.top != y)
    fprintf(stderr, "[dma350-compose] crop (%d,%d %dx%d) granted as (%d,%d %ux%u)\n", x, y, w, h, sel.r.left, sel.r.top,
            sel.r.width, sel.r.height);
  win_w_ = (int)sel.r.width;
  win_h_ = (int)sel.r.height;
  printf("[dma350-compose] source window %dx%d at (%d,%d) of %dx%d\n", win_w_, win_h_, sel.r.left, sel.r.top, src_w_,
         src_h_);
  return true;
}

bool Dma350Composer::compose(int src_fd, size_t src_offset, int dst_fd, int x, int y) {
  if (!ok_ || src_fd < 0 || dst_fd < 0) return false;

  v4l2_selection sel{};
  sel.type = CAP;
  sel.target = V4L2_SEL_TGT_COMPOSE;
  sel.r.left = x;
  sel.r.top = y;
  sel.r.width = win_w_;
  sel.r.height = win_h_;
  if (xioctl(fd_, VIDIOC_S_SELECTION, &sel) < 0) {
    perror("[dma350-compose] S_SELECTION");
    return false;
  }
  // The driver reports what it granted. This catches the 2-pixel rounding only; the
  // 16-byte address rounding that actually moves the picture is not reported (see the
  // header).
  if (!warned_align_ && (sel.r.left != x || sel.r.top != y)) {
    fprintf(stderr, "[dma350-compose] rect (%d,%d) was granted as (%d,%d)\n", x, y, sel.r.left, sel.r.top);
    warned_align_ = true;
  }

  v4l2_plane ip{};
  v4l2_buffer ib{};
  ib.type = OUT;
  ib.memory = V4L2_MEMORY_DMABUF;
  ib.index = 0;
  ib.length = 1;
  ib.m.planes = &ip;
  ip.m.fd = src_fd;
  ip.bytesused = src_bytes_;
  ip.length = src_bytes_;
  ip.data_offset = src_offset;
  if (xioctl(fd_, VIDIOC_QBUF, &ib) < 0) {
    perror("[dma350-compose] QBUF in");
    return false;
  }

  v4l2_plane op{};
  v4l2_buffer ob{};
  ob.type = CAP;
  ob.memory = V4L2_MEMORY_DMABUF;
  ob.index = 0;
  ob.length = 1;
  ob.m.planes = &op;
  op.m.fd = dst_fd;
  op.length = dst_bytes_;
  if (xioctl(fd_, VIDIOC_QBUF, &ob) < 0) {
    perror("[dma350-compose] QBUF out");
    return false;
  }

  v4l2_plane dip{};
  v4l2_buffer dib{};
  dib.type = OUT;
  dib.memory = V4L2_MEMORY_DMABUF;
  dib.length = 1;
  dib.m.planes = &dip;
  const WaitStatus input_ready = wait_ready(fd_, POLLOUT);
  if (input_ready == WaitStatus::Timeout) {
    fprintf(stderr, "[dma350-compose] DQBUF in timed out\n");
    return false;
  }
  if (input_ready == WaitStatus::Error) {
    perror("[dma350-compose] poll in");
    return false;
  }
  if (xioctl(fd_, VIDIOC_DQBUF, &dib) < 0) {
    perror("[dma350-compose] DQBUF in");
    return false;
  }

  v4l2_plane dop{};
  v4l2_buffer dob{};
  dob.type = CAP;
  dob.memory = V4L2_MEMORY_DMABUF;
  dob.length = 1;
  dob.m.planes = &dop;
  const WaitStatus output_ready = wait_ready(fd_, POLLIN);
  if (output_ready == WaitStatus::Timeout) {
    fprintf(stderr, "[dma350-compose] DQBUF out timed out\n");
    return false;
  }
  if (output_ready == WaitStatus::Error) {
    perror("[dma350-compose] poll out");
    return false;
  }
  if (xioctl(fd_, VIDIOC_DQBUF, &dob) < 0) {
    perror("[dma350-compose] DQBUF out");
    return false;
  }

  // No cache maintenance here: the destination's dma-buf sync is owned by the caller
  // (CmaDisplayRing::begin_cpu / end_cpu), which must invalidate AFTER these writes
  // land.
  return true;
}

}  // namespace app
