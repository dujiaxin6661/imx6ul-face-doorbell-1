基于 i.MX6UL 的嵌入式人脸识别门禁终端

在 i.MX6UL (Cortex-A7, 单核, 无 NPU) 平台部署真实神经网络模型实现人脸识别门禁。
推理引擎: ncnn (腾讯开源, ARM NEON 优化)
人脸检测: UltraFace Slim (~300KB, ncnn CPU 推理)
人脸识别: MobileFaceNet (~4MB, ncnn CPU 推理, 128 维特征向量)

设计本地人脸特征库管理方案: 采集 5 帧人脸提取特征并存储平均值，实时比对欧氏距离。

集成 GT911 电容触摸屏，基于帧缓冲直接绘图实现人脸注册、删除用户、开门日志等交互功能。

通过 sysfs GPIO (/sys/class/gpio/) 控制 LED 模拟门锁，预留电磁锁接口。

复用前序项目的 V4L2 采集与帧缓冲显示模块。


技术栈: C + C++ (extern "C" 混合编译) | ncnn | V4L2 | Framebuffer | sysfs GPIO | GT911 触摸屏
编译器: arm-buildroot-linux-gnueabihf-gcc/g++ (Buildroot 工具链)
部署方式: ADB push + shell


模型准备:
  1. 在 Ubuntu PC 上编译 ncnn 工具链 (onnx2ncnn)
  2. 下载/准备 UltraFace + MobileFaceNet 的 ONNX 文件
  3. 运行 scripts/prepare_ncnn_models.sh 转换为 .param + .bin
  4. 交叉编译 ncnn 到 ARM: cmake -DCMAKE_TOOLCHAIN_FILE=... -DNCNN_OPENMP=OFF
  5. 修改 Makefile 中 NCNN_INC / NCNN_LIB 路径

编译 & 运行:
  make           # 交叉编译 (i.MX6UL 目标板)
  make MOCK=1    # PC 逻辑测试 (跳过硬件 + 模型)
  make run       # 编译 + adb push + 运行
