#include "skylight.h"

#include <dlfcn.h>
#include <string.h>

#define SKYLIGHT_PATH "/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight"

typedef CGError (*SLIndependentOutputFn)(CGDisplayConfigRef, CGDirectDisplayID, bool);
typedef CGError (*SLEnabledFn)(CGDisplayConfigRef, CGDirectDisplayID, bool);

typedef struct {
  bool loaded;
  void *handle;
  SLIndependentOutputFn independent_output;
  SLEnabledFn enabled;
  char load_error[256];
} sl_state_t;

static sl_state_t g_sl;

static void sl_load(void) {
  if (g_sl.loaded) return;
  g_sl.loaded = true;

  g_sl.handle = dlopen(SKYLIGHT_PATH, RTLD_NOW | RTLD_LOCAL);
  if (!g_sl.handle) {
    const char *err = dlerror();
    snprintf(g_sl.load_error, sizeof g_sl.load_error, "%s", err ? err : "dlopen failed");
    return;
  }
  /* dlsym 的返回值到函数指针需要一次转换，POSIX 保证这在实践中成立。 */
  g_sl.independent_output = (SLIndependentOutputFn)(void (*)(void))
      dlsym(g_sl.handle, "SLSConfigureDisplayIndependentOutput");
  g_sl.enabled = (SLEnabledFn)(void (*)(void))
      dlsym(g_sl.handle, "SLSConfigureDisplayEnabled");
}

bool sl_available(void) {
  sl_load();
  return g_sl.handle != NULL;
}

bool sl_has_independent_output(void) {
  sl_load();
  return g_sl.independent_output != NULL;
}

bool sl_has_enabled(void) {
  sl_load();
  return g_sl.enabled != NULL;
}

CGError sl_set_independent_output(CGDisplayConfigRef cfg, CGDirectDisplayID id, bool enabled) {
  sl_load();
  if (!g_sl.independent_output) return kCGErrorFailure;
  return g_sl.independent_output(cfg, id, enabled);
}

CGError sl_set_enabled(CGDisplayConfigRef cfg, CGDirectDisplayID id, bool enabled) {
  sl_load();
  if (!g_sl.enabled) return kCGErrorFailure;
  return g_sl.enabled(cfg, id, enabled);
}
