#!/bin/bash
# ====== ncnn 模型准备脚本 (在 Ubuntu PC 上运行) ======
# 用法: chmod +x prepare_ncnn_models.sh && ./prepare_ncnn_models.sh
#
# 功能: 下载 UltraFace ONNX 并转换为 ncnn 格式 (.param + .bin)
# 前提: 需先手动编译 onnx2ncnn 工具 (见 README.md)

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODEL_DIR="$SCRIPT_DIR/../models"
mkdir -p "$MODEL_DIR"

ONNX2NCNN="$HOME/ncnn/build-pc/tools/onnx/onnx2ncnn"

if [ ! -f "$ONNX2NCNN" ]; then
    echo "ERROR: onnx2ncnn not found at $ONNX2NCNN"
    echo "Please build it first:"
    echo "  cd ~/ncnn && mkdir build-pc && cd build-pc"
    echo "  cmake -DNCNN_BUILD_TOOLS=ON .. && make -j4"
    exit 1
fi

echo "=== 1/2 UltraFace Slim ==="
cd "$MODEL_DIR"

if [ ! -f "version-RFB-320_simplified.onnx" ]; then
    wget -O version-RFB-320_simplified.onnx \
        "https://github.com/Linzaer/Ultra-Light-Fast-Generic-Face-Detector-1MB/raw/master/models/onnx/version-RFB-320_simplified.onnx"
fi

"$ONNX2NCNN" version-RFB-320_simplified.onnx ultraface_slim.param ultraface_slim.bin
echo "  -> ultraface_slim.param + ultraface_slim.bin"

echo "=== 2/2 MobileFaceNet ==="
if [ -f "mobilefacenet.onnx" ]; then
    "$ONNX2NCNN" mobilefacenet.onnx mobilefacenet.param mobilefacenet.bin
    echo "  -> mobilefacenet.param + mobilefacenet.bin"
else
    echo "  SKIP: mobilefacenet.onnx not found, please place it in $MODEL_DIR"
fi

echo ""
echo "Done. Required files for deployment:"
ls -lh "$MODEL_DIR"/*.param "$MODEL_DIR"/*.bin 2>/dev/null
