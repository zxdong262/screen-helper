/*
 * diag.h — 诊断信息采集
 */
#ifndef SH_DIAG_H
#define SH_DIAG_H

#include <stdbool.h>
#include <stddef.h>

/* 打印系统版本、显示器、DisplayPort 链路状态、菜单栏偏好。
 * with_logs 为真时额外抓最近 30 分钟的 ControlCenter 相关日志。 */
int sh_cmd_diag(bool with_logs);

/* 一行系统摘要，例如 "macOS 26.6.2 build 25G83 arm64" */
void sh_system_summary(char *buf, size_t buflen);

#endif /* SH_DIAG_H */
