# 用 Docker + QEMU 调试 Linux 2.6.24

这篇笔记记录如何在现代 Linux 主机上编译并运行 Linux 2.6.24。目标不是制作完整发行版，而是得到一个可以启动到 shell、可以用 GDB 调试的最小实验环境。

最终效果：

```text
qemu-system-i386 -> Linux 2.6.24 -> initramfs -> tinysh#
```

已验证环境：

```text
Kernel source: Linux 2.6.24
Target arch:   i386
Builder image: linux-2.6.24-builder:latest
Builder base:  Debian GNU/Linux 8 (jessie)
Compiler:      gcc 4.9.2
Make:          GNU Make 4.0
QEMU:          qemu-system-i386
```

## 为什么需要 Docker

Linux 2.6.24 发布于 2008 年，直接用现代宿主机工具链会遇到兼容问题。

我在宿主机上试过 `gcc 15.2.0` 和 `make 4.4.1`，主要问题包括：

```text
Makefile: mixed implicit and normal rules
false/bool is a keyword with -std=c23
impossible constraint in asm
```

因此更稳妥的做法是用旧发行版容器固定工具链。本文使用 Debian Jessie 中的 `gcc 4.9.2` 和 `make 4.0`。

## 目录约定

下文默认从 Linux 2.6.24 源码根目录执行命令。

构建产物放在 `/tmp`，避免污染源码树：

```text
/tmp/linux-2.6.24-build                 内核构建目录
/tmp/linux-2.6.24-build/vmlinux         带调试符号的内核 ELF
/tmp/linux-2.6.24-build/arch/x86/boot/bzImage
/tmp/linux-2.6.24-initramfs             initramfs staging 目录
/tmp/linux-2.6.24-initramfs.cpio.gz     initramfs 镜像
```

## 准备 Docker 镜像

配套 Dockerfile 在本目录：

```text
notes/linux-2.6.24/Dockerfile
```

从源码根目录构建镜像：

```bash
docker build -t linux-2.6.24-builder:latest -f notes/linux-2.6.24/Dockerfile .
```

检查版本：

```bash
docker run --rm linux-2.6.24-builder:latest bash -lc '
  cat /etc/os-release
  gcc --version | head -n1
  make --version | head -n1
  ld -v
'
```

期望工具链版本：

```text
Debian GNU/Linux 8 (jessie)
gcc 4.9.2
GNU Make 4.0
GNU ld 2.25
```

## 应用源码兼容补丁

当前源码树已经应用了这些补丁。若从干净的 Linux 2.6.24 开始，需要先做同等修改。

补丁目的：

```text
Makefile: 兼容 GNU Make 4.x 的规则解析
irq_32.h: 让 do_IRQ 的声明与 fastcall 定义一致
Makefile_32: 正确把 elf_i386 传给 linker，而不是直接传给 gcc
vsyscall_32.lds.S: 处理 out-of-tree 构建时 VDSO_PRELINK_asm 没展开的问题
mutex.c: 避免 GCC 4.9 下 slowpath 符号没有生成导致 vmlinux 链接失败
```

查看当前补丁：

```bash
git diff -- \
  Makefile \
  include/asm-x86/irq_32.h \
  arch/x86/kernel/Makefile_32 \
  arch/x86/kernel/vsyscall_32.lds.S \
  kernel/mutex.c
```

如果要导出 patch 文件：

```bash
git diff -- \
  Makefile \
  include/asm-x86/irq_32.h \
  arch/x86/kernel/Makefile_32 \
  arch/x86/kernel/vsyscall_32.lds.S \
  kernel/mutex.c \
  > notes/linux-2.6.24/linux-2.6.24-build-compat.patch
```

## 编译内核

生成 i386 默认配置：

```bash
mkdir -p /tmp/linux-2.6.24-build

docker run --rm \
  -u "$(id -u):$(id -g)" \
  -v "$PWD:/src" \
  -v /tmp/linux-2.6.24-build:/build \
  -w /src \
  linux-2.6.24-builder:latest \
  bash -lc 'make O=/build ARCH=i386 defconfig'
```

打开调试符号和 frame pointer：

```bash
sed -i \
  's/# CONFIG_DEBUG_INFO is not set/CONFIG_DEBUG_INFO=y/; s/# CONFIG_FRAME_POINTER is not set/CONFIG_FRAME_POINTER=y/' \
  /tmp/linux-2.6.24-build/.config

docker run --rm \
  -u "$(id -u):$(id -g)" \
  -v "$PWD:/src" \
  -v /tmp/linux-2.6.24-build:/build \
  -w /src \
  linux-2.6.24-builder:latest \
  bash -lc "yes '' | make O=/build ARCH=i386 oldconfig"
```

编译 `bzImage`：

```bash
docker run --rm \
  -u "$(id -u):$(id -g)" \
  -v "$PWD:/src" \
  -v /tmp/linux-2.6.24-build:/build \
  -w /src \
  linux-2.6.24-builder:latest \
  bash -lc 'make O=/build ARCH=i386 -j2 bzImage'
```

编译成功后检查产物：

```bash
ls -lh \
  /tmp/linux-2.6.24-build/arch/x86/boot/bzImage \
  /tmp/linux-2.6.24-build/vmlinux \
  /tmp/linux-2.6.24-build/System.map

file /tmp/linux-2.6.24-build/vmlinux
```

期望看到：

```text
ELF 32-bit LSB executable, Intel i386, statically linked, with debug_info, not stripped
```

## 制作 initramfs

这里没有直接用 BusyBox。原因是 Debian Jessie amd64 镜像里的 `/bin/busybox` 是 x86_64 静态程序，不能作为 i386 内核的 `/init`。

为了减少依赖，我使用一个无 libc 的 32 位静态 tiny shell。它只通过 `int 0x80` 调 Linux i386 syscall，内置少量命令：

```text
help cat cd echo exec int3 ls mount panic poweroff ps pwd reboot sync uname
```

源码在：

```text
notes/linux-2.6.24/tinysh.c
```

编译 `/init`：

```bash
mkdir -p /tmp/linux-2.6.24-initramfs

docker run --rm \
  -u "$(id -u):$(id -g)" \
  -v "$PWD:/src" \
  -v /tmp/linux-2.6.24-initramfs:/initramfs \
  -w /src \
  linux-2.6.24-builder:latest \
  bash -lc \
  'gcc -m32 -fno-stack-protector -fno-pic -nostdlib -static -Wl,-m,elf_i386 -Wl,-e,_start -o /initramfs/init notes/linux-2.6.24/tinysh.c'

chmod 0755 /tmp/linux-2.6.24-initramfs/init
```

确认它是 32 位静态 ELF：

```bash
file /tmp/linux-2.6.24-initramfs/init
```

打包 initramfs：

```bash
(
  cd /tmp/linux-2.6.24-initramfs
  find . -print | cpio -o -H newc | gzip -9 > /tmp/linux-2.6.24-initramfs.cpio.gz
)
```

检查内容：

```bash
gzip -cd /tmp/linux-2.6.24-initramfs.cpio.gz | cpio -t
```

期望输出：

```text
.
init
```

## 用 QEMU 启动

启动到串口 shell：

```bash
qemu-system-i386 \
  -m 256M \
  -kernel /tmp/linux-2.6.24-build/arch/x86/boot/bzImage \
  -initrd /tmp/linux-2.6.24-initramfs.cpio.gz \
  -append 'console=ttyS0 rdinit=/init nokaslr' \
  -nographic -no-reboot
```

启动成功后会看到：

```text
Linux 2.6.24 tiny initramfs shell
builtins: help cat cd echo exec int3 ls mount panic poweroff ps pwd reboot sync uname
tinysh#
```

验证：

```text
tinysh# uname
Linux version 2.6.24 ...
tinysh# pwd
/
tinysh# ls /proc
...
tinysh# poweroff
```

退出 QEMU 的方式：

```text
tinysh# poweroff
```

或者在 `-nographic` 模式下按 `Ctrl-a x`。

## 用 GDB 调试

QEMU 自带 GDB stub。启动时加 `-s -S`：

```bash
qemu-system-i386 \
  -m 256M \
  -kernel /tmp/linux-2.6.24-build/arch/x86/boot/bzImage \
  -initrd /tmp/linux-2.6.24-initramfs.cpio.gz \
  -append 'console=ttyS0 rdinit=/init nokaslr' \
  -nographic -no-reboot \
  -s -S
```

参数含义：

```text
-s  等价于 -gdb tcp::1234
-S  CPU 上电后先暂停，等待 GDB continue
```

另一个终端连接：

```bash
gdb /tmp/linux-2.6.24-build/vmlinux
```

GDB 中：

```gdb
set architecture i386
target remote :1234
hbreak start_kernel
continue
```

内核启动到 shell 后，也可以断系统调用路径：

```gdb
b sys_open
c
```

然后在 tinysh 中执行：

```text
tinysh# cat /proc/version
```

tinysh 还内置了 `int3`，可以主动触发断点：

```text
tinysh# int3
```

## 常见问题

### Docker 没有权限

现象：

```text
permission denied while trying to connect to the Docker daemon socket
```

处理：

```bash
sudo usermod -aG docker "$USER"
newgrp docker
docker ps
```

### Makefile mixed implicit and normal rules

这是 GNU Make 4.x 对旧 Makefile 语法更严格导致的。确认已经应用本文的 `Makefile` 兼容补丁。

### false/bool 变成 C23 关键字

这是误用了现代 GCC，例如 GCC 15。不要直接用宿主工具链编译，使用 Docker 中的 `gcc 4.9.2`。

### initramfs 能加载，但执行不了 /init

确认 `/init` 是 i386 ELF：

```bash
file /tmp/linux-2.6.24-initramfs/init
```

正确结果应包含：

```text
ELF 32-bit LSB executable, Intel 80386, statically linked
```

不要把宿主机的 `/usr/bin/busybox` 直接放进去。它通常是 x86_64，并且最低 Linux ABI 可能高于 2.6.24。

## 本次验证结果

本次实际验证命令：

```bash
qemu-system-i386 \
  -m 256M \
  -kernel /tmp/linux-2.6.24-build/arch/x86/boot/bzImage \
  -initrd /tmp/linux-2.6.24-initramfs.cpio.gz \
  -append 'console=ttyS0 rdinit=/init nokaslr' \
  -nographic -no-reboot
```

启动后执行：

```text
tinysh# uname
Linux version 2.6.24 (@59141547c07c) (gcc version 4.9.2 (Debian 4.9.2-10+deb8u2) ) #1 SMP Mon May 4 17:51:46 UTC 2026

tinysh# pwd
/

tinysh# ls /proc
...

tinysh# poweroff
Power down.
```
