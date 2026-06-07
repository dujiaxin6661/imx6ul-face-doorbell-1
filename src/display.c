#include "display.h"
#include "config.h"
#include "utils.h"
#include "font_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <time.h>

/* ======== 颜色常量 ======== */
static const uint32_t COLOR_WHITE     = 0x00FFFFFF;
static const uint32_t COLOR_GRAY      = 0x00666666;
static const uint32_t COLOR_DARK_GRAY = 0x00333333;
static const uint32_t COLOR_DARK_BG   = 0x001A1A2E;
static const uint32_t COLOR_TITLE_BG  = 0x0016224A;
static const uint32_t COLOR_GREEN     = 0x0000FF00;
static const uint32_t COLOR_YELLOW    = 0x0000CCCC;
static const uint32_t COLOR_CYAN      = 0x00CCCC00;
static const uint32_t COLOR_BLUE      = 0x000000FF;

/* ======== 布局常量 ======== */
#define TITLE_H        36
#define LOG_AREA_Y     516
#define LOG_AREA_H     80
#define PREVIEW_X      0
#define PREVIEW_Y      TITLE_H
#define PANEL_X        648
#define PANEL_Y        TITLE_H
#define PANEL_W        (FB_SCREEN_WIDTH - PANEL_X)

/* ======== 状态变量 ======== */
static int fbfd = -1;
static unsigned char *fb_buffer = NULL;
static unsigned char *back_buffer = NULL;
static int use_double_buffer = 1;
static struct fb_var_screeninfo fb_var;
static struct fb_fix_screeninfo fb_fix;
static size_t screensize;

static int touch_fd = -1;

static char log_entries[MAX_LOG_ENTRIES][128];
static int  log_count = 0;

/* ======== 低级 Framebuffer 操作 ======== */
static inline uint32_t rgb_to_fb(uint32_t rgb888)
{
    uint8_t r = (rgb888 >> 16) & 0xFF;
    uint8_t g = (rgb888 >> 8) & 0xFF;
    uint8_t b = rgb888 & 0xFF;
    return (b << 16) | (g << 8) | r;
}

static inline unsigned char *get_draw_buf(void)
{
    return (use_double_buffer && back_buffer) ? back_buffer : fb_buffer;
}

void fb_draw_pixel(int x, int y, uint32_t rgb888)
{
    if (!fb_buffer || x < 0 || (unsigned int)x >= fb_var.xres ||
        y < 0 || (unsigned int)y >= fb_var.yres) return;
    uint32_t color = rgb_to_fb(rgb888);
    unsigned char *buf = get_draw_buf();
    size_t offset = y * fb_fix.line_length + x * 4;
    *(uint32_t *)(buf + offset) = color;
}

void fb_draw_rect(int x, int y, int w, int h, uint32_t color)
{
    if (!fb_buffer) return;
    if ((unsigned int)(x + w) > fb_var.xres) w = fb_var.xres - x;
    if ((unsigned int)(y + h) > fb_var.yres) h = fb_var.yres - y;
    if (w <= 0 || h <= 0) return;
    unsigned char *buf = get_draw_buf();
    uint32_t fb_color = rgb_to_fb(color);
    for (int i = y; i < y + h; i++) {
        uint32_t *line = (uint32_t *)(buf + i * fb_fix.line_length);
        for (int j = x; j < x + w; j++) line[j] = fb_color;
    }
}

void fb_clear(uint32_t color) { fb_draw_rect(0, 0, fb_var.xres, fb_var.yres, color); }

void fb_draw_char(int x, int y, char c, uint32_t color)
{
    if (c < 32 || c > 126) return;
    const unsigned char *glyph = font_8x16[c - 32];
    for (int row = 0; row < 16; row++)
        for (int col = 0; col < 8; col++)
            if (glyph[row] & (0x80 >> col))
                fb_draw_pixel(x + col, y + row, color);
}

void fb_draw_string(int x, int y, const char *str, uint32_t color)
{
    if (!str) return;
    int cursor = x;
    while (*str && (unsigned int)cursor < fb_var.xres - 8) {
        if (*str == '\n') { y += 18; cursor = x; str++; continue; }
        fb_draw_char(cursor, y, *str++, color);
        cursor += 8;
    }
}

void fb_draw_face_box(int x, int y, int w, int h, uint32_t color)
{
    fb_draw_rect(x, y, w, 4, color);
    fb_draw_rect(x, y + h - 4, w, 4, color);
    fb_draw_rect(x, y, 4, h, color);
    fb_draw_rect(x + w - 4, y, 4, h, color);
}

void fb_flush(void)
{
    if (use_double_buffer && back_buffer)
        memcpy(fb_buffer, back_buffer, screensize);
}

/* ======== Framebuffer 初始化 ======== */
int display_init(void)
{
    fbfd = open(FB_DEVICE, O_RDWR);
    if (fbfd < 0) { perror("open fb0"); return -1; }

    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &fb_var) < 0 ||
        ioctl(fbfd, FBIOGET_FSCREENINFO, &fb_fix) < 0) {
        perror("fb ioctl"); close(fbfd); return -1;
    }

    screensize = fb_fix.line_length * fb_var.yres;
    fb_buffer = (unsigned char *)mmap(0, screensize, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, fbfd, 0);
    if ((intptr_t)fb_buffer == -1) { perror("mmap fb"); close(fbfd); return -1; }

    if (use_double_buffer) {
        back_buffer = (unsigned char *)malloc(screensize);
        if (!back_buffer) use_double_buffer = 0;
    }

    printf("[FB] %dx%d %dbpp line=%d\n",
           fb_var.xres, fb_var.yres, fb_var.bits_per_pixel, fb_fix.line_length);
    fb_clear(COLOR_DARK_BG);

    touch_fd = open(TOUCH_DEVICE, O_RDONLY | O_NONBLOCK);
    if (touch_fd < 0) perror("open touch");

    return 0;
}

void display_release(void)
{
    if (back_buffer) { free(back_buffer); back_buffer = NULL; }
    if (fb_buffer && fb_buffer != MAP_FAILED) { munmap(fb_buffer, screensize); fb_buffer = NULL; }
    if (fbfd >= 0) { close(fbfd); fbfd = -1; }
    if (touch_fd >= 0) { close(touch_fd); touch_fd = -1; }
}

/* ======== 业务 UI 绘制 ======== */
static void draw_title_bar(void)
{
    fb_draw_rect(0, 0, FB_SCREEN_WIDTH, TITLE_H, COLOR_TITLE_BG);
    fb_draw_string(12, 10, "i.MX6UL Face Doorbell", COLOR_WHITE);

    char time_str[32];
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d",
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    fb_draw_string(FB_SCREEN_WIDTH - 90, 10, time_str, COLOR_CYAN);
}

void fb_show_camera_preview(const unsigned char *yuyv, int width, int height)
{
    if (!fb_buffer || !yuyv) return;
    unsigned char *dst = (unsigned char *)get_draw_buf()
                         + PREVIEW_Y * fb_fix.line_length + PREVIEW_X * 4;
    yuyv_to_rgb32(yuyv, dst, width, height, fb_fix.line_length);
}

void fb_add_log_entry(const char *name, int success)
{
    if (log_count >= MAX_LOG_ENTRIES) {
        for (int i = 0; i < MAX_LOG_ENTRIES - 1; i++)
            strncpy(log_entries[i], log_entries[i + 1], sizeof(log_entries[0]));
        log_count = MAX_LOG_ENTRIES - 1;
    }
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char ts[16];
    snprintf(ts, sizeof(ts), "%02d:%02d:%02d",
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    snprintf(log_entries[log_count], sizeof(log_entries[0]),
             "%s %s %s", ts, name ? name : "Unknown", success ? "OPEN" : "DENY");
    log_count++;
}

void fb_show_door_log(void)
{
    int ly = LOG_AREA_Y;
    fb_draw_rect(0, ly, FB_SCREEN_WIDTH, LOG_AREA_H, COLOR_DARK_GRAY);
    fb_draw_rect(0, ly, FB_SCREEN_WIDTH, 2, COLOR_BLUE);
    fb_draw_string(12, ly + 4, "--- Door Log ---", COLOR_YELLOW);
    for (int i = 0; i < log_count && i < 7; i++) {
        int idx = log_count - 1 - i;
        fb_draw_string(16, ly + 22 + i * 8, log_entries[idx], COLOR_WHITE);
    }
}

static void draw_panel(int match, const char *result_name, float distance)
{
    fb_draw_rect(PANEL_X - 2, PANEL_Y, 2, LOG_AREA_Y - PANEL_Y, COLOR_DARK_GRAY);

    fb_draw_string(PANEL_X + 10, PANEL_Y + 10, "--- Status ---", COLOR_CYAN);

    /* 识别结果 */
    fb_draw_string(PANEL_X + 10, PANEL_Y + 38, "Result:", COLOR_WHITE);
    uint32_t rc = match ? COLOR_GREEN : COLOR_BLUE;
    fb_draw_string(PANEL_X + 80, PANEL_Y + 38,
                   result_name ? result_name : "Scanning...", rc);

    char buf[48];
    snprintf(buf, sizeof(buf), "Dist: %.3f", distance);
    fb_draw_string(PANEL_X + 10, PANEL_Y + 58, buf, COLOR_WHITE);

    snprintf(buf, sizeof(buf), "Status: %s", match ? "MATCH" : "UNKNOWN");
    fb_draw_string(PANEL_X + 10, PANEL_Y + 78, buf, rc);

    /* 系统信息 */
    int iy = PANEL_Y + 120;
    fb_draw_string(PANEL_X + 10, iy,      "Model: UltraFace",      COLOR_GRAY);
    fb_draw_string(PANEL_X + 10, iy + 20, "Recog: MobileFaceNet",  COLOR_GRAY);
    fb_draw_string(PANEL_X + 10, iy + 40, "Verification: 1:1",     COLOR_GRAY);
    snprintf(buf, sizeof(buf), "Threshold: %.1f", FACE_RECOG_THRESH);
    fb_draw_string(PANEL_X + 10, iy + 60, buf, COLOR_GRAY);
}

void display_update_screen(const unsigned char *yuyv, int width, int height,
                            const face_box_t *boxes, int num_boxes,
                            int match, const char *result_name, float distance)
{
    fb_clear(COLOR_DARK_BG);
    draw_title_bar();

    if (yuyv)
        fb_show_camera_preview(yuyv, width, height);

    /* 人脸框: 匹配=绿色, 陌生人=蓝色 */
    uint32_t box_color = match ? COLOR_GREEN : COLOR_BLUE;
    for (int i = 0; i < num_boxes; i++) {
        int bx = PREVIEW_X + boxes[i].x;
        int by = PREVIEW_Y + boxes[i].y;
        fb_draw_face_box(bx, by, boxes[i].w, boxes[i].h, box_color);
    }

    draw_panel(match, result_name, distance);
    fb_show_door_log();
    fb_flush();
}
