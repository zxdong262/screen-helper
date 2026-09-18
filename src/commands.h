/*
 * commands.h — 子命令入口
 */
#ifndef SH_COMMANDS_H
#define SH_COMMANDS_H

#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>

typedef enum {
  SH_ARRANGE_NONE = 0,
  SH_ARRANGE_RIGHT,
  SH_ARRANGE_LEFT,
  SH_ARRANGE_ABOVE,
  SH_ARRANGE_BELOW,
} sh_arrange_t;

typedef struct {
  CGDirectDisplayID display; /* kCGNullDirectDisplay = 未指定 */
  uint32_t vendor;
  uint32_t model;
  bool have_vendor;
  bool have_display;

  bool save;    /* 用 kCGConfigurePermanently 落盘 */
  bool force;   /* 即使屏幕已在线也重跑一遍 */
  bool dry_run; /* 只打印将要做什么 */
  bool yes;       /* 跳过危险操作的确认 */
  bool all;       /* 显示幽灵条目 */
  bool skip_icon; /* fix 时不去动菜单栏图标 */
  sh_arrange_t arrange;
} sh_options_t;

int sh_cmd_list(const sh_options_t *o);
int sh_cmd_status(const sh_options_t *o);
int sh_cmd_detail(const sh_options_t *o);
int sh_cmd_fix(const sh_options_t *o);
int sh_cmd_extend(const sh_options_t *o);
int sh_cmd_mirror(const sh_options_t *o);
int sh_cmd_set_enabled(const sh_options_t *o, bool enabled);
int sh_cmd_mode(const sh_options_t *o, const char *spec);
int sh_cmd_save(const sh_options_t *o);

#endif /* SH_COMMANDS_H */
