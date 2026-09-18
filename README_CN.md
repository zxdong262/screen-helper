# screen-helper

[![build](https://github.com/zxdong262/screen-helper/actions/workflows/build.yml/badge.svg?branch=build)](https://github.com/zxdong262/screen-helper/actions/workflows/build.yml)
[![release](https://img.shields.io/github/v/release/zxdong262/screen-helper?sort=semver)](https://github.com/zxdong262/screen-helper/releases)
[![license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

[English →](README.md)

macOS 外接显示器的扩展 / 镜像 / 分辨率 / 摆放管理，外加一条专门用来救回
「已连接但离线」外接屏的修复路径 —— 也就是在控制中心点一下
**停止扩展（Stop Extending）** 会留下的那种坏状态。

纯 C11 + 系统框架，无第三方依赖，无 Xcode 工程，不依赖 Swift 运行时。
`make` 一下就是一个二进制。

---

## 它解决的是什么问题

控制中心的菜单栏模块里有一个图标是「两个屏幕叠加」的（屏幕镜像模块）。
展开它的下拉菜单，点一次「停止扩展」，外接屏就可能进入这种状态：

```
id=2 builtin=0 vendor=9747 model=1 online=0 active=0
```

DisplayPort 链路本身完全健康 —— `Active = Yes`、`HPD = High`、
`DriverStatus = Ready` —— 所以问题不在线缆、扩展坞或显示器，而是
WindowServer / CoreGraphics 里残留了一个错误的「独立输出」
(independent output) 显示配置。

公开的 `SLSConfigureDisplayEnabled(config, id, true)` 对这个状态完全无效，
真正有效的是 SkyLight 的私有调用：

```c
SLSConfigureDisplayIndependentOutput(config, displayID, false)
```

`screen-helper fix` 就是围绕这个调用做的，外面套了验证和回滚。

另外，没有外接显示器的时候，那个菜单栏图标自己也会消失；
`screen-helper icon always` 负责把它恢复成常显。

## 安装

### Homebrew

```bash
brew install zxdong262/tap/screen-helper
```

formula 放在 [zxdong262/homebrew-tap](https://github.com/zxdong262/homebrew-tap)，
**从源码编译**，所以不存在「下载来的二进制被 Gatekeeper 拦下」这回事。

Homebrew 6.0.0 起第三方 tap 需要显式信任：上面那种带全名的写法只信任这一个
formula，推荐这样。想信任整个 tap：

```bash
brew tap zxdong262/tap
brew trust zxdong262/tap
```

### 压缩包

从 [releases 页面](https://github.com/zxdong262/screen-helper/releases) 拿压缩包，
或者直接取最新版：

```bash
VERSION=1.0.0
curl -LO "https://github.com/zxdong262/screen-helper/releases/download/v$VERSION/screen-helper-v$VERSION-macos-universal.tar.gz"
curl -LO "https://github.com/zxdong262/screen-helper/releases/download/v$VERSION/screen-helper-v$VERSION-macos-universal.tar.gz.sha256"
shasum -a 256 -c "screen-helper-v$VERSION-macos-universal.tar.gz.sha256"

tar -xzf "screen-helper-v$VERSION-macos-universal.tar.gz"
./install.sh                        # 装到 /usr/local/bin（会问 sudo）
PREFIX=~/.local ./install.sh        # 或者装到用户目录，不用 sudo
```

发布包里是通用二进制（`arm64` + `x86_64`），并且做了 ad-hoc 签名。
`install.sh` 会清掉 Gatekeeper 的 quarantine 属性并重新签名 —— 从网上下载的
命令行工具能直接跑起来，靠的就是这一步。

从源码构建：

```bash
make                 # 产出原生 ./screen-helper，本机折腾最快
make smoke           # 构建 + 一组只读自检
sudo make install    # 装到 /usr/local/bin
make dist            # 通用二进制 + 打包 + sha256，都在 dist/
```

要求：装了命令行开发工具的 macOS（要 `clang`），没有别的。
开发与实测环境是 macOS 26.6.2 / Apple M1。

## 用法

```
screen-helper [命令] [选项]        # 不带命令时等于 status
```

查询类：

- `status` —— 一行行汇总当前状态，最后一行的 `state` 会直接告诉你该不该修
- `list` —— 列出所有显示槽位（`--all` 连幽灵槽位一起显示）
- `detail` —— 打印每个屏的详细字段，含 EDID、序列号、原点、像素尺寸
- `diag [--logs]` —— 系统版本 + 显示器 + DisplayPort 链路 + 菜单栏偏好 + 私有 API 可用性

操作类：

- `fix` —— 修复离线的外接屏，并恢复菜单栏图标。这个工具存在的理由就是它
- `extend` —— 切到扩展模式（取消镜像），`--arrange right|left|above|below` 可顺带摆放
- `mirror` —— 把其它在线屏镜像到主屏
- `mode 2560x1440@60` —— 给目标屏绑定分辨率
- `enable <id>` / `disable <id>` —— 开关某个屏；`disable` 需要 `--yes`，
  因为它正是搞坏状态的入口本身
- `save` —— 把当前这套摆法用 `kCGConfigurePermanently` 固化下来
- `icon always|auto|never|status` —— 管理那个「紫色两屏叠加」的菜单栏图标

目标选择（不写就取第一个外接屏）：

- `--display 2` —— 按 CoreGraphics display id 指定
- `--edid 9747:1` —— 按 EDID 指定。**插拔后 id 会变，EDID 不会**，脚本里优先用它

通用选项：

- `--save` —— 默认只作用于当前会话，加上它才落盘
- `--dry-run` —— 只打印将要做什么，不动配置
- `--force` —— 即使屏已经在线也重跑修复流程
- `--no-icon` —— `fix` 时不去碰菜单栏图标
- `--all` —— 显示幽灵槽位
- `-h`、`-V`

## 复现与修复

在控制中心那个下拉菜单点一次「停止扩展」，等外接屏黑掉之后：

```bash
./screen-helper status        # state 一行会显示 external display present but OFFLINE
./screen-helper detail        # 确认 online=0 active=0，但 id / EDID 还在
./screen-helper fix           # 修复，只作用于当前会话
./screen-helper fix --save    # 修好之后想固化下来
./screen-helper extend        # 如果回来后是镜像模式，这条切回扩展
```

`fix` 的流程是三段式，每段之后都重新扫描确认，不假设任何一步一定成功：

1. `SLSConfigureDisplayIndependentOutput(cfg, id, false)`
2. 还没上线，就在同一个事务里补一次显式 `SLSConfigureDisplayEnabled`
3. 已经在线但没激活，就绑一个显示模式上去

退出码：`0` 成功 / `1` 通用错误 / `2` 参数错误 / `3` 没找到外接屏 /
`4` 私有 API 不可用 / `5` 流程跑完但屏仍未上线。

## 关于「闭着眼睛写」这件事

那个私有标记**没有 getter**。SkyLight 里不存在
`SLSGetDisplayIndependentOutput` 之类的符号（用 `dlsym` 一个个探过），
`SLSCopyDisplayInfoDictionary` 里也不暴露这个字段。所以**动手之前读不出它的
真实值**，也就无法先验确认「关掉它」到底是把屏拉回显示组、还是把屏踢出去。

所以每次提交之后程序都会重新扫描全部屏：只要有任何一个「原本在线」的屏因为
这次调用掉线了，就判定方向反了，立刻反着写回去然后中止。宁可什么都不改，
也不把好屏弄坏。这一步的输出是
`guard: no previously working display was affected`。

## 注意事项

- SkyLight 是私有框架，API 没有任何兼容性保证。系统升级后跑一下
  `screen-helper diag`，看 `Private API` 那一段，它会直接告诉你符号还在不在。
- 无效 display id 的 `CGDisplayVendorNumber` 返回 `0xFFFFFFFF`（不是 0），
  判断槽位是否有效必须同时排除这两个值。
- macOS 会保留一个非 builtin 的「幽灵」槽位，EDID 与内建屏完全相同
  （本机实测 `id=3`、`1552:41033`）。无脑取「第一个非 builtin」会打到它身上。
  程序把它分类为 `kind=ghost`，默认不显示。
- `disable` 是搞坏状态的入口，所以默认要求 `--yes`。
- 万一修复失败，最后的兜底手段是重插链路或重启 WindowServer（会结束图形会话），
  程序在放弃时会把这句话打出来。

## 文件说明

按阅读顺序，每个文件都在这。

根目录：

- [`Makefile`](Makefile) —— 构建、自检、通用二进制、打包、安装这几组目标
- [`VERSION`](VERSION) —— 版本号唯一来源；构建时注入为 `SH_VERSION`，CI 用它打 tag
- [`README.md`](README.md) —— 英文说明
- [`README_CN.md`](README_CN.md) —— 本文件
- [`LICENSE`](LICENSE) —— MIT
- [`.gitignore`](.gitignore) —— 构建产物、`dist/`、本地临时目录

源码：

- [`src/main.c`](src/main.c) —— 参数解析与分发，所有子命令在这里一眼看全
- [`src/commands.c`](src/commands.c) —— 最重的一块：`apply_steps`、
  `recover_display`（三段式修复）、`apply_steps_guarded` + `snapshot_regressions`
  （回归守护），以及各个命令的实现
- [`src/commands.h`](src/commands.h) —— 选项结构与命令原型
- [`src/display.c`](src/display.c) —— 扫 1..63 号槽位、builtin/external/ghost 分类、
  按 EDID 定位目标、配置事务、模式选择
- [`src/display.h`](src/display.h) —— 显示描述结构与事务封装
- [`src/skylight.c`](src/skylight.c) —— 就两个函数，`dlopen` 一次 + `dlsym` 缓存；
  符号缺失时返回错误而不是崩溃
- [`src/skylight.h`](src/skylight.h) —— 私有 API 原型
- [`src/menubar.c`](src/menubar.c) —— 管 `ScreenMirroring` / `Display` 两个菜单栏
  偏好键；`ensure_visible` 在值已经正确时不会去重启 ControlCenter
- [`src/menubar.h`](src/menubar.h) —— 菜单栏 API
- [`src/diag.c`](src/diag.c) —— 诊断采集，DisplayPort 链路直接从 IOKit 读
  （不 shell 调 `ioreg`）
- [`src/diag.h`](src/diag.h) —— 诊断 API
- [`src/common.c`](src/common.c) —— 输出封装，以及 `sh_run` / `sh_capture`，
  自己 fork/execvp，不经过 shell
- [`src/common.h`](src/common.h) —— 退出码、`SH_VERSION` 兜底值、通用工具

自动化：

- [`scripts/release`](scripts/release) —— 发布驱动，把提交推到 CI 监听的 `build` 分支
- [`scripts/install.sh`](scripts/install.sh) —— 随发布包分发；清 quarantine、
  ad-hoc 签名、安装
- [`.github/workflows/build.yml`](.github/workflows/build.yml) —— 构建、自检、打 tag、发 release

## 开发与发布

版本号只存在 [`VERSION`](VERSION) 里。Makefile 把它注入成 `-DSH_VERSION=...`，
CI 读它来打 tag，别处不再存第二份版本号。

发布流程 —— CI 只监听 `build` 分支，别处一概不触发，所以发版永远是显式动作：

```bash
bash scripts/release --dry-run        # 先看它会做什么
bash scripts/release                  # 用当前 VERSION 重新发一次
bash scripts/release --bump patch     # 1.0.0 -> 1.0.1 后发布
bash scripts/release --version 2.0.0  # 直接跳到指定版本
```

`scripts/release` 干的事：preflight（工作区干净、`VERSION` 合法、不在 `build` 上）
→ 同步 origin → 需要时升版本并提交 `release vX.Y.Z` → 本地 `make dist` 证明还编得出来
→ push 源分支 → force push `HEAD` 到 `build`。

接着 CI 用 `-Werror` 编译通用二进制、跑自检、发现 tag 已存在就拒绝发布，
最后创建 GitHub Release 并附上 `.tar.gz` 和 `.sha256`。
往 `main` 上正常提交不会触发任何构建。

Homebrew 的 formula 放在另一个 tap 仓库
[zxdong262/homebrew-tap](https://github.com/zxdong262/homebrew-tap)，
它在 release 出来之后单独 bump —— formula 指向的是 tag 归档，所以 tag 必须先存在：

```bash
git clone https://github.com/zxdong262/homebrew-tap
cd homebrew-tap
./scripts/bump screen-helper <版本号>
```

## 许可证

[MIT](LICENSE)。
