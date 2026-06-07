#!/usr/bin/env python3
"""
参考人脸特征提取 (PC 端运行一次)
用法: python gen_reference.py <照片.jpg> <输出.bin> [models目录]
示例: python gen_reference.py my_face.jpg reference.bin ../models

依赖: pip install onnxruntime pillow opencv-python numpy
"""

import sys
import os
import numpy as np
import onnxruntime as ort
from PIL import Image

FACE_DIM = 128

def detect_face(img_bgr, cascade_path=None):
    """用 OpenCV Haar Cascade 检测人脸, 返回最大的一张"""
    import cv2
    if cascade_path is None:
        cascade_path = cv2.data.haarcascades + 'haarcascade_frontalface_default.xml'
    cascade = cv2.CascadeClassifier(cascade_path)
    gray = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2GRAY)
    faces = cascade.detectMultiScale(gray, 1.1, 3, minSize=(40, 40))
    if len(faces) == 0:
        return None
    # 返回面积最大的脸
    best = max(faces, key=lambda r: r[2] * r[3])
    return best  # (x, y, w, h)

def extract_embedding(face_rgb, model_path):
    """用 MobileFaceNet ONNX 提取 128 维特征"""
    # resize 112x112
    img = Image.fromarray(face_rgb).resize((112, 112), Image.BILINEAR)
    arr = np.array(img).astype(np.float32)
    # 归一化: (pixel - 127.5) / 128 -> [-1, 1]
    arr = (arr - 127.5) / 128.0
    # HWC -> CHW -> NCHW
    arr = np.transpose(arr, (2, 0, 1))
    arr = np.expand_dims(arr, 0)

    session = ort.InferenceSession(model_path)
    out = session.run(None, {'input': arr})[0]
    emb = out[0].flatten()
    # L2 归一化
    norm = np.sqrt(np.sum(emb ** 2)) + 1e-8
    emb = emb / norm
    return emb.astype(np.float32)

def main():
    if len(sys.argv) < 3:
        print(f"用法: {sys.argv[0]} <照片.jpg> <输出.bin> [models目录]")
        sys.exit(1)

    img_path   = sys.argv[1]
    out_path   = sys.argv[2]
    model_dir  = sys.argv[3] if len(sys.argv) >= 4 else "../models"

    import cv2
    img = cv2.imread(img_path)
    if img is None:
        print(f"无法打开图片: {img_path}")
        sys.exit(1)
    print(f"图片: {img_path} ({img.shape[1]}x{img.shape[0]})")

    # 人脸检测
    face = detect_face(img)
    if face is None:
        print("未检测到人脸！请换一张正面照")
        sys.exit(1)
    x, y, w, h = face
    print(f"检测到人脸: ({x},{y}) {w}x{h}")

    # 裁剪人脸
    face_bgr = img[y:y+h, x:x+w]
    face_rgb = cv2.cvtColor(face_bgr, cv2.COLOR_BGR2RGB)

    # 提取特征
    model_path = os.path.join(model_dir, "mobilefacenet.onnx")
    emb = extract_embedding(face_rgb, model_path)
    print(f"特征向量: {len(emb)} dims, norm={np.sqrt(np.sum(emb**2)):.4f}")

    # 保存
    emb.tofile(out_path)
    print(f"已保存: {out_path} ({len(emb) * 4} bytes)")

if __name__ == "__main__":
    main()
