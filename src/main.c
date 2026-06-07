#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

#include "config.h"
#include "v4l2_capture.h"
#include "display.h"
#include "face_detect.h"
#include "face_recog.h"
#include "gpio_control.h"
#include "utils.h"

static volatile int running = 1;

/* ======== 参考人脸特征 (从 reference.bin 加载) ======== */
static float ref_embedding[FACE_FEAT_DIM];
static int   ref_loaded = 0;

/* ======== 共享帧 ======== */
static unsigned char shared_frame[CAM_WIDTH * CAM_HEIGHT * 2];
static size_t        shared_frame_size = 0;
static int           shared_frame_ready = 0;
static pthread_mutex_t shared_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  shared_cond  = PTHREAD_COND_INITIALIZER;

/* ======== 识别结果 (共享给 UI) ======== */
static face_box_t last_boxes[5];
static int        last_num_boxes = 0;
static int        last_match = 0;
static float      last_distance = 999.0f;
static char       last_name[64] = "";
static pthread_mutex_t box_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t res_mutex  = PTHREAD_MUTEX_INITIALIZER;

/* ======== 节流控制 ======== */
static time_t last_open_time = 0;

/* ======== 信号处理 ======== */
static void signal_handler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM){
        running = 0;
    } 
}

/* ======== 加载参考特征 ======== */
static int load_reference(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[REF] Cannot open %s\n", path);
        return -1;
    }
    size_t n = fread(ref_embedding, sizeof(float), FACE_FEAT_DIM, fp);  // 读取参考人脸的二进制数据
    fclose(fp);
    if (n != FACE_FEAT_DIM) {
        fprintf(stderr, "[REF] Read %zu floats, expected %d\n", n, FACE_FEAT_DIM);
        return -1;
    }
    ref_loaded = 1;
    printf("[REF] Loaded reference vector (%d floats)\n", FACE_FEAT_DIM);
    return 0;
}

/* ======== 采集线程 ======== */
static void *capture_thread_func(void *arg)
{
    (void)arg;
    unsigned char *raw_data;
    size_t raw_size;
    while (running) {
        if (camera_get_frame(&raw_data, &raw_size) == 0) {
            pthread_mutex_lock(&shared_mutex);
            if (raw_size <= sizeof(shared_frame)) {
                memcpy(shared_frame, raw_data, raw_size);
                shared_frame_size = raw_size;
                shared_frame_ready = 1;
                pthread_cond_signal(&shared_cond);
            }
            pthread_mutex_unlock(&shared_mutex);
        }
        usleep(10000);
    }
    return NULL;
}

/* ======== 识别线程 (1:1 验证) ======== */
static void *recognition_thread_func(void *arg)
{
    (void)arg;
    unsigned char yuyv[CAM_WIDTH * CAM_HEIGHT * 2];
    unsigned char rgb[CAM_WIDTH * CAM_HEIGHT * 3];
    unsigned char face_rgb[112 * 112 * 3];
    face_box_t boxes[5];
    int num_boxes;
    float embedding[FACE_FEAT_DIM];

    while (running) {
        /* 等新帧 */
        pthread_mutex_lock(&shared_mutex);
        while (!shared_frame_ready && running) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;
            pthread_cond_timedwait(&shared_cond, &shared_mutex, &ts);
        }
        if (!running) { pthread_mutex_unlock(&shared_mutex); break; }
        memcpy(yuyv, shared_frame, shared_frame_size);
        shared_frame_ready = 0;
        pthread_mutex_unlock(&shared_mutex);

        yuyv_to_rgb24(yuyv, rgb, CAM_WIDTH, CAM_HEIGHT);

        /* 人脸检测 */
        if (face_detect_run(rgb, CAM_WIDTH, CAM_HEIGHT,
                             boxes, 5, &num_boxes) != 0 || num_boxes == 0) {
            pthread_mutex_lock(&box_mutex);
            last_num_boxes = 0;
            pthread_mutex_unlock(&box_mutex);
            usleep(50000);
            continue;
        }

        /* 挑最高分的脸 */
        face_box_t *best = &boxes[0];
        for (int i = 1; i < num_boxes; i++)
            if (boxes[i].score > best->score) best = &boxes[i];

        /* 裁剪 + 缩放到 112×112 */
        rgb24_crop_resize(rgb, CAM_WIDTH, CAM_HEIGHT,
                          best->x, best->y, best->w, best->h,
                          face_rgb, 112, 112);

        /* 提取特征 */
        if (face_recog_extract(face_rgb, 112, 112, embedding) != 0) {
            usleep(50000); continue;
        }
        l2_normalize(embedding, FACE_FEAT_DIM);

        /* 1:1 比对 */
        float dist = 999.0f;
        int is_match = 0;
        if (ref_loaded) {
            dist = face_recog_compare(ref_embedding, embedding, FACE_FEAT_DIM);
            is_match = (dist < FACE_RECOG_THRESH) ? 1 : 0;
        }

        /* 保存结果给 UI */
        pthread_mutex_lock(&box_mutex);
        last_num_boxes = num_boxes;
        memcpy(last_boxes, boxes, num_boxes * sizeof(face_box_t));
        pthread_mutex_unlock(&box_mutex);

        pthread_mutex_lock(&res_mutex);
        last_match = is_match;
        last_distance = dist;
        if (is_match)
            strncpy(last_name, "Authorized", sizeof(last_name));
        else
            snprintf(last_name, sizeof(last_name), "Unknown (%.2f)", dist);
        pthread_mutex_unlock(&res_mutex);

        /* 匹配成功: 开门 + 记录日志 */
        if (is_match) {
            time_t now = time(NULL);
            if (now - last_open_time > 3) {
                last_open_time = now;
                fb_add_log_entry("Authorized", 1);
                open_door();
                FILE *fp = fopen(DOOR_LOG_PATH, "a");
                if (fp) {
                    time_t t = time(NULL);
                    struct tm *tm = localtime(&t);
                    fprintf(fp, "%04d-%02d-%02d %02d:%02d:%02d MATCH SUCCESS\n",
                            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                            tm->tm_hour, tm->tm_min, tm->tm_sec);
                    fclose(fp);
                }
            }
        }

        usleep(100000);
    }
    return NULL;
}

/* ======== UI 线程 ======== */
static void *ui_thread_func(void *arg)
{
    (void)arg;
    while (running) {
        unsigned char yuyv[CAM_WIDTH * CAM_HEIGHT * 2] = {0};
        int has_frame = 0;

        pthread_mutex_lock(&shared_mutex);
        if (shared_frame_ready) {
            memcpy(yuyv, shared_frame, shared_frame_size);
            has_frame = 1;
        }
        pthread_mutex_unlock(&shared_mutex);

        /* 读取检测框 */
        face_box_t draw_boxes[5];
        int draw_count = 0;
        pthread_mutex_lock(&box_mutex);
        draw_count = last_num_boxes;
        memcpy(draw_boxes, last_boxes, draw_count * sizeof(face_box_t));
        pthread_mutex_unlock(&box_mutex);

        /* 读取识别结果 */
        int m; float d; char nm[64];
        pthread_mutex_lock(&res_mutex);
        m  = last_match;
        d  = last_distance;
        strncpy(nm, last_name, sizeof(nm));
        pthread_mutex_unlock(&res_mutex);

        if (has_frame)
            display_update_screen(yuyv, CAM_WIDTH, CAM_HEIGHT,
                                   draw_boxes, draw_count, m, nm, d);
        else
            display_update_screen(NULL, CAM_WIDTH, CAM_HEIGHT,
                                   draw_boxes, draw_count, m, nm, d);

        usleep(33000);
    }
    return NULL;
}

/* ======== 主函数 ======== */
int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("============================================\n");
    printf("  i.MX6UL Face Recognition Doorbell\n");
    printf("  Mode: 1:1 Verification\n");
    printf("============================================\n\n");

    /* 加载参考特征 */
    load_reference(REFERENCE_BIN);

    /* 初始化 */
    if (display_init() < 0)      { fprintf(stderr, "display init failed\n"); return -1; }
    if (camera_init() < 0)       { fprintf(stderr, "camera init failed\n"); display_release(); return -1; }
    if (face_detect_init() < 0)  { fprintf(stderr, "detect init failed\n"); camera_release(); display_release(); return -1; }
    if (face_recog_init() < 0)   { fprintf(stderr, "recog init failed\n"); face_detect_release(); camera_release(); display_release(); return -1; }
    if (gpio_init() < 0)         fprintf(stderr, "[WARN] GPIO init failed\n");

    /* 创建线程 */
    pthread_t tid_capture, tid_recog, tid_ui;
    pthread_create(&tid_capture, NULL, capture_thread_func, NULL);
    pthread_create(&tid_recog,   NULL, recognition_thread_func, NULL);
    pthread_create(&tid_ui,      NULL, ui_thread_func, NULL);

    printf("[MAIN] All threads started, Ctrl+C to exit\n\n");
    while (running) sleep(1);

    printf("\n[MAIN] Shutting down...\n");
    pthread_join(tid_capture, NULL);
    pthread_join(tid_recog,   NULL);
    pthread_join(tid_ui,      NULL);

    gpio_release();
    face_recog_release();
    face_detect_release();
    camera_release();
    display_release();
    printf("[MAIN] Clean exit\n");
    return 0;
}
