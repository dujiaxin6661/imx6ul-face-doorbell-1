#ifndef CONFIG_H
#define CONFIG_H

/*
  所有硬件配置集中管理 -- 摄像头、GPIO、屏幕、识别参数等
  目标平台: i.MX6UL, Cortex-A7, 无 NPU
  模式: 1:1 人脸验证 (与 reference.bin 对比)
*/

/* ====== 摄像头配置 ====== */
#define CAM_DEVICE          "/dev/video1"
#define CAM_WIDTH           640   // 摄像头捕获尺寸
#define CAM_HEIGHT          480
#define CAM_BUFFER_COUNT    3

/* ====== 显示屏配置 ====== */
#define FB_DEVICE           "/dev/fb0"
#define FB_BITS_PER_PIXEL   32
#define FB_SCREEN_WIDTH     1024
#define FB_SCREEN_HEIGHT    600

/* ====== 触摸屏配置 ====== */
#define TOUCH_DEVICE        "/dev/input/event2"
#define TOUCH_WIDTH         1024
#define TOUCH_HEIGHT        600

/* ====== GPIO 门控配置 ====== */
#define GPIO_CHIP           "/dev/gpiochip0"
#define GPIO_LINE           17
#define GPIO_OPEN_DURATION  2

/* ====== 人脸检测 (ncnn UltraFace) ====== */
#define ULTRAF_PARAM        "./models/ultraface_slim.param"
#define ULTRAF_BIN          "./models/ultraface_slim.bin"

/* ====== 人脸识别 (ncnn MobileFaceNet, 1:1 验证) ====== */
#define MFNET_PARAM         "./models/mobilefacenet.param"
#define MFNET_BIN           "./models/mobilefacenet.bin"
#define FACE_FEAT_DIM       128
#define FACE_RECOG_THRESH   1.5f

/* ====== 参考人脸特征 (gen_reference 工具生成, 512 字节) ====== */
#define REFERENCE_BIN       "./reference.bin"

/* ====== 门禁日志 ====== */
#define DOOR_LOG_PATH       "./door_log.txt"
#define MAX_LOG_ENTRIES     10

/* ====== 共享帧队列 ====== */
#define FRAME_QUEUE_SIZE    3

/* ====== 调试开关 ====== */
#ifndef MOCK_MODE
#define MOCK_MODE           0
#endif

#endif
