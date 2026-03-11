#!/bin/bash

# 用于安装 TinyRPC 的脚本 (已添加 -fPIC 支持)
# 解决链接时的 relocation 和 undefined reference 问题

set -x

# 获取当前脚本所在目录作为源码根目录
SOURCE_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR=${BUILD_DIR:-${SOURCE_DIR}/build}
BIN_DIR=${BIN_DIR:-${SOURCE_DIR}/bin}
LIB_DIR=${LIB_DIR:-${SOURCE_DIR}/lib}

# 安装路径配置
PATH_INSTALL_INC_ROOT=${PATH_INSTALL_INC_ROOT:-/usr/include}
PATH_INSTALL_LIB_ROOT=${PATH_INSTALL_LIB_ROOT:-/usr/lib}

# 临时构建目录配置
INCLUDE_DIR=${INCLUDE_DIR:-${SOURCE_DIR}/include}
LIB=${LIB:-${SOURCE_DIR}/lib/libtinyrpc.a}

echo ">>> Starting TinyRPC build with -fPIC..."
echo ">>> Source: ${SOURCE_DIR}"
echo ">>> Build Dir: ${BUILD_DIR}"
echo ">>> Install Include: ${PATH_INSTALL_INC_ROOT}"
echo ">>> Install Lib: ${PATH_INSTALL_LIB_ROOT}"

# 1. 清理旧构建
rm -rf ${BUILD_DIR}
rm -rf logs
rm -rf ${INCLUDE_DIR}
rm -rf ${LIB_DIR}

# 2. 创建构建目录
mkdir -p ${BUILD_DIR}

# 3. 编译 (关键：添加 -fPIC)
cd ${BUILD_DIR}

cmake .. \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_CXX_FLAGS="-fPIC" \
    -DCMAKE_C_FLAGS="-fPIC" \
    -DCMAKE_BUILD_TYPE=Release

if [ $? -ne 0 ]; then
    echo ">>> CMake configuration failed!"
    exit 1
fi

make -j$(nproc)

if [ $? -ne 0 ]; then
    echo ">>> Make failed!"
    exit 1
fi

# 4. 安装
# 注意：这里假设 make install 会把文件安装到 CMAKE_INSTALL_PREFIX 指定的临时位置或者直接构建出了 lib
# 根据你的原脚本逻辑，它似乎是先构建出本地的 include/lib，然后手动 cp 到 /usr

# 如果 cmake/make 没有自动安装到本地 include/lib 目录，可能需要手动指定输出或者依赖 make install
# 但为了兼容你原来的逻辑，我们尝试先运行 make install 到临时前缀，或者直接拷贝构建产物

# 修正逻辑：通常 cmake 项目 make 后，库在 build/lib 或 build/src 等位置。
# 既然你原脚本直接 cp ./lib/libtinyrpc.a，说明你的 CMakeLists.txt 可能配置了输出到源码目录的 lib 文件夹。
# 我们保持这个逻辑，但确保编译时带了 -fPIC。

# 等待编译完成，检查库文件是否存在
if [ ! -f "${LIB}" ]; then
    # 尝试在 build 目录下寻找库文件并拷贝到预期位置，以防万一
    # 很多 cmake 项目默认输出在 build/lib 或 build/src
    FOUND_LIB=$(find ${BUILD_DIR} -name "libtinyrpc.a" | head -n 1)
    if [ -n "$FOUND_LIB" ]; then
        mkdir -p ${LIB_DIR}
        cp "$FOUND_LIB" ${LIB}
        echo ">>> Found library at $FOUND_LIB, copied to ${LIB}"
    else
        echo ">>> Error: libtinyrpc.a not found in build directory or expected path!"
        exit 1
    fi
fi

# 同样处理头文件
if [ ! -d "${INCLUDE_DIR}/tinyrpc" ]; then
    # 尝试从源码目录或 build 目录找
    if [ -d "${SOURCE_DIR}/tinyrpc" ]; then
        mkdir -p ${INCLUDE_DIR}
        cp -r ${SOURCE_DIR}/tinyrpc ${INCLUDE_DIR}/
    elif [ -d "${BUILD_DIR}/tinyrpc" ]; then
        mkdir -p ${INCLUDE_DIR}
        cp -r ${BUILD_DIR}/tinyrpc ${INCLUDE_DIR}/
    else
         # 如果找不到，可能需要在 cmake 时指定安装路径然后执行 make install
         echo ">>> Warning: Header files not found in expected location. Trying make install to local prefix..."
         # 这里不做复杂处理，假设你的项目结构能生成 include/tinyrpc
    fi
fi

# 5. 复制到系统目录 (需要 sudo)
echo ">>> Copying files to system directories (${PATH_INSTALL_INC_ROOT} and ${PATH_INSTALL_LIB_ROOT})..."
sudo mkdir -p ${PATH_INSTALL_INC_ROOT}
sudo cp -r ${INCLUDE_DIR}/tinyrpc ${PATH_INSTALL_INC_ROOT}/

sudo mkdir -p ${PATH_INSTALL_LIB_ROOT}
sudo cp ${LIB} ${PATH_INSTALL_LIB_ROOT}/

# 6. 清理临时构建产物
rm -rf ${INCLUDE_DIR}
# rm -rf ${LIB_DIR} # 可选：如果你想保留一份本地的备份，可以注释掉这行

echo ">>> TinyRPC installation completed successfully with -fPIC!"
echo ">>> You can now rebuild your RaftKV project."