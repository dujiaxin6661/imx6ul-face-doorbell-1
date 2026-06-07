#ifndef FACE_RECOG_H
#define FACE_RECOG_H

#ifdef __cplusplus
extern "C" {
#endif

int  face_recog_init(void);
void face_recog_release(void);

int  face_recog_extract(const unsigned char *face_rgb,
                        int w, int h,
                        float *embedding);

float face_recog_compare(const float *a, const float *b, int dim);

#ifdef __cplusplus
}
#endif

#endif
