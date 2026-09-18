/*
 * menubar.h — ControlCenter 菜单栏模块偏好
 */
#ifndef SH_MENUBAR_H
#define SH_MENUBAR_H

#include <stdbool.h>

/*
 * com.apple.controlcenter 的菜单栏开关取值。
 * 这里没有公开文档，取值来自实测：
 *   0 = 从不显示，1 = 有活动时显示，2 = 始终显示
 */
typedef enum {
  SH_ICON_NEVER  = 0,
  SH_ICON_ACTIVE = 1,
  SH_ICON_ALWAYS = 2,
} sh_icon_mode_t;

/*
 * SH_MODULE_SCREEN_MIRRORING 对应「屏幕镜像」模块，也就是菜单栏里那个
 * 紫色两个屏幕叠加的图标（AirPlay / 投屏入口）。用户把它的下拉菜单里
 * 的「停止扩展」点掉之后，外接屏就回不来了，图标本身也会在没接外接屏
 * 时消失 —— 就是本工具要解决的那个极端情况。
 *
 * SH_MODULE_DISPLAY 是普通的「显示器」模块，一并管理以便排查。
 */
typedef enum {
  SH_MODULE_ALL = 0,
  SH_MODULE_SCREEN_MIRRORING,
  SH_MODULE_DISPLAY,
} sh_module_t;

/* 读取偏好值，*value = -1 表示该键不存在。返回子进程退出码。 */
int sh_icon_read(sh_module_t module, int *value);
const char *sh_icon_value_name(int value);

int sh_icon_write(sh_module_t module, int value, bool restart);

/* 把屏幕镜像图标恢复成常显。已经正确时不重启 ControlCenter，返回 false。 */
bool sh_menu_bar_ensure_visible(bool restart);

int sh_cmd_icon(sh_icon_mode_t mode, sh_module_t module, bool restart);

#endif /* SH_MENUBAR_H */
