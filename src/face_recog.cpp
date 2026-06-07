#include "face_recog.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>

#if MOCK_MODE

extern "C" {

int face_recog_init(void) {
    printf("[RECOG] MOCK: dummy embedding\n");
    return 0;
}
void face_recog_release(void) {}

int face_recog_extract(const unsigned char *face_rgb, int w, int h,
                        float *embedding)
{
    (void)face_rgb; (void)w; (void)h;
    for (int i = 0; i < FACE_FEAT_DIM; i++) embedding[i] = 0.1f;
    return 0;
}

float face_recog_compare(const float *a, const float *b, int dim)
{
    float dist = 0.0f;
    for (int i = 0; i < dim; i++) { float d = a[i] - b[i]; dist += d * d; }
    return sqrtf(dist);
}

} // extern "C"

#else

#include <ncnn/net.h>

static ncnn::Net *net = NULL;

extern "C" {

int face_recog_init(void)
{
    net = new ncnn::Net();
    net->opt.use_vulkan_compute = 0;
    net->opt.num_threads = 1;

    if (net->load_param(MFNET_PARAM) != 0 ||
        net->load_model(MFNET_BIN)   != 0) {
        fprintf(stderr, "[RECOG] Failed to load MobileFaceNet\n");
        delete net; net = NULL;
        return -1;
    }
    printf("[RECOG] MobileFaceNet loaded\n");
    return 0;
}

void face_recog_release(void)
{
    if (net) { delete net; net = NULL; }
}

// 调用MobileFaceNet模型，输入裁剪后的112*112人脸图像，输出128维特征向量
int face_recog_extract(const unsigned char *face_rgb,
                        int w, int h,
                        float *embedding)
{
    if (!net) return -1;

    /* 双线性插值缩放到 112x112 
    unsigned char *resized = new unsigned char[112 * 112 * 3];
    {
        float rx = (float)(w - 1) / 111.0f, ry = (float)(h - 1) / 111.0f;
        for (int i = 0; i < 112; i++) {
            float y  = i * ry;
            int   y0 = (int)y, y1 = y0 + 1 < h ? y0 + 1 : y0;
            float dy = y - y0;
            for (int j = 0; j < 112; j++) {
                float x  = j * rx;
                int   x0 = (int)x, x1 = x0 + 1 < w ? x0 + 1 : x0;
                float dx = x - x0;
                for (int c = 0; c < 3; c++) {
                    float v = face_rgb[(y0*w + x0)*3 + c] * (1.0f-dx) * (1.0f-dy) +
                              face_rgb[(y0*w + x1)*3 + c] * dx * (1.0f-dy) +
                              face_rgb[(y1*w + x0)*3 + c] * (1.0f-dx) * dy +
                              face_rgb[(y1*w + x1)*3 + c] * dx * dy;
                    resized[(i*112 + j)*3 + c] = (unsigned char)(v + 0.5f);
                }
            }
        }
    }
    */
    

    /*
    ncnn 的输入不仅可以是RGB24，还可以是RGB32，但是模型真正需要的输入是一个四维的浮点数矩阵[N, C, H, W]，ncnn不论输入是什么格式，都会在内部进行转换和预处理，所以我们直接把缩放后的RGB24图像转换成ncnn的Mat格式，并且在Mat里进行均值归一化等预处理操作，最后输入到模型里。
    */
    /* main.c 已通过 rgb24_crop_resize 保证输入 = 112x112, 直接转换 */
    ncnn::Mat in = ncnn::Mat::from_pixels(face_rgb, ncnn::Mat::PIXEL_RGB, 112, 112);
    // 均值归一化，MobileFaceNet 模型需要把输入像素值从 [0, 255] 归一化到 [-1, 1]，所以先减去127.5再乘以1/128
    const float mean[3] = {127.5f, 127.5f, 127.5f};
    const float norm[3] = {1.0f/128.0f, 1.0f/128.0f, 1.0f/128.0f};
    in.substract_mean_normalize(mean, norm);

    /* 推理 */
    ncnn::Extractor ex = net->create_extractor();
    ex.input("input", in);

    ncnn::Mat out;
    ex.extract("embedding", out);

    /* 提取 128 维特征 (ncnn 输出 CHW 布局: 128x1x1) */
    int dim = out.w * out.h * out.c;
    if (dim > FACE_FEAT_DIM){
        dim = FACE_FEAT_DIM;
    } 
    // 将 out.data 指向的内存区域中的前 dim 个 float 数据拷贝到 embedding 数组的起始位置。
    memcpy(embedding, (float*)out.data, dim * sizeof(float));
    for (int i = dim; i < FACE_FEAT_DIM; i++){
        embedding[i] = 0.0f;   // 剩余位置补零（如果实际特征数不足）
    } 

    return 0;
}

// 比较欧氏距离，距离越小越相似
float face_recog_compare(const float* a, const float* b, int dim)
{
    float dist = 0.0f;
    for (int i = 0; i < dim; i++) { 
        float d = a[i] - b[i]; 
        dist += d * d; 
    }
    return sqrtf(dist);
}

} // extern "C"

#endif
