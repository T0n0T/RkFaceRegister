#arm-toolchain.cmake

set(USERNAME $ENV{USER})
set(CHRROT_NAME "ubuntu22-arm")
message(STATUS "当前用户名: ${USERNAME}")

#1. 指定系统名称
set(CMAKE_SYSTEM_NAME Linux)

#2. 指定目标架构
set(CMAKE_SYSTEM_PROCESSOR aarch64)

#3. 指定交叉编译器
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

#4. 指定目标根文件系统
#set(CMAKE_SYSROOT /home/${USERNAME}/chroot/${CHRROT_NAME})

#5. 告诉 CMake 在查找时以 sysroot 为优先，并设置查找策略
#   这样 CMake 会优先在 sysroot 下查找头文件和库，不会误用宿主机 x86_64 上的
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)   # 不在 sysroot 中查找可执行程序
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)    # 仅在 sysroot 中查找库文件
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)    # 仅在 sysroot 中查找头文件
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)    # 仅在 sysroot 中查找 CMake 包

#6. 强制 Disable TRY_RUN（因为 aarch64 二进制无法在 x86_64 主机上运行）
#   以下两行用于跳过 CMake 的 TRY_RUN 测试
set(THREADS_PTHREAD_ARG "0" CACHE STRING "Result from TRY_RUN" FORCE)

#添加下面几行，让 find_package 找到 aarch64 的 pkg-config
set(ENV{PKG_CONFIG_SYSROOT_DIR} ${CMAKE_SYSROOT})
set(ENV{PKG_CONFIG_LIBDIR}
    "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig"
)
