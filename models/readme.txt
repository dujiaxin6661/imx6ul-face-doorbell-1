ultraface_slim.param是 UltraFace 模型结构：记录了每层卷积/激活/池化的拓扑关系，ncnn 靠它构建计算图

ultraface_slim.bin 是 UltraFace 模型权重：训练好的参数（卷积核数值等），ncnn 推理时实际运算的数据

mobilefacenet.param是MobileFaceNet 模型结构：112×112 RGB → 128 维特征向量的网络拓扑

mobilefacenet.bin是MobileFaceNet 模型权重：预训练人脸识别参数
