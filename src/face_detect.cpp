#include "face_detect.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>
#include <vector>
#include <algorithm>

#if MOCK_MODE

extern "C" {

int face_detect_init(void) {
    printf("[DETECT] MOCK: fixed ROI\n");
    return 0;
}
void face_detect_release(void) {}

int face_detect_run(const unsigned char *rgb, int width, int height,
                    face_box_t *boxes, int max_boxes, int *num_boxes)
{
    (void)rgb; (void)width; (void)height;
    *num_boxes = 1;
    if (*num_boxes > max_boxes) *num_boxes = max_boxes;
    boxes[0].x = width / 4;
    boxes[0].y = height / 4;
    boxes[0].w = width / 2;
    boxes[0].h = height / 2;
    boxes[0].score = 0.95f;
    return 0;
}

} // extern "C"

#else

#include <ncnn/net.h>

static ncnn::Net* net = NULL;   // 定义一个静态指针，指向 ncnn::Net 对象。这个对象是 ncnn 的核心类，负责加载模型、管理资源和执行推理。
static int input_w = 320, input_h = 240;
static int model_w = 0, model_h = 0;

/* 
产生 UltraFace 锚点框 (4420个, 匹配 Linzaer 模型) 
cx, cy：框的中心点坐标（归一化到 [0,1] 范围，即相对于原图宽高的比例）
s_kx, s_ky：框的宽度和高度（也是归一化的，即相对于原图宽高的比例）
*/
struct Anchor { float cx, cy, s_kx, s_ky; };
static std::vector<Anchor> anchors;    // 动态数组，存放所有生成的锚点框

static int generate_anchors(int w, int h)
{
    anchors.clear();
    struct { float s; int ns; float sz[3]; } cfg[] = {
        {8.0f,  3, {10.0f, 16.0f, 24.0f}},
        {16.0f, 2, {32.0f, 48.0f}},
        {32.0f, 2, {64.0f, 96.0f}},
        {64.0f, 3, {128.0f, 192.0f, 256.0f}},
    };
    for (int si = 0; si < 4; si++) {
        int fw = (w + (int)cfg[si].s - 1) / (int)cfg[si].s;
        int fh = (h + (int)cfg[si].s - 1) / (int)cfg[si].s;
        for (int i = 0; i < fh; i++) {
            for (int j = 0; j < fw; j++) {
                float cx = (j + 0.5f) * cfg[si].s / w;
                float cy = (i + 0.5f) * cfg[si].s / h;
                for (int k = 0; k < cfg[si].ns; k++) {
                    anchors.push_back({cx, cy,
                        cfg[si].sz[k] / (float)w,
                        cfg[si].sz[k] / (float)h});
                }
            }
        }
    }
    return 0;
}

/* 双线性插值 RGB resize */
static void rgb_resize_bilinear(const unsigned char *src, int sw, int sh,
                                 unsigned char *dst, int dw, int dh)
{
    float rx = (float)(sw - 1) / (float)(dw > 1 ? dw - 1 : 1);
    float ry = (float)(sh - 1) / (float)(dh > 1 ? dh - 1 : 1);
    for (int i = 0; i < dh; i++) {
        float y  = i * ry;
        int   y0 = (int)y, y1 = y0 + 1 < sh ? y0 + 1 : y0;
        float dy = y - y0;
        for (int j = 0; j < dw; j++) {
            float x  = j * rx;
            int   x0 = (int)x, x1 = x0 + 1 < sw ? x0 + 1 : x0;
            float dx = x - x0;
            for (int c = 0; c < 3; c++) {
                float v = src[(y0*sw + x0)*3 + c] * (1.0f-dx) * (1.0f-dy) +
                          src[(y0*sw + x1)*3 + c] * dx * (1.0f-dy) +
                          src[(y1*sw + x0)*3 + c] * (1.0f-dx) * dy +
                          src[(y1*sw + x1)*3 + c] * dx * dy;
                dst[(i*dw + j)*3 + c] = (unsigned char)(v + 0.5f);
            }
        }
    }
}

/* RGB resize + 归一化到 ncnn::Mat */
static void preprocess(const unsigned char *rgb, int w, int h,
                       ncnn::Mat &in, int tw, int th)
{
    unsigned char *resized = new unsigned char[tw * th * 3];
    rgb_resize_bilinear(rgb, w, h, resized, tw, th);
    in = ncnn::Mat::from_pixels(resized, ncnn::Mat::PIXEL_RGB, tw, th);
    delete[] resized;
    // 均值归一化，UltraFace 模型需要把输入像素值从 [0, 255] 归一化到 [-1, 1]，所以先减去127.5再乘以1/128
    const float mean[3] = {127.5f, 127.5f, 127.5f};
    const float norm[3] = {1.0f/128.0f, 1.0f/128.0f, 1.0f/128.0f};
    in.substract_mean_normalize(mean, norm);
}

/* 解码 + NMS */
static int postprocess(const ncnn::Mat &scores, const ncnn::Mat &boxes_out,
                       int img_w, int img_h,
                       face_box_t *dst, int max_boxes)
{
    int num_anchors = anchors.size();
    if (num_anchors == 0) return 0;

    // struct Det 定义了一个结构体，包含一个检测框的坐标（左上角 (x1,y1)，右下角 (x2,y2)）和置信度 score
    struct Det { float x1,y1,x2,y2,score; };
    std::vector<Det> dets;   // 声明了一个动态数组，元素类型为 Det
    /*
    std::vector 是 C++ 标准库中的容器，自动管理内存，能动态增删元素，比 C 的数组（需要手动 malloc/free）更方便、安全。
    */

    for (int i = 0; i < num_anchors; i++) {
        float conf = ((const float*)scores.data)[i * 2 + 1];  // 取第 i 个锚点"是脸"的分数
        if (conf < 0.03f) continue;    // 分数太低 → 跳过，这个锚点不是脸

        const float *b = (const float*)boxes_out.data;
        float cx = anchors[i].cx + b[i * 4 + 0] * 0.1f * anchors[i].s_kx;
        float cy = anchors[i].cy + b[i * 4 + 1] * 0.1f * anchors[i].s_ky;
        float w  = anchors[i].s_kx * expf(b[i * 4 + 2] * 0.2f);
        float h  = anchors[i].s_ky * expf(b[i * 4 + 3] * 0.2f);

        float x1 = (cx - w * 0.5f) * img_w;
        float y1 = (cy - h * 0.5f) * img_h;
        float x2 = (cx + w * 0.5f) * img_w;
        float y2 = (cy + h * 0.5f) * img_h;

        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 > img_w) x2 = img_w;
        if (y2 > img_h) y2 = img_h;
        if (x2 - x1 < 10 || y2 - y1 < 10) continue;

        dets.push_back({x1, y1, x2, y2, conf});
    }

    /* NMS */
    // 将 dets 中的所有元素按 score 从大到小降序排序。
    std::sort(dets.begin(), dets.end(),
              [](const Det &a, const Det &b) { return a.score > b.score; });

    // suppressed 是一个与 dets 大小相同的布尔向量，初始值为 false，表示每个检测框是否被抑制（即被认为是重复的或重叠过多的框）。
    std::vector<bool> suppressed(dets.size(), false);
    for (size_t i = 0; i < dets.size() && (int)i < max_boxes; i++) {
        if (suppressed[i]) continue;
        for (size_t j = i + 1; j < dets.size(); j++) {
            if (suppressed[j]) continue;
            // ix1, iy1：交集矩形左上角的 x、y 坐标。取两个框左上角坐标的最大值（因为交集左边界是更靠右的那个）
            float ix1 = std::max(dets[i].x1, dets[j].x1);
            float iy1 = std::max(dets[i].y1, dets[j].y1);
            // ix2, iy2：交集矩形右下角的 x、y 坐标。取两个框右下角坐标的最小值（因为交集右边界是更靠左的那个）
            float ix2 = std::min(dets[i].x2, dets[j].x2);
            float iy2 = std::min(dets[i].y2, dets[j].y2);
            float iw = ix2 - ix1, ih = iy2 - iy1;
            if (iw <= 0 || ih <= 0) continue;
            // 计算交并比 IoU = 交集面积 / (框 i 面积 + 框 j 面积 - 交集面积)
            float iou = (iw * ih) / ((dets[i].x2-dets[i].x1)*(dets[i].y2-dets[i].y1) +
                                      (dets[j].x2-dets[j].x1)*(dets[j].y2-dets[j].y1) - iw*ih);
            // 如果 IoU > 0.45，表示两个框重叠非常严重，认为它们是重复的检测结果，那么就把 j 号框标记为被抑制（suppressed[j] = true），表示它不应该被保留在最终的检测结果中。
            if (iou > 0.45f){
                suppressed[j] = true;
            } 
        }
    }

    int count = 0;
    for (size_t i = 0; i < dets.size() && count < max_boxes; i++) {
        if (suppressed[i]) continue;

        dst[count].x = (int)dets[i].x1;
        dst[count].y = (int)dets[i].y1;
        dst[count].w = (int)(dets[i].x2 - dets[i].x1);
        dst[count].h = (int)(dets[i].y2 - dets[i].y1);
        dst[count].score = dets[i].score;
        count++;
    }
    return count;
}

extern "C" {

int face_detect_init(void)
{
    net = new ncnn::Net();
    net->opt.use_vulkan_compute = 0;
    net->opt.num_threads = 1;

    /* 加载 ncnn 模型 */
    if (net->load_param(ULTRAF_PARAM) != 0 ||
        net->load_model(ULTRAF_BIN)   != 0) {
        fprintf(stderr, "[DETECT] Failed to load UltraFace model\n");
        delete net; net = NULL;
        return -1;
    }

    /* 推断输入尺寸 */
    ncnn::Mat in;
    ncnn::Extractor ex = net->create_extractor();
    ex.input("input", in);
    model_w = in.w; model_h = in.h;
    if (model_w == 0) model_w = input_w;
    if (model_h == 0) model_h = input_h;

    generate_anchors(model_w, model_h);
    printf("[DETECT] UltraFace loaded (%dx%d, %d anchors)\n",
           model_w, model_h, (int)anchors.size());
    return 0;
}

void face_detect_release(void)
{
    if (net) { delete net; net = NULL; }
    anchors.clear();
}

// 它是 main.c 调用的唯一入口函数，内部串起了检测全流程：
int face_detect_run(const unsigned char *rgb, int width, int height,
                    face_box_t *boxes, int max_boxes, int *num_boxes)
{
    if (!net) return -1;

    ncnn::Mat in;
    preprocess(rgb, width, height, in, model_w, model_h);  // ① 缩放+归一化

    ncnn::Extractor ex = net->create_extractor();          
    ex.input("input", in);                                 // ② 送入推理引擎

    ncnn::Mat scores, boxes_out;
    ex.extract("scores", scores);                          // ③ 取置信度表
    ex.extract("boxes",  boxes_out);                       // ③ 取偏移量表

    *num_boxes = postprocess(scores, boxes_out, width, height,
                              boxes, max_boxes);
    return 0;
}

} // extern "C"

#endif
