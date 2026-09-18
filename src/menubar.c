#include "menubar.h"

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CC_DOMAIN "com.apple.controlcenter"
#define CC_KEY_SCREEN_MIRRORING "ScreenMirroring"
#define CC_KEY_DISPLAY "Display"

static const char *module_key(sh_module_t module) {
  return module == SH_MODULE_DISPLAY ? CC_KEY_DISPLAY : CC_KEY_SCREEN_MIRRORING;
}

static const char *module_name(sh_module_t module) {
  return module == SH_MODULE_DISPLAY ? "Display" : "ScreenMirroring";
}

int sh_icon_read(sh_module_t module, int *value) {
  if (value) *value = -1;
  const char *argv[] = {
    "defaults", "-currentHost", "read", CC_DOMAIN, module_key(module), NULL
  };
  char buf[64] = "";
  int rc = sh_capture(argv, buf, sizeof buf);
  if (rc != 0 || buf[0] == '\0') return rc;
  if (value) *value = atoi(buf);
  return rc;
}

const char *sh_icon_value_name(int value) {
  switch (value) {
    case -1: return "unset";
    case SH_ICON_NEVER:  return "0 (never)";
    case SH_ICON_ACTIVE: return "1 (when active)";
    case SH_ICON_ALWAYS: return "2 (always)";
    default: break;
  }
  static char buf[32];
  snprintf(buf, sizeof buf, "%d (unknown)", value);
  return buf;
}

int sh_icon_write(sh_module_t module, int value, bool restart) {
  char number[16];
  snprintf(number, sizeof number, "%d", value);
  const char *argv[] = {
    "defaults", "-currentHost", "write", CC_DOMAIN, module_key(module), "-int", number, NULL
  };
  int rc = sh_run(argv);
  if (rc != 0) {
    sh_fail("defaults write %s %s %d failed (exit %d)",
            CC_DOMAIN, module_key(module), value, rc);
    return rc;
  }
  sh_note("%-24s = %s", module_name(module), sh_icon_value_name(value));

  if (restart) {
    const char *kill[] = { "killall", "ControlCenter", NULL };
    int krc = sh_run(kill);
    if (krc != 0) sh_warn("killall ControlCenter returned %d (may not have been running)", krc);
    else sh_note("ControlCenter restarted to pick up the change");
  }
  return 0;
}

bool sh_menu_bar_ensure_visible(bool restart) {
  int current = -1;
  sh_icon_read(SH_MODULE_SCREEN_MIRRORING, &current);
  if (current == SH_ICON_ALWAYS) return false;
  sh_out("menu bar icon was %s, restoring to %s",
         sh_icon_value_name(current), sh_icon_value_name(SH_ICON_ALWAYS));
  sh_icon_write(SH_MODULE_SCREEN_MIRRORING, SH_ICON_ALWAYS, restart);
  return true;
}

int sh_cmd_icon(sh_icon_mode_t mode, sh_module_t module, bool restart) {
  sh_module_t modules[2];
  size_t count = 0;
  if (module == SH_MODULE_ALL) {
    modules[count++] = SH_MODULE_SCREEN_MIRRORING;
    modules[count++] = SH_MODULE_DISPLAY;
  } else {
    modules[count++] = module;
  }

  for (size_t i = 0; i < count; i++) {
    int current = -1;
    sh_icon_read(modules[i], &current);
    if (mode == (sh_icon_mode_t)-1) {
      sh_out("%-16s = %s", module_name(modules[i]), sh_icon_value_name(current));
      continue;
    }
    if (current == (int)mode) {
      sh_out("%-16s = %s (already)", module_name(modules[i]), sh_icon_value_name(current));
      continue;
    }
    sh_out("%-16s : %s -> %s", module_name(modules[i]),
           sh_icon_value_name(current), sh_icon_value_name((int)mode));
    sh_icon_write(modules[i], (int)mode, restart);
  }
  return SH_OK;
}
