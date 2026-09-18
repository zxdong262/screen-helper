/*
 * screen-helper — macOS 外接显示器扩展 / 显示状态管理
 *
 * 一个纯粹的 CLI：枚举显示槽位，做常规的扩展/镜像/分辨率/摆放，
 * 并且能修复「已连接但离线」这种极端情况（WindowServer 残留独立输出状态）。
 */
#include "commands.h"
#include "common.h"
#include "diag.h"
#include "display.h"
#include "menubar.h"
#include "skylight.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f) {
  fprintf(f,
    "screen-helper %s — macOS 外接显示器扩展 / 显示状态管理\n"
    "\n"
    "用法:\n"
    "  screen-helper [命令] [选项]\n"
    "\n"
    "常用命令:\n"
    "  status                       汇总显示状态（不带命令时的默认行为）\n"
    "  fix                          修复「已连接但离线」的外接屏，并恢复菜单栏图标\n"
    "  extend                       切到扩展模式（取消镜像），可顺带摆放位置\n"
    "  mirror                       把其它在线屏镜像到主屏\n"
    "  save                         把当前这套摆法永久保存\n"
    "\n"
    "查询与微调:\n"
    "  list                         列出所有显示槽位\n"
    "  detail                       打印每个屏的详细字段\n"
    "  enable <id>                  打开某个屏\n"
    "  disable <id>                 关闭某个屏（需要 --yes；这正是搞坏状态的入口）\n"
    "  mode <WxH[@Hz]>              给目标屏绑定分辨率，例如 2560x1440@60\n"
    "  icon <模式>                  管理菜单栏那个「紫色两屏叠加」图标\n"
    "  diag [--logs]                采集诊断信息\n"
    "  version                      版本号\n"
    "\n"
    "目标选择（可选，默认取第一个外接屏，插拔后按 EDID 定位更稳）:\n"
    "  --display <id>               CoreGraphics display id\n"
    "  --edid <vendor:model>        按 EDID 指定，例如 9747:1\n"
    "\n"
    "通用选项:\n"
    "  --save                       用 kCGConfigurePermanently 落盘（默认只作用于当前会话）\n"
    "  --arrange <位置>             extend 时摆放：right | left | above | below\n"
    "  --force                      即使屏已经在线也重跑修复流程\n"
    "  --dry-run                    只打印将要做什么，不动配置\n"
    "  --yes                        跳过危险操作确认\n"
    "  --all                        连同「幽灵」显示槽位一起显示\n"
    "  --no-icon                    fix 时不去碰菜单栏图标\n"
    "  -h, --help                   显示本帮助\n"
    "  -V, --version                版本号\n"
    "\n"
    "icon 的模式:\n"
    "  always | auto | never | status\n"
    "  --module all | screen-mirroring | display     默认 all\n"
    "  --no-restart                 不重启 ControlCenter\n"
    "\n"
    "退出码: 0 成功 / 1 通用错误 / 2 参数错误 / 3 没有外接屏 / 4 私有 API 不可用\n"
    "        5 修复流程跑完但屏幕仍未上线\n"
    "\n"
    "示例:\n"
    "  screen-helper fix --save                 修好外接屏并永久保存\n"
    "  screen-helper extend --arrange right      扩展，并把外接屏摆到右侧\n"
    "  screen-helper mode 2560x1440@60 --save    指定分辨率并保存\n"
    "  screen-helper icon always                 让菜单栏图标始终显示\n",
    SH_VERSION);
}

static int parse_edid(const char *spec, uint32_t *vendor, uint32_t *model) {
  unsigned v = 0, m = 0;
  if (sscanf(spec, "%u:%u", &v, &m) != 2) return -1;
  *vendor = v;
  *model = m;
  return 0;
}

static int parse_arrange(const char *text, sh_arrange_t *out) {
  if (!strcmp(text, "right")) { *out = SH_ARRANGE_RIGHT; return 0; }
  if (!strcmp(text, "left"))  { *out = SH_ARRANGE_LEFT;  return 0; }
  if (!strcmp(text, "above")) { *out = SH_ARRANGE_ABOVE; return 0; }
  if (!strcmp(text, "below")) { *out = SH_ARRANGE_BELOW; return 0; }
  return -1;
}

int main(int argc, char **argv) {
  sh_options_t opt;
  memset(&opt, 0, sizeof opt);
  opt.display = kCGNullDirectDisplay;
  opt.arrange = SH_ARRANGE_NONE;

  const char *command = NULL;
  const char *arg1 = NULL;      /* mode 的规格 / icon 的模式 */
  sh_icon_mode_t icon_mode = (sh_icon_mode_t)-1;
  sh_module_t icon_module = SH_MODULE_ALL;
  bool icon_restart = true;
  bool diag_logs = false;
  bool show_help = false;
  bool show_version = false;

  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];

    if (!strcmp(a, "-h") || !strcmp(a, "--help")) { show_help = true; continue; }
    if (!strcmp(a, "-V") || !strcmp(a, "--version")) { show_version = true; continue; }
    if (!strcmp(a, "--save")) { opt.save = true; continue; }
    if (!strcmp(a, "--force")) { opt.force = true; continue; }
    if (!strcmp(a, "--dry-run")) { opt.dry_run = true; continue; }
    if (!strcmp(a, "--yes")) { opt.yes = true; continue; }
    if (!strcmp(a, "--all")) { opt.all = true; continue; }
    if (!strcmp(a, "--no-icon")) { opt.skip_icon = true; continue; }
    if (!strcmp(a, "--no-restart")) { icon_restart = false; continue; }
    if (!strcmp(a, "--logs")) { diag_logs = true; continue; }

    if (!strcmp(a, "--display") && i + 1 < argc) {
      opt.display = (CGDirectDisplayID)strtoul(argv[++i], NULL, 10);
      opt.have_display = true;
      continue;
    }
    if (!strcmp(a, "--edid") && i + 1 < argc) {
      if (parse_edid(argv[++i], &opt.vendor, &opt.model) != 0) {
        sh_fail("--edid expects vendor:model, e.g. 9747:1");
        return SH_USAGE;
      }
      opt.have_vendor = true;
      continue;
    }
    if (!strcmp(a, "--arrange") && i + 1 < argc) {
      if (parse_arrange(argv[++i], &opt.arrange) != 0) {
        sh_fail("--arrange expects right | left | above | below");
        return SH_USAGE;
      }
      continue;
    }
    if (!strcmp(a, "--module") && i + 1 < argc) {
      const char *m = argv[++i];
      if (!strcmp(m, "all")) icon_module = SH_MODULE_ALL;
      else if (!strcmp(m, "screen-mirroring")) icon_module = SH_MODULE_SCREEN_MIRRORING;
      else if (!strcmp(m, "display")) icon_module = SH_MODULE_DISPLAY;
      else { sh_fail("--module expects all | screen-mirroring | display"); return SH_USAGE; }
      continue;
    }

    if (a[0] == '-' && a[1] != '\0') {
      sh_fail("unknown option: %s", a);
      usage(stderr);
      return SH_USAGE;
    }

    if (!command) { command = a; continue; }
    if (!arg1) { arg1 = a; continue; }
    sh_fail("unexpected extra argument: %s", a);
    return SH_USAGE;
  }

  if (show_help) { usage(stdout); return SH_OK; }
  if (show_version) { printf("screen-helper %s\n", SH_VERSION); return SH_OK; }

  if (!command) command = "status";

  if (!strcmp(command, "status"))  return sh_cmd_status(&opt);
  if (!strcmp(command, "list"))    return sh_cmd_list(&opt);
  if (!strcmp(command, "detail"))  return sh_cmd_detail(&opt);
  if (!strcmp(command, "fix"))     return sh_cmd_fix(&opt);
  if (!strcmp(command, "extend"))  return sh_cmd_extend(&opt);
  if (!strcmp(command, "mirror"))  return sh_cmd_mirror(&opt);
  if (!strcmp(command, "save"))    return sh_cmd_save(&opt);
  if (!strcmp(command, "help"))    { usage(stdout); return SH_OK; }
  if (!strcmp(command, "version")) { printf("screen-helper %s\n", SH_VERSION); return SH_OK; }
  if (!strcmp(command, "diag"))    return sh_cmd_diag(diag_logs);

  if (!strcmp(command, "enable"))  return sh_cmd_set_enabled(&opt, true);
  if (!strcmp(command, "disable")) return sh_cmd_set_enabled(&opt, false);

  if (!strcmp(command, "mode")) {
    if (!arg1) {
      sh_fail("mode needs a spec, e.g. 2560x1440@60");
      return SH_USAGE;
    }
    return sh_cmd_mode(&opt, arg1);
  }

  if (!strcmp(command, "icon")) {
    if (!arg1) {
      sh_fail("icon needs a mode: always | auto | never | status");
      return SH_USAGE;
    }
    if (!strcmp(arg1, "status"))       icon_mode = (sh_icon_mode_t)-1;
    else if (!strcmp(arg1, "always"))  icon_mode = SH_ICON_ALWAYS;
    else if (!strcmp(arg1, "auto"))    icon_mode = SH_ICON_ACTIVE;
    else if (!strcmp(arg1, "never"))   icon_mode = SH_ICON_NEVER;
    else {
      sh_fail("icon mode must be always | auto | never | status");
      return SH_USAGE;
    }
    return sh_cmd_icon(icon_mode, icon_module, icon_restart);
  }

  sh_fail("unknown command: %s", command);
  usage(stderr);
  return SH_USAGE;
}
