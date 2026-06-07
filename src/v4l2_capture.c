#include "v4l2_capture.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>

int cam_fd = -1;
struct cam_buffer *cam_buffers = NULL;
unsigned int cam_n_buffers = 0;
unsigned int cam_width = 0;
unsigned int cam_height = 0;
unsigned int cam_pixel_format = 0;

#if MOCK_MODE

int camera_init(void)
{
    cam_width = CAM_WIDTH;
    cam_height = CAM_HEIGHT;
    cam_pixel_format = V4L2_PIX_FMT_YUYV;
    printf("[CAM] MOCK: dummy YUYV 640x480\n");
    return 0;
}

void camera_release(void) {}

int camera_get_frame(unsigned char **data, size_t *size)
{
    /* 生成假 YUYV 帧（灰绿条纹测试图案），供显示和识别逻辑验证 */
    static unsigned char dummy[CAM_WIDTH * CAM_HEIGHT * 2];
    static int inited = 0;
    if (!inited) {
        int i, j;
        for (i = 0; i < CAM_HEIGHT; i++) {
            for (j = 0; j < CAM_WIDTH; j += 2) {
                int idx = (i * CAM_WIDTH + j) * 2;
                /* 上半部分绿色，下半部分灰色 */
                unsigned char y = (i < CAM_HEIGHT / 2) ? 150 : 100;
                unsigned char u = 128, v = 128;
                if ((j / 32) % 2 == 0) { v = 100; }  /* 彩色竖条纹 */
                dummy[idx]     = y;
                dummy[idx + 1] = u;
                dummy[idx + 2] = y;
                dummy[idx + 3] = v;
            }
        }
        inited = 1;
    }
    *data = dummy;
    *size = sizeof(dummy);
    return 0;
}

#else

int camera_init(void)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    unsigned int i;

    cam_fd = open(CAM_DEVICE, O_RDWR);
    if (cam_fd < 0) {
        perror("open camera");
        return -1;
    }

    ioctl(cam_fd, VIDIOC_QUERYCAP, &cap);
    printf("[CAM] driver=%s card=%s\n", cap.driver, cap.card);

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = CAM_WIDTH;
    fmt.fmt.pix.height = CAM_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(cam_fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("set YUYV failed, trying MJPEG");
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        if (ioctl(cam_fd, VIDIOC_S_FMT, &fmt) < 0) {
            perror("set format failed");
            close(cam_fd);
            cam_fd = -1;
            return -1;
        }
    }

    cam_width = fmt.fmt.pix.width;
    cam_height = fmt.fmt.pix.height;
    cam_pixel_format = fmt.fmt.pix.pixelformat;
    printf("[CAM] %dx%d pixelformat=0x%08X\n",
           cam_width, cam_height, cam_pixel_format);

    memset(&req, 0, sizeof(req));
    req.count = CAM_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(cam_fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("request buffers");
        close(cam_fd);
        cam_fd = -1;
        return -1;
    }

    cam_buffers = (struct cam_buffer *)calloc(req.count, sizeof(*cam_buffers));
    cam_n_buffers = req.count;

    for (i = 0; i < cam_n_buffers; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        ioctl(cam_fd, VIDIOC_QUERYBUF, &buf);
        cam_buffers[i].length = buf.length;
        cam_buffers[i].start = mmap(NULL, buf.length,
                                     PROT_READ | PROT_WRITE,
                                     MAP_SHARED, cam_fd, buf.m.offset);
        if (cam_buffers[i].start == MAP_FAILED) {
            perror("mmap failed");
            return -1;
        }
    }

    for (i = 0; i < cam_n_buffers; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        ioctl(cam_fd, VIDIOC_QBUF, &buf);
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam_fd, VIDIOC_STREAMON, &type);
    printf("[CAM] stream started\n");
    return 0;
}

void camera_release(void)
{
    if (cam_fd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(cam_fd, VIDIOC_STREAMOFF, &type);
        unsigned int i;
        for (i = 0; i < cam_n_buffers; i++) {
            if (cam_buffers && cam_buffers[i].start)
                munmap(cam_buffers[i].start, cam_buffers[i].length);
        }
        free(cam_buffers);
        cam_buffers = NULL;
        close(cam_fd);
        cam_fd = -1;
    }
}

int camera_get_frame(unsigned char **data, size_t *size)
{
    struct v4l2_buffer buf;
    fd_set fds;
    struct timeval tv;

    FD_ZERO(&fds);
    FD_SET(cam_fd, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 100000;

    if (select(cam_fd + 1, &fds, NULL, NULL, &tv) <= 0)
        return -1;

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(cam_fd, VIDIOC_DQBUF, &buf) < 0)
        return -1;

    *data = (unsigned char *)cam_buffers[buf.index].start;
    *size = buf.bytesused;

    ioctl(cam_fd, VIDIOC_QBUF, &buf);
    return 0;
}

#endif

static struct frame_data frame_queue[FRAME_QUEUE_SIZE];
static int frame_wr_idx = 0;
static int frame_rd_idx = 0;
static int frame_count = 0;
static pthread_mutex_t frame_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  frame_cond  = PTHREAD_COND_INITIALIZER;

int frame_queue_init(void)
{
    int i;
    for (i = 0; i < FRAME_QUEUE_SIZE; i++) {
        frame_queue[i].data = (unsigned char *)malloc(CAM_WIDTH * CAM_HEIGHT * 2);
        frame_queue[i].size = 0;
        frame_queue[i].ready = 0;
    }
    return 0;
}

void frame_queue_put(const unsigned char *data, size_t size)
{
    pthread_mutex_lock(&frame_mutex);
    if (frame_count < FRAME_QUEUE_SIZE) {
        memcpy(frame_queue[frame_wr_idx].data, data, size);
        frame_queue[frame_wr_idx].size = size;
        frame_queue[frame_wr_idx].ready = 1;
        frame_wr_idx = (frame_wr_idx + 1) % FRAME_QUEUE_SIZE;
        frame_count++;
        pthread_cond_signal(&frame_cond);
    }
    pthread_mutex_unlock(&frame_mutex);
}

int frame_queue_get(unsigned char *data, size_t *size)
{
    pthread_mutex_lock(&frame_mutex);
    while (frame_count == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;
        if (pthread_cond_timedwait(&frame_cond, &frame_mutex, &ts) != 0) {
            pthread_mutex_unlock(&frame_mutex);
            return -1;
        }
    }
    memcpy(data, frame_queue[frame_rd_idx].data, frame_queue[frame_rd_idx].size);
    *size = frame_queue[frame_rd_idx].size;
    frame_queue[frame_rd_idx].ready = 0;
    frame_rd_idx = (frame_rd_idx + 1) % FRAME_QUEUE_SIZE;
    frame_count--;
    pthread_mutex_unlock(&frame_mutex);
    return 0;
}

void frame_queue_destroy(void)
{
    int i;
    for (i = 0; i < FRAME_QUEUE_SIZE; i++) {
        free(frame_queue[i].data);
        frame_queue[i].data = NULL;
    }
}
