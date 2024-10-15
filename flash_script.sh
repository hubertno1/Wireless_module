#!/bin/bash

# 检查是否已经设置了ESP-IDF环境
if [ -z "$IDF_PATH" ]; then
    echo "Setting up ESP-IDF environment ..."
    cd ..
    . ./esp-idf/export.sh
    cd - > /dev/null
else
    echo "ESP-IDF environment already set."
fi

echo "Building the project ..."

idf.py build

# 检查上一条命令是否成功
if [ $? -eq 0 ]; then
    echo "Build successful. Flashing the device ..."
    # 烧录并启动监控
    idf.py -p /dev/ttyACM0 flash monitor
else
    echo "Build failed. Please check the compilation errors."
fi
