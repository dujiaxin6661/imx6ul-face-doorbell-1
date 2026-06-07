#ifndef FACE_DETECT_H
#define FACE_DETECT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int    x, y, w, h;
    float  score;
} face_box_t;

int  face_detect_init(void);
void face_detect_release(void);

int face_detect_run(const unsigned char *rgb, int width, int height,
                    face_box_t *boxes, int max_boxes, int *num_boxes);

#ifdef __cplusplus
}
#endif

#endif
