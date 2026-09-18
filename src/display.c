#include "display.h"

#include "common.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* 枚举与分类                                                                  */
/* -------------------------------------------------------------------------- */

bool sh_display_id_valid(CGDirectDisplayID id) {
  if (id == kCGNullDirectDisplay) return false;
  uint32_t vendor = CGDisplayVendorNumber(id);
  return vendor != 0 && vendor != SH_BAD_U32;
}

static void fill(sh_display_t *d, CGDirectDisplayID id) {
  memset(d, 0, sizeof *d);
  d->id = id;
  d->builtin = CGDisplayIsBuiltin(id);
  d->vendor = CGDisplayVendorNumber(id);
  d->model = CGDisplayModelNumber(id);
  d->serial = CGDisplaySerialNumber(id);
  d->online = CGDisplayIsOnline(id);
  d->active = CGDisplayIsActive(id);
  d->main = CGDisplayIsMain(id);
  d->mirrors = kCGNullDirectDisplay;

  CGDirectDisplayID mir = CGDisplayMirrorsDisplay(id);
  if (mir != kCGNullDirectDisplay && mir != id) d->mirrors = mir;

  /* 离线屏 CGDisplayCopyDisplayMode 可能返回 NULL，这是正常情况 */
  CGDisplayModeRef m = CGDisplayCopyDisplayMode(id);
  if (m) {
    d->has_mode = true;
    d->width = CGDisplayModeGetWidth(m);
    d->height = CGDisplayModeGetHeight(m);
    d->pixel_width = CGDisplayModeGetPixelWidth(m);
    d->pixel_height = CGDisplayModeGetPixelHeight(m);
    d->refresh = CGDisplayModeGetRefreshRate(m);
    CGDisplayModeRelease(m);
  }

  CGRect b = CGDisplayBounds(id);
  if (!CGRectIsEmpty(b)) {
    d->has_bounds = true;
    d->x = (int32_t)b.origin.x;
    d->y = (int32_t)b.origin.y;
  }
}

static int rank_of(const sh_display_t *d) {
  int r = d->online ? 0 : 1;
  if (d->kind == SH_KIND_GHOST) r += 4;
  return r;
}

static int compare(const void *a, const void *b) {
  const sh_display_t *x = a, *y = b;
  int rx = rank_of(x), ry = rank_of(y);
  if (rx != ry) return rx - ry;
  return x->id < y->id ? -1 : (x->id > y->id ? 1 : 0);
}

size_t sh_display_scan(sh_display_t *out, size_t cap) {
  sh_display_t all[SH_MAX_DISPLAYS];
  size_t n = 0;

  for (CGDirectDisplayID id = 1; id < SH_MAX_DISPLAY_ID && n < SH_MAX_DISPLAYS; id++) {
    if (!sh_display_id_valid(id)) continue;
    fill(&all[n], id);
    n++;
  }

  /*
   * 分类。除了「是否内建」，还要识别幽灵条目：macOS 会保留一个非 builtin
   * 的槽位，其 EDID 与内建屏完全相同（本机实测 id=3，vendor=1552
   * model=41033）。如果不排除，把「第一个外接屏」当成目标时会打到它身上。
   */
  for (size_t i = 0; i < n; i++) {
    if (all[i].builtin) { all[i].kind = SH_KIND_BUILTIN; continue; }

    bool same_edid_as_builtin = false;
    for (size_t j = 0; j < n; j++) {
      if (!all[j].builtin) continue;
      if (all[j].vendor == all[i].vendor && all[j].model == all[i].model) {
        same_edid_as_builtin = true;
        break;
      }
    }
    if (same_edid_as_builtin && !all[i].online) all[i].kind = SH_KIND_GHOST;
    else all[i].kind = SH_KIND_EXTERNAL;
  }

  qsort(all, n, sizeof all[0], compare);
  if (n > cap) n = cap;
  memcpy(out, all, n * sizeof all[0]);
  return n;
}

int sh_display_find_external(const sh_display_t *list, size_t n) {
  for (size_t i = 0; i < n; i++) if (list[i].kind == SH_KIND_EXTERNAL) return (int)i;
  return -1;
}

int sh_display_find_by_id(const sh_display_t *list, size_t n, CGDirectDisplayID id) {
  for (size_t i = 0; i < n; i++) if (list[i].id == id) return (int)i;
  return -1;
}

int sh_display_find_by_edid(const sh_display_t *list, size_t n, uint32_t vendor, uint32_t model) {
  for (size_t i = 0; i < n; i++) {
    if (list[i].vendor == vendor && list[i].model == model && list[i].kind == SH_KIND_EXTERNAL)
      return (int)i;
  }
  return -1;
}

size_t sh_display_list_externals(const sh_display_t *list, size_t n, int *out, size_t cap) {
  size_t used = 0;
  for (size_t i = 0; i < n && used < cap; i++) {
    if (list[i].kind == SH_KIND_EXTERNAL) out[used++] = (int)i;
  }
  return used;
}

const char *sh_display_kind_name(sh_kind_t k) {
  switch (k) {
    case SH_KIND_BUILTIN:  return "builtin";
    case SH_KIND_EXTERNAL: return "external";
    case SH_KIND_GHOST:    return "ghost";
  }
  return "?";
}

void sh_display_edid_name(const sh_display_t *d, char *buf, size_t buflen) {
  snprintf(buf, buflen, "%u:%u", d->vendor, d->model);
}

/* -------------------------------------------------------------------------- */
/* 输出                                                                        */
/* -------------------------------------------------------------------------- */

void sh_display_print_header(FILE *f) {
  fprintf(f, "  ID  %-9s %-11s %-6s %-6s %-5s %-7s %s\n",
          "KIND", "EDID", "ONLINE", "ACTIVE", "MAIN", "MIRROR", "MODE");
}

void sh_display_print_row(const sh_display_t *d, FILE *f) {
  char edid[32];
  sh_display_edid_name(d, edid, sizeof edid);

  char mode[64] = "-";
  if (d->has_mode) {
    snprintf(mode, sizeof mode, "%zux%zu @%.0f", d->width, d->height,
             d->refresh > 0 ? d->refresh : 0.0);
  }

  fprintf(f, "  %2u  %-9s %-11s %-6s %-6s %-5s %-7s %s\n",
          d->id,
          sh_display_kind_name(d->kind),
          edid,
          d->online ? "yes" : "no",
          d->active ? "yes" : "no",
          d->main ? "yes" : "no",
          d->mirrors ? "yes" : "-",
          mode);
}

void sh_display_print_detail(const sh_display_t *d, FILE *f) {
  char edid[32];
  sh_display_edid_name(d, edid, sizeof edid);
  fprintf(f, "id            = %u\n", d->id);
  fprintf(f, "kind          = %s (CGDisplayIsBuiltin=%s)\n",
          sh_display_kind_name(d->kind), d->builtin ? "true" : "false");
  fprintf(f, "edid          = %s\n", edid);
  fprintf(f, "serial        = %u\n", d->serial);
  fprintf(f, "online        = %s\n", d->online ? "yes" : "no");
  fprintf(f, "active        = %s\n", d->active ? "yes" : "no");
  fprintf(f, "main          = %s\n", d->main ? "yes" : "no");
  if (d->mirrors) fprintf(f, "mirrors       = display %u\n", d->mirrors);
  else            fprintf(f, "mirrors       = off\n");
  if (d->has_mode) {
    fprintf(f, "mode          = %zux%zu @%.0fHz (pixels %zux%zu)\n",
            d->width, d->height, d->refresh, d->pixel_width, d->pixel_height);
  } else {
    fprintf(f, "mode          = unavailable\n");
  }
  if (d->has_bounds) fprintf(f, "origin        = %d,%d\n", d->x, d->y);
  else               fprintf(f, "origin        = -\n");
}

void sh_display_print_layout(FILE *f, const sh_display_t *list, size_t n) {
  for (size_t i = 0; i < n; i++) {
    const sh_display_t *d = &list[i];
    if (!d->online) continue;
    fprintf(f, "  id=%u %s", d->id, sh_display_kind_name(d->kind));
    if (d->has_bounds) fprintf(f, " at %d,%d", d->x, d->y);
    if (d->has_mode)   fprintf(f, " %zux%zu@%.0fHz", d->width, d->height, d->refresh);
    if (d->main)       fprintf(f, " [main]");
    if (d->mirrors)    fprintf(f, " [mirrors %u]", d->mirrors);
    fputc('\n', f);
  }
}

/* -------------------------------------------------------------------------- */
/* 目标解析                                                                    */
/* -------------------------------------------------------------------------- */

int sh_display_resolve(const sh_display_t *list, size_t n,
                       CGDirectDisplayID id_hint, uint32_t vendor_hint, uint32_t model_hint,
                       bool have_vendor, int *out_index, char *err, size_t errlen) {
  int idx = -1;

  if (have_vendor) {
    idx = sh_display_find_by_edid(list, n, vendor_hint, model_hint);
    if (idx < 0) {
      snprintf(err, errlen, "no external display with EDID %u:%u (server may still be booting)",
               vendor_hint, model_hint);
      return SH_NO_EXTERNAL;
    }
  } else if (id_hint != kCGNullDirectDisplay) {
    idx = sh_display_find_by_id(list, n, id_hint);
    if (idx < 0) {
      snprintf(err, errlen, "display id %u not found among %zu slots", id_hint, n);
      return SH_NO_EXTERNAL;
    }
    if (list[idx].kind == SH_KIND_GHOST) {
      snprintf(err, errlen, "display id %u is a ghost entry (built-in EDID), refusing", id_hint);
      return SH_NO_EXTERNAL;
    }
  } else {
    idx = sh_display_find_external(list, n);
    if (idx < 0) {
      snprintf(err, errlen, "no external display detected");
      return SH_NO_EXTERNAL;
    }
  }

  if (out_index) *out_index = idx;
  return SH_OK;
}

/* -------------------------------------------------------------------------- */
/* 配置事务                                                                    */
/* -------------------------------------------------------------------------- */

CGError sh_config_begin(CGDisplayConfigRef *cfg) {
  *cfg = NULL;
  return CGBeginDisplayConfiguration(cfg);
}

CGError sh_config_commit(CGDisplayConfigRef cfg, bool persist) {
  return CGCompleteDisplayConfiguration(
      cfg, persist ? kCGConfigurePermanently : kCGConfigureForSession);
}

void sh_config_cancel(CGDisplayConfigRef cfg) {
  if (cfg) CGCancelDisplayConfiguration(cfg);
}

/* -------------------------------------------------------------------------- */
/* 模式选择                                                                    */
/* -------------------------------------------------------------------------- */

static double refresh_of(CGDisplayModeRef m) {
  double r = CGDisplayModeGetRefreshRate(m);
  return r > 0 ? r : 60.0;
}

CGDisplayModeRef sh_display_best_mode(CGDirectDisplayID id,
                                     size_t want_w, size_t want_h, double want_hz) {
  CFArrayRef modes = CGDisplayCopyAllDisplayModes(id, NULL);
  if (!modes) return NULL;

  CGDisplayModeRef best = NULL;
  double best_score = -1e18;

  CFIndex count = CFArrayGetCount(modes);
  for (CFIndex i = 0; i < count; i++) {
    CGDisplayModeRef m = (CGDisplayModeRef)CFArrayGetValueAtIndex(modes, i);
    if (!m) continue;

    size_t w = CGDisplayModeGetWidth(m);
    size_t h = CGDisplayModeGetHeight(m);
    size_t pw = CGDisplayModeGetPixelWidth(m);
    size_t ph = CGDisplayModeGetPixelHeight(m);

    double score;
    if (want_w && want_h) {
      bool hit = (w == want_w && h == want_h) || (pw == want_w && ph == want_h);
      if (!hit) continue;
      /* 尺寸精确匹配后，按刷新率贴近程度打分 */
      score = 1000.0 - fabs(refresh_of(m) - (want_hz > 0 ? want_hz : refresh_of(m)));
    } else {
      /* 没指定就挑面积最大的，同面积取刷新率高的 */
      score = (double)(w * h) * 1000.0 + refresh_of(m);
    }

    if (score > best_score) { best_score = score; best = m; }
  }

  /* best 是 CFArray 里的 borrowed 引用，外面要能安全 release，这里 retain 一份 */
  if (best) CFRetain(best);
  CFRelease(modes);
  return best;
}
