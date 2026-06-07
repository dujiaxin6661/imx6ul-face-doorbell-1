#ifndef V4L2_CAPTURE_H
#define V4L2_CAPTURE_H

#include <stdint.h>
#include <pthread.h>

struct cam_buffer {
    void   *start;
    size_t  length;
};

struct frame_data {
    unsigned char *data;
    size_t         size;
    int            ready;
};

extern int cam_fd;
extern struct cam_buffer *cam_buffers;
extern unsigned int cam_n_buffers;
extern unsigned int cam_width;
extern unsigned int cam_height;
extern unsigned int cam_pixel_format;

int  camera_init(void);
void camera_release(void);

int  camera_get_frame(unsigned char **data, size_t *size);

int  frame_queue_init(void);
void frame_queue_put(const unsigned char *data, size_t size);
int  frame_queue_get(unsigned char *data, size_t *size);
void frame_queue_destroy(void);

#endif
