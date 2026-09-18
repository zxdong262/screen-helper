/*
 * skylight.h — macOS 私有 SkyLight API 的运行时封装
 *
 * 这些符号位于 /System/Library/PrivateFrameworks/SkyLight.framework，
 * 没有公开头文件，也没有任何兼容性承诺。所以这里一律用 dlopen/dlsym
 * 在运行时解析：符号被 Apple 移走或改名时，程序给出明确的“不可用”，
 * 而不是在链接期或运行期直接崩掉。
 */
#ifndef SH_SKYLIGHT_H
#define SH_SKYLIGHT_H

#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>

/* 私有框架能否加载 */
bool sl_available(void);

/* 各个符号是否存在 */
bool sl_has_independent_output(void);
bool sl_has_enabled(void);

/*
 * 本问题的真正解药。
 *
 * “已连接但离线”的根因是 WindowServer 里残留了一个“独立输出”
 * (independent output) 标记 —— 屏幕被标成不属于任何显示组，于是
 * 普通的启用调用完全无效。把该标记关掉，屏才回到可用的显示组里。
 */
CGError sl_set_independent_output(CGDisplayConfigRef cfg, CGDirectDisplayID id, bool enabled);

/*
 * 普通的启用/关闭。单独调用无法修复被卡住的独立输出状态，
 * 保留它主要是为了诊断和组合尝试。
 */
CGError sl_set_enabled(CGDisplayConfigRef cfg, CGDirectDisplayID id, bool enabled);

#endif /* SH_SKYLIGHT_H */
