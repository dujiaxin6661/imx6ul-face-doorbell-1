========== ncnn 模型转换说明 ==========

目标平台: i.MX6UL (Cortex-A7, ARM NEON)
推理框架: ncnn (腾讯开源)
转换链路: PyTorch/预训练模型 → ONNX → ncnn (.param + .bin)


--- 步骤 1: 编译 onnx2ncnn 工具 (PC 端) ---

  cd ~/ncnn && mkdir build-pc && cd build-pc
  cmake -DNCNN_BUILD_TOOLS=ON -DNCNN_BUILD_EXAMPLES=OFF ..
  make -j4

  注意: 如果 cmake 报 "Could NOT find protobuf"，需先:
    sudo apt install -y libprotobuf-dev protobuf-compiler
  如果仍找不到，安装后删掉 build-pc 目录重新 cmake。


--- 步骤 2: 获取 ONNX 模型 ---

  UltraFace Slim (人脸检测):
    wget -O version-RFB-320_simplified.onnx \
      "https://github.com/Linzaer/Ultra-Light-Fast-Generic-Face-Detector-1MB/raw/master/models/onnx/version-RFB-320_simplified.onnx"

  MobileFaceNet (人脸识别):
    使用之前从 foamliu/MobileFaceNet 导出的 mobilefacenet.onnx
    (输入 112x112 RGB, 输出 128 维特征向量)


--- 步骤 3: ONNX → ncnn 转换 ---

  ~/ncnn/build-pc/tools/onnx/onnx2ncnn version-RFB-320_simplified.onnx ultraface_slim.param ultraface_slim.bin
  ~/ncnn/build-pc/tools/onnx/onnx2ncnn mobilefacenet.onnx mobilefacenet.param mobilefacenet.bin

  输出: *.param (模型结构) + *.bin (权重数据)
  或者直接运行一键脚本: ./prepare_ncnn_models.sh


--- 步骤 4: 交叉编译 ncnn 库 (ARM 目标板用) ---

  cd ~/ncnn && mkdir build-arm && cd build-arm
  cmake -DCMAKE_TOOLCHAIN_FILE=../toolchains/arm-linux-gnueabihf.toolchain.cmake \
        -DNCNN_BUILD_TOOLS=OFF -DNCNN_BUILD_EXAMPLES=OFF -DNCNN_OPENMP=OFF \
        -DCMAKE_INSTALL_PREFIX=~/ncnn-arm ..
  make -j4 && make install

  注意:
  - 编译器名可能需要改成 arm-buildroot-linux-gnueabihf-gcc/g++ (修改 toolchain 文件)
  - i.MX6UL 单核必须关 OpenMP (-DNCNN_OPENMP=OFF)


--- 步骤 5: 配置文件路径 ---

  编辑项目 Makefile:
    NCNN_INC = /home/jason/ncnn-arm/include
    NCNN_LIB = /home/jason/ncnn-arm/lib

  确认 config.h 中模型路径:
    ULTRAF_PARAM  -> ./models/ultraface_slim.param
    ULTRAF_BIN    -> ./models/ultraface_slim.bin
    MFNET_PARAM   -> ./models/mobilefacenet.param
    MFNET_BIN     -> ./models/mobilefacenet.bin


--- 板端部署文件清单 ---

  只需要 4 个模型文件:
    models/ultraface_slim.param
    models/ultraface_slim.bin
    models/mobilefacenet.param
    models/mobilefacenet.bin
