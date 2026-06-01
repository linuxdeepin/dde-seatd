# dde-seatd、seatd 与 libseat

## DDE 分支说明

`dde-seatd` 是面向 DDM + Treeland 的 DDE 定制 `seatd` 守护进程分支。它
保持与上游 seatd/libseat 协议兼容，但安装出的守护进程名称为
`dde-seatd`，默认 socket 路径为 `/run/dde-seatd.sock`，因此可以与使用
`/run/seatd.sock` 的标准 `seatd.service` 共存。

这个分支尽量保持上游安装面不变，同时对会冲突的产物做了重命名，以便和
标准 `seatd` 共存：辅助程序安装为 `dde-seatd-launch`，库与 pkg-config
名字改为 DDE 专用名称，公开头文件安装到 `include/dde-seatd/`。
Treeland 或其他需要连接此守护进程的 wlroots compositor 仍然使用常规的
libseat seatd backend，只需把它指向 DDE socket：

```bash
LIBSEAT_BACKEND=seatd SEATD_SOCK=/run/dde-seatd.sock treeland
```

针对 DDM 的行为扩展通过一个独立的 DDE control socket 提供：

- `-s <path>` 选择兼容 libseat 的 socket。
- `-c <path>` 启用 DDE control socket。
- DDM 可以通过 control socket 传入 owner 进程 pid，为现有 libseat client
  注册额外的 grouped VT。
- 打包安装的 `dde-seatd.service` 会默认使用
  `-s /run/dde-seatd.sock -c /run/dde-seatd-control.sock` 启动，确保 DDM
  总能按约定路径连接这两个 socket。

这样在同一组内的用户 VT 切换时，compositor 可以继续保持 DRM / input
所有权；而切换到组外 VT 时，`dde-seatd` 仍按常规 VT-bound 流程执行
disable / enable。

一个最小化的 seat 管理守护进程，以及一个通用的 seat 管理库。

目前支持 Linux 和 FreeBSD，并对 NetBSD 提供实验性支持。

## 什么是 seat 管理？

seat 管理负责协调共享设备（图形、输入）的访问，而不需要实际使用这些
设备的应用程序以 root 身份运行。

## 包含哪些组件？

### seatd

一个专注于完成 seat 管理工作的守护进程，不多做也不少做。仅依赖 libc。

### libseat

一个 seat 管理库，用于让应用程序使用当前系统上可用的 seat 管理方案。

支持：

- seatd
- (e)logind
- 独立运行时的嵌入式 seatd

每个 backend 都可以在编译时选择是否包含，并在运行时自动探测，或通过
`LIBSEAT_BACKEND` 环境变量手动指定。

具体使用哪个 backend 对应用程序是透明的，它们看到的是一个统一的简单接
口。

## 为什么不用 (e)logind？

systemd-logind 不可移植，而且它属于 systemd 项目的一部分，因此无法在不
基于 systemd、或与 systemd 不兼容的环境中使用。

elogind 通过硬拆方式把 systemd-logind 从 systemd 中剥离出来。这需要不断
对抗上游为了深度集成做出的设计决策，而且每次同步上游都要重复这套工作。
即便如此，最终得到的也仍然只是一个打补丁的方案。

既然如此，为什么不把精力用在做一个工作量更小、实现更好的替代品上？

## 为什么 libseat 仍然支持 (e)logind？

[为了不让问题变得更糟](https://xkcd.com/927/)。短期内我们并不会取代
systemd-logind，因此对于像 [sway](https://github.com/swaywm/sway) 这样的
用户空间 shell，seatd 与 logind、直接 session 管理一样，都是需要支持的
选项。

libseat 的目标不是给用户空间 shell 开发者增加工作量，而是让支持 seatd
比他们当前自己维护的方案更省事。它通过统一处理多种 backend 下的 seat
管理需求，不仅提供 seatd 支持，也替代现有的 logind 和直接 seat 管理实
现。

## 如何讨论

可以前往 [#kennylevinsen @ irc.libera.chat](ircs://irc.libera.chat/#kennylevinsen)
讨论，或使用 [~kennylevinsen/seatd-devel@lists.sr.ht](https://lists.sr.ht/~kennylevinsen/).
