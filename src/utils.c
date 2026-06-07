#include "utils.h"
#include <string.h>
#include <math.h>
/*
RGB24 类型的数据主要是给UltraFace和MobileFaceNet模型使用，
RGB32 类型的数据主要是给Framebuffer 屏幕显示使用
*/

// 把摄像头常见的 YUYV 格式图像转换成 RGB24 格式
void yuyv_to_rgb24(const unsigned char *yuyv, unsigned char *rgb,
                   int width, int height)
{
    int i, j;
    for (i = 0; i < height; i++) {
        const unsigned char *line_src = yuyv + i * width * 2;
        unsigned char *line_dst = rgb + i * width * 3;
        for (j = 0; j < width; j += 2) {
            int y0 = line_src[j * 2];
            int u  = line_src[j * 2 + 1];
            int y1 = line_src[j * 2 + 2];
            int v  = line_src[j * 2 + 3];

            int u_adj = u - 128;
            int v_adj = v - 128;

            line_dst[j * 3 + 0] = CLIP(y0 + ((359 * v_adj) >> 8));
            line_dst[j * 3 + 1] = CLIP(y0 - ((88 * u_adj + 183 * v_adj) >> 8));
            line_dst[j * 3 + 2] = CLIP(y0 + ((454 * u_adj) >> 8));

            line_dst[(j + 1) * 3 + 0] = CLIP(y1 + ((359 * v_adj) >> 8));
            line_dst[(j + 1) * 3 + 1] = CLIP(y1 - ((88 * u_adj + 183 * v_adj) >> 8));
            line_dst[(j + 1) * 3 + 2] = CLIP(y1 + ((454 * u_adj) >> 8));
        }
    }
}

// 把 YUYV 转换成 RGB32，直接写入帧缓冲（带行距）
void yuyv_to_rgb32(const unsigned char *yuyv, unsigned char *fb_pos,
                   int width, int height, int fb_line_len)
{
    int i, j;
    for (i = 0; i < height; i++) {
        const unsigned char *line_src = yuyv + i * width * 2;
        unsigned int *line_dst = (unsigned int *)(fb_pos + i * fb_line_len);
        for (j = 0; j < width; j += 2) {
            int y0 = line_src[j * 2];
            int u  = line_src[j * 2 + 1];
            int y1 = line_src[j * 2 + 2];
            int v  = line_src[j * 2 + 3];

            int u_adj = u - 128;
            int v_adj = v - 128;

            int r0 = CLIP(y0 + ((359 * v_adj) >> 8));
            int g0 = CLIP(y0 - ((88 * u_adj + 183 * v_adj) >> 8));
            int b0 = CLIP(y0 + ((454 * u_adj) >> 8));
            int r1 = CLIP(y1 + ((359 * v_adj) >> 8));
            int g1 = CLIP(y1 - ((88 * u_adj + 183 * v_adj) >> 8));
            int b1 = CLIP(y1 + ((454 * u_adj) >> 8));

            line_dst[j]     = (0xFF << 24) | (r0 << 16) | (g0 << 8) | b0;
            line_dst[j + 1] = (0xFF << 24) | (r1 << 16) | (g1 << 8) | b1;
        }
    }
}

/*
rgb24_crop_resize(rgb, 640, 480, x, y, w, h,  face_rgb, 112, 112)
1. crop（裁剪）：从 640×480 大图里把 UltraFace 输出的矩形抠出来
2. resize（缩放）：不管抠出来的脸是 200×260 还是 50×60，统一用最近邻采样缩到 112×112，因为 MobileFaceNet 模型需要输入 112×112 的人脸图像
*/ 
void rgb24_crop_resize(const unsigned char *src, int src_w, int src_h,
                       int x, int y, int w, int h,
                       unsigned char *dst, int dst_w, int dst_h)
{
    int cx = x, cy = y, cw = w, ch = h;
    if (cx < 0) { cw += cx; cx = 0; }
    if (cy < 0) { ch += cy; cy = 0; }
    if (cx + cw > src_w) cw = src_w - cx;
    if (cy + ch > src_h) ch = src_h - cy;
    if (cw <= 0 || ch <= 0) {
        memset(dst, 0, dst_w * dst_h * 3);
        return;
    }

    int di, dj;
    for (di = 0; di < dst_h; di++) {
        int si = cy + (di * ch) / dst_h;
        for (dj = 0; dj < dst_w; dj++) {
            int sj = cx + (dj * cw) / dst_w;
            int src_idx = (si * src_w + sj) * 3;
            int dst_idx = (di * dst_w + dj) * 3;
            dst[dst_idx + 0] = src[src_idx + 0];
            dst[dst_idx + 1] = src[src_idx + 1];
            dst[dst_idx + 2] = src[src_idx + 2];
        }
    }
}

// 把 RGB24 图像转换成 RGB32，直接写入帧缓冲（带行距）
void rgb24_to_rgb32(const unsigned char *rgb24, int width, int height,
                    unsigned char *rgb32, int fb_line_len)
{
    int i, j;
    for (i = 0; i < height; i++) {
        const unsigned char *src = rgb24 + i * width * 3;
        unsigned int *dst = (unsigned int *)(rgb32 + i * fb_line_len);
        for (j = 0; j < width; j++) {
            dst[j] = (0xFF << 24) |
                     (src[j * 3] << 16) |
                     (src[j * 3 + 1] << 8) |
                     src[j * 3 + 2];
        }
    }
}

/*
特征向量归一化
ultraface 模型吐出的 [1*128] 维特征向量转化为单位向量，
公式：$vec[i] = \frac{vec[i]}{\sqrt{\sum vec[j]^2}}$
为了消除环境的干扰，通常会把特征向量进行归一化处理，也就是把它转换成一个单位向量，这样在计算两个特征向量之间的距离时，就只需要计算它们的夹角（余弦距离）或者欧氏距离，而不受特征向量的长度影响。
*/
void l2_normalize(float *vec, int dim)
{
    float sum = 0.0f;
    int i;
    for (i = 0; i < dim; i++){
        sum += vec[i] * vec[i];
    } 

    float norm = sqrtf(sum);

    if (norm > 1e-8f) {
        float inv = 1.0f / norm;
        for (i = 0; i < dim; i++){
            vec[i] *= inv;
        } 
    }
}
