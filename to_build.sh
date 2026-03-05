#!/bin/bash

# 简单构建脚本
set -e

echo "=== begin to build tinyrpc ==="

# 设置环境
# export Protobuf_ROOT="/usr/local/protobuf"

rm -rf build/

rm -rf logs/

# 创建构建目录
mkdir -p build
cd build

# 配置和构建
cmake .. && make -j$(nproc)

echo "=== build success ==="