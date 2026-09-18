/*
 * common.h — 全局常量与通用工具
 *
 * 输出约定：正常的机器可读内容走 stdout，进度/告警走 stderr 之外的同一个
 * stdout 也无妨，但错误一律走 stderr。全部纯文本，不带 ANSI 颜色，
 * 方便重定向、diff 和贴进日志。
 */
#ifndef SH_COMMON_H
#define SH_COMMON_H

#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/*
 * 版本号由 Makefile 从仓库根目录的 VERSION 文件注入
 * （-DSH_VERSION=...）；直接用 clang 手工编译时退回下面的默认值。
 */
#ifndef SH_VERSION
#define SH_VERSION "0.0.0-dev"
#endif

/*
 * 退出码。docs/ 里的 shell 脚本和自动化流程会依赖这些值，改动需谨慎。
 */
enum sh_exit_code {
  SH_OK          = 0, /* 成功 */
  SH_ERR         = 1, /* 通用失败 */
  SH_USAGE       = 2, /* 参数错误 */
  SH_NO_EXTERNAL = 3, /* 没有可操作的外接显示器 */
  SH_NO_PRIVATE  = 4, /* SkyLight 私有符号不可用 */
  SH_UNFIXED     = 5, /* 修复流程执行完，但屏幕仍未上线 */
};

/* ---- 输出 ---------------------------------------------------------------- */

void sh_out(const char *fmt, ...);  /* 结果行 */
void sh_note(const char *fmt, ...); /* 过程说明 */
void sh_warn(const char *fmt, ...); /* 可继续的异常 */
void sh_fail(const char *fmt, ...); /* 致命错误，走 stderr */

/* CGError 的符号名；未收录的返回 "CGError(<n>)"。 */
const char *sh_cg_error(CGError err);

/* 打印一次配置事务调用的结果，例如：
 *   independent-output-off = 0 (kCGErrorSuccess)          */
void sh_step(const char *label, CGError err);

/* ---- 子进程 -------------------------------------------------------------- */
/*
 * 直接 fork+execvp，不经过 shell，唯一的目的是调用 /usr/bin/defaults 和
 * killall，避免任何形式的命令拼接。
 */
int sh_run(const char *const argv[]);
/* 捕获子进程 stdout（首行，去掉结尾换行）。返回退出码。 */
int sh_capture(const char *const argv[], char *buf, size_t buflen);

#endif /* SH_COMMON_H */
