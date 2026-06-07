#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include "face_detect.h"

int  display_init(void);
void display_release(void);

void fb_clear(uint32_t color);
void fb_draw_pixel(int x, int y, uint32_t rgb888);
void fb_draw_rect(int x, int y, int w, int h, uint32_t color);
void fb_draw_string(int x, int y, const char *str, uint32_t color);
void fb_draw_char(int x, int y, char c, uint32_t color);
void fb_draw_face_box(int x, int y, int w, int h, uint32_t color);

void fb_show_camera_preview(const unsigned char *yuyv, int width, int height);
void fb_show_door_log(void);
void fb_add_log_entry(const char *name, int success);
void fb_flush(void);

/* match: 0=陌生人(蓝框) / 1=匹配成功(绿框) */
void display_update_screen(const unsigned char *yuyv,
                            int width, int height,
                            const face_box_t *boxes, int num_boxes,
                            int match, const char *result_name, float distance);

#endif
