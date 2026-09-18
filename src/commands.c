#include "commands.h"

#include "common.h"
#include "diag.h"
#include "display.h"
#include "menubar.h"
#include "skylight.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* 提交配置后给 WindowServer 一点时间把屏点亮，再回读真实状态 */
#define RESCAN_DELAY_US 700000

/* -------------------------------------------------------------------------- */
/* 小工具                                                                      */
/* -------------------------------------------------------------------------- */

static void describe(const sh_display_t *d, char *buf, size_t buflen) {
  char edid[32];
  sh_display_edid_name(d, edid, sizeof edid);
  snprintf(buf, buflen, "id=%u %s edid=%s online=%s active=%s",
           d->id, sh_display_kind_name(d->kind), edid,
           d->online ? "yes" : "no", d->active ? "yes" : "no");
}

/*
 * 重新扫描并按 EDID 找回目标。修复提交后 display id 有可能变化，
 * 所以不能继续用旧 id，必须按 EDID 重新定位。
 */
static bool rescan(const sh_display_t *ref, sh_display_t *out) {
  sh_display_t list[SH_MAX_DISPLAYS];
  size_t n = sh_display_scan(list, SH_MAX_DISPLAYS);
  int idx = sh_display_find_by_edid(list, n, ref->vendor, ref->model);
  if (idx < 0) idx = sh_display_find_by_id(list, n, ref->id);
  if (idx < 0) return false;
  if (out) *out = list[idx];
  return true;
}

/* 在命令行指定目标时用它；否则返回所有外接屏 */
static size_t collect_targets(const sh_options_t *o, const sh_display_t *list, size_t n,
                              int *out, size_t cap, const char *verb) {
  if (o->have_display || o->have_vendor) {
    char err[256];
    int idx = -1;
    int rc = sh_display_resolve(list, n, o->display, o->vendor, o->model,
                                o->have_vendor, &idx, err, sizeof err);
    if (rc != SH_OK) {
      sh_fail("%s", err);
      return 0;
    }
    out[0] = idx;
    return 1;
  }
  size_t count = sh_display_list_externals(list, n, out, cap);
  if (count == 0) sh_fail("no external display to %s", verb);
  return count;
}

/* -------------------------------------------------------------------------- */
/* 配置事务的原子步骤                                                          */
/* -------------------------------------------------------------------------- */

typedef enum {
  STEP_ENABLE,
  STEP_INDEPENDENT_OFF,
  STEP_INDEPENDENT_ON,
  STEP_MIRROR_OFF,
} step_t;

static const char *step_label(step_t s) {
  switch (s) {
    case STEP_ENABLE:          return "display-enabled";
    case STEP_INDEPENDENT_OFF: return "independent-output-off";
    case STEP_INDEPENDENT_ON:  return "independent-output-on";
    case STEP_MIRROR_OFF:      return "mirror-off";
  }
  return "?";
}

/*
 * 把若干调用放进同一个 CG 配置事务里，任一步失败就整体取消。
 * 顺序有意义：enable 必须在 independent-output 之前，因为后者要重建
 * 显示组归属。
 */
static CGError apply_steps(CGDirectDisplayID id, const step_t *steps, size_t count, bool persist) {
  CGDisplayConfigRef cfg = NULL;
  CGError e = sh_config_begin(&cfg);
  sh_step("begin", e);
  if (e != kCGErrorSuccess) return e;

  for (size_t i = 0; i < count; i++) {
    switch (steps[i]) {
      case STEP_ENABLE:
        if (!sl_has_enabled()) {
          sh_note("%-24s = skipped (symbol missing)", step_label(steps[i]));
          continue;
        }
        e = sl_set_enabled(cfg, id, true);
        break;
      case STEP_INDEPENDENT_OFF:
        e = sl_set_independent_output(cfg, id, false);
        break;
      case STEP_INDEPENDENT_ON:
        e = sl_set_independent_output(cfg, id, true);
        break;
      case STEP_MIRROR_OFF:
        e = CGConfigureDisplayMirrorOfDisplay(cfg, id, kCGNullDirectDisplay);
        break;
    }
    sh_step(step_label(steps[i]), e);
    if (e != kCGErrorSuccess) {
      sh_config_cancel(cfg);
      return e;
    }
  }

  e = sh_config_commit(cfg, persist);
  sh_step(persist ? "complete(permanent)" : "complete(session)", e);
  return e;
}

/* -------------------------------------------------------------------------- */
/* 回归保护                                                                    */
/* -------------------------------------------------------------------------- */

/*
 * SkyLight 没有给 IndependentOutput 提供 getter（实测无
 * SLSGetDisplayIndependentOutput 之类符号），所以动手之前读不出这个标记
 * 的真实值，也就无法先验地确认「关掉它」到底是把它拉回显示组、还是把它
 * 踢出显示组。
 *
 * 因此每次提交之后都重新扫描全部屏：只要有任何一个「原本在线」的屏因为
 * 这次调用掉线了，就说明方向反了，立刻反着写回去。宁可什么都不改，
 * 也不能把好屏弄坏。
 */

typedef struct {
  size_t n;
  struct {
    CGDirectDisplayID id;
    uint32_t vendor, model;
    bool online;
  } entries[SH_MAX_DISPLAYS];
} sh_snapshot_t;

static void snapshot_take(sh_snapshot_t *s) {
  sh_display_t list[SH_MAX_DISPLAYS];
  size_t n = sh_display_scan(list, SH_MAX_DISPLAYS);
  s->n = 0;
  for (size_t i = 0; i < n && s->n < SH_MAX_DISPLAYS; i++) {
    s->entries[s->n].id = list[i].id;
    s->entries[s->n].vendor = list[i].vendor;
    s->entries[s->n].model = list[i].model;
    s->entries[s->n].online = list[i].online;
    s->n++;
  }
}

/* 返回「快照时在线、现在掉线」的屏数量；把第一个的描述写进 buf。 */
static size_t snapshot_regressions(const sh_snapshot_t *s, char *buf, size_t buflen) {
  sh_display_t list[SH_MAX_DISPLAYS];
  size_t n = sh_display_scan(list, SH_MAX_DISPLAYS);

  size_t bad = 0;
  bool described = false;
  for (size_t i = 0; i < s->n; i++) {
    if (!s->entries[i].online) continue;

    int idx = sh_display_find_by_id(list, n, s->entries[i].id);
    if (idx < 0) idx = sh_display_find_by_edid(list, n, s->entries[i].vendor, s->entries[i].model);

    if (idx < 0) {
      if (!described && buf) {
        snprintf(buf, buflen, "display %u vanished from the display server", s->entries[i].id);
        described = true;
      }
      bad++;
      continue;
    }
    if (!list[idx].online) {
      if (!described && buf) {
        describe(&list[idx], buf, buflen);
        described = true;
      }
      bad++;
    }
  }
  return bad;
}

static bool step_reversible(step_t s) {
  return s == STEP_INDEPENDENT_OFF || s == STEP_INDEPENDENT_ON;
}

static step_t step_inverse(step_t s) {
  return s == STEP_INDEPENDENT_OFF ? STEP_INDEPENDENT_ON :
         s == STEP_INDEPENDENT_ON ? STEP_INDEPENDENT_OFF : s;
}

/*
 * 提交一组步骤并检查副作用。
 * 返回 SH_OK / SH_ERR；*rolled_back 为真表示方向反了、已经回滚。
 */
static int apply_steps_guarded(CGDirectDisplayID id, const step_t *steps, size_t count,
                               bool persist, bool *rolled_back) {
  if (rolled_back) *rolled_back = false;

  sh_snapshot_t before;
  snapshot_take(&before);

  CGError e = apply_steps(id, steps, count, persist);
  if (e != kCGErrorSuccess) return SH_ERR;

  usleep(RESCAN_DELAY_US);

  char what[192] = "";
  if (snapshot_regressions(&before, what, sizeof what) == 0) {
    sh_note("guard: no previously working display was affected");
    return SH_OK;
  }

  sh_warn("this attempt knocked a working display offline: %s", what);
  sh_note("the private flag evidently goes the other way on this macOS build; rolling back");

  step_t undo[8];
  size_t un = 0;
  for (size_t i = 0; i < count && un < sizeof undo / sizeof undo[0]; i++)
    if (step_reversible(steps[i])) undo[un++] = step_inverse(steps[i]);

  if (un) {
    apply_steps(id, undo, un, persist);
    usleep(RESCAN_DELAY_US);
  } else {
    sh_warn("this step has no inverse, cannot roll back automatically");
  }
  if (rolled_back) *rolled_back = true;
  return SH_ERR;
}

/* -------------------------------------------------------------------------- */
/* 核心修复流程                                                                */
/* -------------------------------------------------------------------------- */

/*
 * 这是整个工具的立足点。docs/README.md 里记录的结论：
 *
 *   普通 SLSConfigureDisplayEnabled(..., true) 对「已连接但离线」的屏无效，
 *   真正能救回来的是 SLSConfigureDisplayIndependentOutput(..., false)
 *   —— 清掉 WindowServer 里残留的独立输出标记。
 *
 * 这里把它做成一个有回退的三段式流程，每段之后都重新扫描确认，
 * 不假设任何一步一定成功。
 */
static int recover_display(const sh_options_t *o, const sh_display_t *target, sh_display_t *after_out) {
  sh_display_t cur = *target;
  char text[192];

  describe(&cur, text, sizeof text);
  sh_out("target: %s", text);

  if (cur.online && cur.active && !o->force) {
    sh_note("already online and active, nothing to repair (use --force to re-apply)");
    if (after_out) *after_out = cur;
    return SH_OK;
  }

  if (!sl_available()) {
    sh_fail("cannot dlopen SkyLight private framework at "
            "/System/Library/PrivateFrameworks/SkyLight.framework");
    return SH_NO_PRIVATE;
  }
  if (!sl_has_independent_output()) {
    sh_fail("SLSConfigureDisplayIndependentOutput not found in SkyLight "
            "(macOS version may have moved or removed it)");
    return SH_NO_PRIVATE;
  }

  const bool persist = o->save;
  sh_display_t now;
  bool found = false;
  bool rolled_back = false;
  CGError e = kCGErrorSuccess;

  /* 第 1 段：只有关掉独立输出。docs 里验证过，单这一步就能救回来。 */
  sh_out("step 1/3: clear stale independent-output state");
  const step_t s1[] = { STEP_INDEPENDENT_OFF };
  int rc = apply_steps_guarded(cur.id, s1, sizeof s1 / sizeof s1[0], persist, &rolled_back);
  if (rolled_back) {
    sh_fail("aborted: SLSConfigureDisplayIndependentOutput(false) takes working displays down on "
            "this build, so the direction recorded in docs/README.md does not hold here");
    sh_note("nothing was left changed; the display configuration is back to where it started");
    return SH_UNFIXED;
  }
  if (rc == SH_OK) found = rescan(&cur, &now);

  /* 第 2 段：还没上线，补一个显式 enable 再一起提交。 */
  if (!(found && now.online)) {
    sh_out("step 2/3: retry with explicit display-enable + independent-output-off");
    const step_t s2[] = { STEP_ENABLE, STEP_INDEPENDENT_OFF };
    rc = apply_steps_guarded(cur.id, s2, sizeof s2 / sizeof s2[0], persist, &rolled_back);
    if (rolled_back) {
      sh_fail("aborted: the enable + independent-output-off combination breaks working displays here");
      sh_note("nothing was left changed; the display configuration is back to where it started");
      return SH_UNFIXED;
    }
    if (rc == SH_OK) found = rescan(&cur, &now);
  }

  /* 第 3 段：已经在线但没激活，说明缺一次模式绑定。 */
  if (found && now.online && !now.active) {
    sh_out("step 3/3: bind a display mode so the panel actually lights up");
    CGDisplayModeRef mode = sh_display_best_mode(now.id, 0, 0, 0);
    if (!mode) {
      sh_warn("no usable display mode reported for id=%u", now.id);
    } else {
      sh_snapshot_t before;
      snapshot_take(&before);

      CGDisplayConfigRef cfg = NULL;
      e = sh_config_begin(&cfg);
      sh_step("begin", e);
      if (e == kCGErrorSuccess) {
        e = CGConfigureDisplayWithDisplayMode(cfg, now.id, mode, NULL);
        sh_step("set-mode", e);
        if (e == kCGErrorSuccess) {
          e = sh_config_commit(cfg, persist);
          sh_step(persist ? "complete(permanent)" : "complete(session)", e);
        } else {
          sh_config_cancel(cfg);
        }
      }
      CFRelease(mode);
      usleep(RESCAN_DELAY_US);

      char what[192] = "";
      if (snapshot_regressions(&before, what, sizeof what))
        sh_warn("mode binding changed other displays: %s", what);

      found = rescan(&cur, &now);
    }
  }

  if (found && after_out) *after_out = now;

  if (found && now.online && now.active) {
    describe(&now, text, sizeof text);
    sh_out("recovered: %s", text);
    return SH_OK;
  }

  if (found) describe(&now, text, sizeof text);
  else snprintf(text, sizeof text, "display vanished from the display server entirely");
  sh_fail("still offline after repair: %s", text);
  sh_note("last resort: unplug and replug the link, or log out / restart WindowServer "
          "(WindowServer restart ends the GUI session)");
  return SH_UNFIXED;
}

/* -------------------------------------------------------------------------- */
/* list / status / detail                                                      */
/* -------------------------------------------------------------------------- */

int sh_cmd_list(const sh_options_t *o) {
  sh_display_t list[SH_MAX_DISPLAYS];
  size_t n = sh_display_scan(list, SH_MAX_DISPLAYS);

  size_t shown = 0, ghosts = 0;
  sh_display_print_header(stdout);
  for (size_t i = 0; i < n; i++) {
    if (list[i].kind == SH_KIND_GHOST) { ghosts++; if (!o->all) continue; }
    sh_display_print_row(&list[i], stdout);
    shown++;
  }
  if (!shown) sh_out("  (no displays reported)");
  if (ghosts && !o->all)
    sh_note("hid %zu ghost slot(s); pass --all to see them", ghosts);
  return SH_OK;
}

int sh_cmd_status(const sh_options_t *o) {
  (void)o;
  char sys[128];
  sh_system_summary(sys, sizeof sys);
  sh_out("screen-helper %s", SH_VERSION);
  sh_out("system        %s", sys);

  sh_display_t list[SH_MAX_DISPLAYS];
  size_t n = sh_display_scan(list, SH_MAX_DISPLAYS);

  char text[24];
  int external = 0;
  for (size_t i = 0; i < n; i++) {
    const sh_display_t *d = &list[i];
    if (d->kind == SH_KIND_GHOST) continue;
    if (d->kind == SH_KIND_BUILTIN) snprintf(text, sizeof text, "%-10s", "built-in");
    else { snprintf(text, sizeof text, "%-10s", "external"); external++; }

    char edid[32];
    sh_display_edid_name(d, edid, sizeof edid);

    char mode[48] = "-";
    if (d->has_mode) snprintf(mode, sizeof mode, "%zux%zu@%.0fHz", d->width, d->height, d->refresh);

    sh_out("%s    id=%u edid=%-10s %-16s online=%s active=%s%s%s",
           text, d->id, edid, mode,
           d->online ? "yes" : "no", d->active ? "yes" : "no",
           d->main ? " main" : "",
           d->mirrors ? " mirrored" : "");
  }

  int sm = -1, disp = -1;
  sh_icon_read(SH_MODULE_SCREEN_MIRRORING, &sm);
  sh_icon_read(SH_MODULE_DISPLAY, &disp);
  sh_out("menu bar      ScreenMirroring=%s  Display=%s",
         sh_icon_value_name(sm), sh_icon_value_name(disp));

  if (!sl_available()) sh_out("skylight      unavailable (dlopen failed)");
  else sh_out("skylight      ok%s",
              sl_has_independent_output() ? " (SLSConfigureDisplayIndependentOutput found)"
                                          : " (SLSConfigureDisplayIndependentOutput MISSING)");

  if (external == 0) {
    sh_out("state         no external display attached");
  } else {
    bool stuck = false;
    for (size_t i = 0; i < n; i++) {
      const sh_display_t *d = &list[i];
      if (d->kind == SH_KIND_EXTERNAL && (!d->online || !d->active)) stuck = true;
    }
    if (stuck) sh_out("state         external display present but OFFLINE — run: screen-helper fix");
    else       sh_out("state         ok");
  }
  return SH_OK;
}

int sh_cmd_detail(const sh_options_t *o) {
  sh_display_t list[SH_MAX_DISPLAYS];
  size_t n = sh_display_scan(list, SH_MAX_DISPLAYS);
  int targets[SH_MAX_DISPLAYS];
  size_t tn = 0;

  if (o->have_display || o->have_vendor) {
    tn = collect_targets(o, list, n, targets, SH_MAX_DISPLAYS, "inspect");
    if (!tn) return SH_NO_EXTERNAL;
  } else {
    for (size_t i = 0; i < n; i++) {
      if (list[i].kind == SH_KIND_GHOST && !o->all) continue;
      targets[tn++] = (int)i;
    }
  }

  for (size_t i = 0; i < tn; i++) {
    if (i) fputc('\n', stdout);
    sh_display_print_detail(&list[targets[i]], stdout);
  }
  return SH_OK;
}

/* -------------------------------------------------------------------------- */
/* fix                                                                         */
/* -------------------------------------------------------------------------- */

int sh_cmd_fix(const sh_options_t *o) {
  sh_display_t list[SH_MAX_DISPLAYS];
  size_t n = sh_display_scan(list, SH_MAX_DISPLAYS);

  int idx = -1;
  char err[256];
  int rc = sh_display_resolve(list, n, o->display, o->vendor, o->model,
                              o->have_vendor, &idx, err, sizeof err);
  if (rc != SH_OK) {
    sh_fail("%s", err);
    return rc;
  }

  if (o->dry_run) {
    char text[192];
    describe(&list[idx], text, sizeof text);
    sh_out("dry run, would repair %s", text);
    sh_note("step 1/3 clear stale independent-output state");
    sh_note("step 2/3 on failure: explicit enable + independent-output-off");
    sh_note("step 3/3 on failure: bind a display mode");
    sh_note("commit mode: %s", o->save ? "permanent" : "session");
    return SH_OK;
  }

  sh_display_t after;
  rc = recover_display(o, &list[idx], &after);
  if (rc != SH_OK) return rc;

  /* 顺手恢复那个「紫色两个屏幕叠加」的菜单栏图标 —— 用户点掉的就是它 */
  if (!o->skip_icon) {
    sh_out("step 4/4: restore the screen-mirroring menu bar icon");
    if (!sh_menu_bar_ensure_visible(true))
      sh_note("already set to always-visible, ControlCenter left alone");
  }

  if (after.mirrors) {
    sh_note("the display came back in mirror mode; run `screen-helper extend` to get extension back");
  }
  return SH_OK;
}

/* -------------------------------------------------------------------------- */
/* extend                                                                      */
/* -------------------------------------------------------------------------- */

static void arrange_origin(const sh_options_t *o, const sh_display_t *d,
                           const CGRect *main_bounds, size_t slot,
                           int32_t *x, int32_t *y) {
  size_t mw = (size_t)main_bounds->size.width;
  size_t mh = (size_t)main_bounds->size.height;
  size_t ew = d->has_mode ? d->width : 1920;
  size_t eh = d->has_mode ? d->height : 1080;

  switch (o->arrange) {
    case SH_ARRANGE_LEFT:
      *x = -(int32_t)(ew * (slot + 1));
      *y = 0;
      break;
    case SH_ARRANGE_ABOVE:
      *x = 0;
      *y = -(int32_t)(eh * (slot + 1));
      break;
    case SH_ARRANGE_BELOW:
      *x = 0;
      *y = (int32_t)(mh + eh * slot);
      break;
    case SH_ARRANGE_RIGHT:
    default:
      *x = (int32_t)(mw + ew * slot);
      *y = 0;
      break;
  }
}

int sh_cmd_extend(const sh_options_t *o) {
  sh_display_t list[SH_MAX_DISPLAYS];
  size_t n = sh_display_scan(list, SH_MAX_DISPLAYS);

  int targets[SH_MAX_DISPLAYS];
  size_t tn = collect_targets(o, list, n, targets, SH_MAX_DISPLAYS, "extend");
  if (!tn) return SH_NO_EXTERNAL;

  if (o->dry_run) {
    sh_out("dry run, would extend on %zu display(s)", tn);
    for (size_t i = 0; i < tn; i++) {
      char text[192];
      describe(&list[targets[i]], text, sizeof text);
      sh_note("%s", text);
    }
    sh_note("commit mode: %s", o->save ? "permanent" : "session");
    return SH_OK;
  }

  /*
   * 先把可能卡住的屏救回来。布局事务和修复事务分开提交：
   * docs 里验证过的是独立输出单独一次调用，混在一起会掩盖结果。
   */
  bool any_offline = false;
  for (size_t i = 0; i < tn; i++) {
    const sh_display_t *d = &list[targets[i]];
    if (!d->online || !d->active) any_offline = true;
  }
  if (any_offline) {
    sh_out("== repair pass ==");
    for (size_t i = 0; i < tn; i++) {
      sh_display_t *d = &list[targets[i]];
      if (d->online && d->active) continue;
      sh_display_t after;
      if (recover_display(o, d, &after) == SH_OK) list[targets[i]] = after;
    }
    /* 修完之后重新扫描，布局要用最新状态 */
    n = sh_display_scan(list, SH_MAX_DISPLAYS);
  }

  sh_out("== layout pass (extension mode) ==");
  CGRect main_bounds = CGDisplayBounds(CGMainDisplayID());
  CGDisplayConfigRef cfg = NULL;
  CGError e = sh_config_begin(&cfg);
  sh_step("begin", e);
  if (e != kCGErrorSuccess) return SH_ERR;

  size_t slot = 0;
  for (size_t i = 0; i < tn; i++) {
    sh_display_t *d = &list[targets[i]];
    if (!d->online) {
      sh_warn("id=%u still offline, skipped in layout", d->id);
      continue;
    }
    if (d->main) {
      /* 主屏不能移动，也不必取消镜像 */
      sh_note("id=%u is the main display, left as is", d->id);
      continue;
    }

    if (d->mirrors) {
      e = CGConfigureDisplayMirrorOfDisplay(cfg, d->id, kCGNullDirectDisplay);
      sh_step("mirror-off", e);
      if (e != kCGErrorSuccess) { sh_config_cancel(cfg); return SH_ERR; }
    } else {
      sh_note("mirror-off                = (already off)");
    }

    if (o->arrange != SH_ARRANGE_NONE) {
      int32_t x = 0, y = 0;
      arrange_origin(o, d, &main_bounds, slot, &x, &y);
      e = CGConfigureDisplayOrigin(cfg, d->id, x, y);
      sh_step("set-origin", e);
      if (e != kCGErrorSuccess) { sh_config_cancel(cfg); return SH_ERR; }
    }
    slot++;
  }

  e = sh_config_commit(cfg, o->save);
  sh_step(o->save ? "complete(permanent)" : "complete(session)", e);
  if (e != kCGErrorSuccess) return SH_ERR;

  usleep(RESCAN_DELAY_US);
  sh_out("layout now:");
  n = sh_display_scan(list, SH_MAX_DISPLAYS);
  sh_display_print_layout(stdout, list, n);
  return SH_OK;
}

/* -------------------------------------------------------------------------- */
/* mirror                                                                      */
/* -------------------------------------------------------------------------- */

int sh_cmd_mirror(const sh_options_t *o) {
  sh_display_t list[SH_MAX_DISPLAYS];
  size_t n = sh_display_scan(list, SH_MAX_DISPLAYS);

  CGDirectDisplayID master = CGMainDisplayID();
  int targets[SH_MAX_DISPLAYS];
  size_t tn = 0;

  if (o->have_display || o->have_vendor) {
    tn = collect_targets(o, list, n, targets, SH_MAX_DISPLAYS, "mirror");
    if (!tn) return SH_NO_EXTERNAL;
  } else {
    for (size_t i = 0; i < n; i++) {
      if (!list[i].online || list[i].main || list[i].kind == SH_KIND_GHOST) continue;
      targets[tn++] = (int)i;
    }
    if (!tn) { sh_fail("no secondary online display to mirror"); return SH_NO_EXTERNAL; }
  }

  CGDisplayConfigRef cfg = NULL;
  CGError e = sh_config_begin(&cfg);
  sh_step("begin", e);
  if (e != kCGErrorSuccess) return SH_ERR;

  for (size_t i = 0; i < tn; i++) {
    const sh_display_t *d = &list[targets[i]];
    if (d->id == master) {
      sh_warn("id=%u is the mirror master already, skipped", d->id);
      continue;
    }
    e = CGConfigureDisplayMirrorOfDisplay(cfg, d->id, master);
    sh_step("mirror-to-main", e);
    if (e != kCGErrorSuccess) { sh_config_cancel(cfg); return SH_ERR; }
  }

  e = sh_config_commit(cfg, o->save);
  sh_step(o->save ? "complete(permanent)" : "complete(session)", e);
  if (e != kCGErrorSuccess) return SH_ERR;

  usleep(RESCAN_DELAY_US);
  sh_out("layout now:");
  n = sh_display_scan(list, SH_MAX_DISPLAYS);
  sh_display_print_layout(stdout, list, n);
  return SH_OK;
}

/* -------------------------------------------------------------------------- */
/* enable / disable                                                            */
/* -------------------------------------------------------------------------- */

int sh_cmd_set_enabled(const sh_options_t *o, bool enabled) {
  sh_display_t list[SH_MAX_DISPLAYS];
  size_t n = sh_display_scan(list, SH_MAX_DISPLAYS);

  int idx = -1;
  char err[256];
  int rc = sh_display_resolve(list, n, o->display, o->vendor, o->model,
                              o->have_vendor, &idx, err, sizeof err);
  if (rc != SH_OK) { sh_fail("%s", err); return rc; }

  const sh_display_t *d = &list[idx];
  if (!enabled && !o->yes) {
    sh_fail("disabling a display is what got you here in the first place; re-run with --yes");
    return SH_USAGE;
  }
  if (o->dry_run) {
    sh_out("dry run, would %s display %u", enabled ? "enable" : "disable", d->id);
    return SH_OK;
  }
  if (!sl_has_enabled()) {
    sh_fail("SLSConfigureDisplayEnabled not found in SkyLight");
    return SH_NO_PRIVATE;
  }

  sh_out("%s display %u (edid check below)", enabled ? "enabling" : "disabling", d->id);
  CGDisplayConfigRef cfg = NULL;
  CGError e = sh_config_begin(&cfg);
  sh_step("begin", e);
  if (e != kCGErrorSuccess) return SH_ERR;

  e = sl_set_enabled(cfg, d->id, enabled);
  sh_step(enabled ? "display-enabled" : "display-disabled", e);
  if (e != kCGErrorSuccess) { sh_config_cancel(cfg); return SH_ERR; }

  e = sh_config_commit(cfg, o->save);
  sh_step(o->save ? "complete(permanent)" : "complete(session)", e);
  if (e != kCGErrorSuccess) return SH_ERR;

  usleep(RESCAN_DELAY_US);
  sh_display_t now;
  if (rescan(d, &now)) {
    char text[192];
    describe(&now, text, sizeof text);
    sh_out("now: %s", text);
    if (enabled && (!now.online || !now.active)) {
      sh_note("plain enable did not bring it online — that is exactly the state");
      sh_note("`screen-helper fix` repairs: run it now.");
    }
  }
  return SH_OK;
}

/* -------------------------------------------------------------------------- */
/* mode                                                                        */
/* -------------------------------------------------------------------------- */

int sh_cmd_mode(const sh_options_t *o, const char *spec) {
  unsigned w = 0, h = 0;
  double hz = 0;
  if (sscanf(spec, "%ux%u@%lf", &w, &h, &hz) < 2) {
    sh_fail("cannot parse mode %s (expect WxH or WxH@Hz, e.g. 2560x1440@60)", spec);
    return SH_USAGE;
  }

  sh_display_t list[SH_MAX_DISPLAYS];
  size_t n = sh_display_scan(list, SH_MAX_DISPLAYS);

  int idx = -1;
  char err[256];
  int rc = sh_display_resolve(list, n, o->display, o->vendor, o->model,
                              o->have_vendor, &idx, err, sizeof err);
  if (rc != SH_OK) { sh_fail("%s", err); return rc; }

  const sh_display_t *d = &list[idx];
  if (!d->online) {
    sh_fail("display %u is offline; run `screen-helper fix` first", d->id);
    return SH_UNFIXED;
  }

  CGDisplayModeRef mode = sh_display_best_mode(d->id, w, h, hz);
  if (!mode) {
    sh_fail("no mode matching %ux%u%s on display %u",
            w, h, hz > 0 ? "@Hz" : "", d->id);
    return SH_ERR;
  }

  sh_out("selected mode: %zux%zu @%.0fHz (id=%u)",
         CGDisplayModeGetWidth(mode), CGDisplayModeGetHeight(mode),
         CGDisplayModeGetRefreshRate(mode), d->id);

  if (o->dry_run) { CFRelease(mode); return SH_OK; }

  CGDisplayConfigRef cfg = NULL;
  CGError e = sh_config_begin(&cfg);
  sh_step("begin", e);
  if (e != kCGErrorSuccess) { CFRelease(mode); return SH_ERR; }

  e = CGConfigureDisplayWithDisplayMode(cfg, d->id, mode, NULL);
  sh_step("set-mode", e);
  CFRelease(mode);
  if (e != kCGErrorSuccess) { sh_config_cancel(cfg); return SH_ERR; }

  e = sh_config_commit(cfg, o->save);
  sh_step(o->save ? "complete(permanent)" : "complete(session)", e);
  if (e != kCGErrorSuccess) return SH_ERR;

  usleep(RESCAN_DELAY_US);
  sh_out("layout now:");
  n = sh_display_scan(list, SH_MAX_DISPLAYS);
  sh_display_print_layout(stdout, list, n);
  return SH_OK;
}

/* -------------------------------------------------------------------------- */
/* save                                                                        */
/* -------------------------------------------------------------------------- */

/*
 * 「把当前这套摆法固化下来」。
 *
 * CoreGraphics 没有「保存当前配置」这种 API，能持久化的只有一次显式的、
 * 带 kCGConfigurePermanently 的完整配置提交。所以这里先把当前布局读出来，
 * 再原样重新提交一次。
 */
int sh_cmd_save(const sh_options_t *o) {
  (void)o;
  sh_display_t list[SH_MAX_DISPLAYS];
  size_t n = sh_display_scan(list, SH_MAX_DISPLAYS);

  CGDisplayConfigRef cfg = NULL;
  CGError e = sh_config_begin(&cfg);
  sh_step("begin", e);
  if (e != kCGErrorSuccess) return SH_ERR;

  CGError first_err = kCGErrorSuccess;
  const char *first_label = NULL;
  bool touched = false;

  for (size_t i = 0; i < n; i++) {
    const sh_display_t *d = &list[i];
    if (!d->online || d->kind == SH_KIND_GHOST) continue;

    if (d->mirrors) {
      e = CGConfigureDisplayMirrorOfDisplay(cfg, d->id, d->mirrors);
      sh_step("mirror-keep", e);
      touched = true;
      if (e != kCGErrorSuccess && first_err == kCGErrorSuccess) { first_err = e; first_label = "mirror-keep"; }
      continue;
    }

    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(d->id);
    if (mode) {
      e = CGConfigureDisplayWithDisplayMode(cfg, d->id, mode, NULL);
      CFRelease(mode);
      sh_step("mode-keep", e);
      touched = true;
      if (e != kCGErrorSuccess && first_err == kCGErrorSuccess) { first_err = e; first_label = "mode-keep"; }
    }

    /* 主屏的原点固定在 0,0，提交它会返回参数错误，直接跳过 */
    if (!d->main && d->has_bounds) {
      e = CGConfigureDisplayOrigin(cfg, d->id, d->x, d->y);
      sh_step("origin-keep", e);
      if (e != kCGErrorSuccess && first_err == kCGErrorSuccess) { first_err = e; first_label = "origin-keep"; }
    }
  }

  if (!touched) {
    sh_warn("nothing to persist, cancelling");
    sh_config_cancel(cfg);
    return SH_ERR;
  }

  if (first_err != kCGErrorSuccess)
    sh_warn("%s returned %s, committing anyway", first_label, sh_cg_error(first_err));

  e = sh_config_commit(cfg, true);
  sh_step("complete(permanent)", e);
  if (e != kCGErrorSuccess) return SH_ERR;

  usleep(RESCAN_DELAY_US);
  sh_out("saved layout:");
  n = sh_display_scan(list, SH_MAX_DISPLAYS);
  sh_display_print_layout(stdout, list, n);
  return SH_OK;
}
