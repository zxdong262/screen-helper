/*
 * display.h — 显示器枚举、分类与配置事务
 */
#ifndef SH_DISPLAY_H
#define SH_DISPLAY_H

#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/*
 * 扫描 display id 的上限。CoreGraphics 的 display id 是小整数，
 * docs/ 里的历史脚本用 1..64 探测“已连接但离线”的屏，这里保持一致。
 * 注意：无效 id 的 CGDisplayVendorNumber 返回 0xFFFFFFFF（不是 0），
 * 判断时必须同时排除 0 和 0xFFFFFFFF。
 */
#define SH_MAX_DISPLAY_ID 64
#define SH_MAX_DISPLAYS   32
#define SH_BAD_U32        0xFFFFFFFFu

typedef enum {
  SH_KIND_BUILTIN = 0, /* 内建屏 */
  SH_KIND_EXTERNAL,    /* 外接屏 */
  SH_KIND_GHOST,       /* 内建屏的离线残影：非 builtin 但 EDID 与内建屏相同 */
} sh_kind_t;

typedef struct {
  CGDirectDisplayID id;
  sh_kind_t kind;
  bool builtin;              /* CGDisplayIsBuiltin 原始值 */
  uint32_t vendor;
  uint32_t model;
  uint32_t serial;
  bool online;
  bool active;
  bool main;
  CGDirectDisplayID mirrors; /* kCGNullDirectDisplay 表示没有镜像任何屏 */
  bool has_mode;
  size_t width, height;      /* 逻辑（UI）尺寸 */
  size_t pixel_width, pixel_height;
  double refresh;
  bool has_bounds;
  int32_t x, y;              /* 全局坐标原点 */
} sh_display_t;

/* id 是否对应一个真实存在的显示槽位 */
bool sh_display_id_valid(CGDirectDisplayID id);

/* 枚举所有候选显示槽位，结果排序：在线优先，幽灵条目最后。返回数量。 */
size_t sh_display_scan(sh_display_t *out, size_t cap);

int sh_display_find_external(const sh_display_t *list, size_t n);
int sh_display_find_by_id(const sh_display_t *list, size_t n, CGDirectDisplayID id);
int sh_display_find_by_edid(const sh_display_t *list, size_t n, uint32_t vendor, uint32_t model);
size_t sh_display_list_externals(const sh_display_t *list, size_t n, int *out, size_t cap);

const char *sh_display_kind_name(sh_kind_t k);
void sh_display_edid_name(const sh_display_t *d, char *buf, size_t buflen);

void sh_display_print_header(FILE *f);
void sh_display_print_row(const sh_display_t *d, FILE *f);
void sh_display_print_detail(const sh_display_t *d, FILE *f);
void sh_display_print_layout(FILE *f, const sh_display_t *list, size_t n);

/*
 * 决定操作目标。
 *   - vendor/model 提示优先（插拔后 id 会变，EDID 不会）
 *   - 其次 display id
 *   - 最后默认第一个外接屏
 * 成功返回 0 并把下标写进 *out_index，失败写错误信息并返回 SH_NO_EXTERNAL。
 */
int sh_display_resolve(const sh_display_t *list, size_t n,
                       CGDirectDisplayID id_hint, uint32_t vendor_hint, uint32_t model_hint,
                       bool have_vendor, int *out_index, char *err, size_t errlen);

/* ---- 配置事务 ------------------------------------------------------------ */

CGError sh_config_begin(CGDisplayConfigRef *cfg);
CGError sh_config_commit(CGDisplayConfigRef cfg, bool persist);
void    sh_config_cancel(CGDisplayConfigRef cfg);

/* 在 id 的所有模式里挑一个最接近 w×h@hz 的。want_w/want_h 为 0 表示要“最大的”。
 *
 * want_hz > 0  按给的刷新率贴近程度挑，写多少就是多少（用户可以硬要 72/144）。
 * want_hz <= 0 走「安全刷新率」策略：优先 60Hz，其次 ≤60 里最接近 60 的，
 *              最后才轮到 >60 的低档。高刷时序在部分面板上会被导出却同步不了
 *              （2026-09-19：2560x1440@72Hz 黑屏但 state 仍报 ok），所以默认不取高刷。
 *
 * 返回的是 retained 的 mode，用完要 release。 */
CGDisplayModeRef sh_display_best_mode(CGDirectDisplayID id,
                                     size_t want_w, size_t want_h, double want_hz);

#endif /* SH_DISPLAY_H */
