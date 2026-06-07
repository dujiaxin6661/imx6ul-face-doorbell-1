# ====== 交叉编译工具链 (i.MX6UL, Cortex-A7, armhf) ======
CC  = arm-buildroot-linux-gnueabihf-gcc
CXX = arm-buildroot-linux-gnueabihf-g++

# ====== 目标文件名 ======
TARGET = face_doorbell

# ====== 源文件 ======
SRC_DIR = src
C_SRC   = $(wildcard $(SRC_DIR)/*.c)
CPP_SRC = $(wildcard $(SRC_DIR)/*.cpp)
C_OBJ   = $(C_SRC:.c=.o)
CPP_OBJ = $(CPP_SRC:.cpp=.o)

# ====== ncnn 路径 (先交叉编译 ncnn 后, 填入实际路径) ======
NCNN_INC = /home/jason/ncnn-arm/include
NCNN_LIB = /home/jason/ncnn-arm/lib

# ====== 编译选项 ======
CFLAGS   = -Wall -O2 -D_GNU_SOURCE -I. -I$(SRC_DIR)
CXXFLAGS = -Wall -O2 -std=c++11 -D_GNU_SOURCE -I. -I$(SRC_DIR)

# Mock 模式
ifdef MOCK
    CFLAGS   += -DMOCK_MODE=1
    CXXFLAGS += -DMOCK_MODE=1
endif

# ncnn (非 mock 时启用)
ifndef MOCK
    CFLAGS   += -I$(NCNN_INC)
    CXXFLAGS += -I$(NCNN_INC)
    LDFLAGS  += -L$(NCNN_LIB) -lncnn
endif

# LVGL 开关
ifdef LVGL
    CFLAGS   += -DUSE_LVGL
    CXXFLAGS += -DUSE_LVGL
    LDFLAGS  += -llvgl
endif

# ====== 通用链接库 ======
LDFLAGS += -lpthread -lm -lrt -lstdc++

# ====== 目标路径 (ADB 部署) ======
REMOTE_PATH = /tmp/

# ====== 编译规则 ======
all: $(TARGET)

$(TARGET): $(C_OBJ) $(CPP_OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS)
	@echo "编译完成: $(TARGET)"

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ====== 部署 ======
push: $(TARGET)
	adb push $(TARGET) $(REMOTE_PATH)
	adb shell "chmod +x $(REMOTE_PATH)$(TARGET)"
	@echo "已推送到板子: $(REMOTE_PATH)$(TARGET)"

run: push
	@echo "开始在板子上运行..."
	adb shell "cd $(REMOTE_PATH) && ./$(TARGET)"

mock:
	$(MAKE) MOCK=1

clean:
	rm -f $(SRC_DIR)/*.o $(TARGET)
	@echo "已清理"

.PHONY: all push run mock clean
