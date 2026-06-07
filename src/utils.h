#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

#define CLIP(x) ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))

void yuyv_to_rgb24(const unsigned char *yuyv, unsigned char *rgb,
                   int width, int height);

void yuyv_to_rgb32(const unsigned char *yuyv, unsigned char *fb_pos,
                   int width, int height, int fb_line_len);

void rgb24_crop_resize(const unsigned char *src, int src_w, int src_h,
                       int x, int y, int w, int h,
                       unsigned char *dst, int dst_w, int dst_h);

void rgb24_to_rgb32(const unsigned char *rgb24, int width, int height,
                    unsigned char *rgb32, int fb_line_len);

void l2_normalize(float *vec, int dim);

#endif
