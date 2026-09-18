#include "diag.h"

#include "common.h"
#include "display.h"
#include "menubar.h"
#include "skylight.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>

void sh_system_summary(char *buf, size_t buflen) {
  char osver[64] = "?";
  char build[64] = "?";
  char machine[64] = "?";
  size_t len = sizeof osver;
  if (sysctlbyname("kern.osproductversion", osver, &len, NULL, 0) != 0)
    snprintf(osver, sizeof osver, "?");
  len = sizeof build;
  if (sysctlbyname("kern.osversion", build, &len, NULL, 0) != 0)
    snprintf(build, sizeof build, "?");
  len = sizeof machine;
  if (sysctlbyname("hw.machine", machine, &len, NULL, 0) != 0)
    snprintf(machine, sizeof machine, "?");
  snprintf(buf, buflen, "macOS %s build %s %s", osver, build, machine);
}

/* -------------------------------------------------------------------------- */
/* DisplayPort 链路                                                            */
/* -------------------------------------------------------------------------- */

static void print_cf_value(const char *key, CFTypeRef value) {
  char text[192] = "";
  if (!value) { printf("  %-28s -\n", key); return; }

  CFTypeID type = CFGetTypeID(value);
  if (type == CFStringGetTypeID()) {
    CFStringGetCString((CFStringRef)value, text, sizeof text, kCFStringEncodingUTF8);
  } else if (type == CFBooleanGetTypeID()) {
    snprintf(text, sizeof text, "%s", CFBooleanGetValue((CFBooleanRef)value) ? "Yes" : "No");
  } else if (type == CFNumberGetTypeID()) {
    long long n = 0;
    CFNumberGetValue((CFNumberRef)value, kCFNumberLongLongType, &n);
    snprintf(text, sizeof text, "%lld", n);
  } else {
    snprintf(text, sizeof text, "<%s>", "binary/data");
  }
  printf("  %-28s %s\n", key, text);
}

static void print_dp_link(void) {
  io_service_t service = IOServiceGetMatchingService(
      kIOMainPortDefault, IOServiceMatching("IOPortTransportStateDisplayPort"));
  if (!service) {
    sh_note("no IOPortTransportStateDisplayPort node (internal panel only?)");
    return;
  }

  static const char *keys[] = {
    "ProductName", "Active", "HPD_State", "HPD_StateDescription",
    "DriverStatus", "DriverStatusDescription",
    "AuthorizationRequired", "AuthorizationStatus", "AuthorizationStatusDescription",
  };

  CFMutableDictionaryRef props = NULL;
  if (IORegistryEntryCreateCFProperties(service, &props, kCFAllocatorDefault, 0) == KERN_SUCCESS && props) {
    printf("  node: IOPortTransportStateDisplayPort\n");
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
      CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, keys[i], kCFStringEncodingUTF8);
      if (k) {
        print_cf_value(keys[i], CFDictionaryGetValue(props, k));
        CFRelease(k);
      }
    }
    CFRelease(props);
  } else {
    sh_note("could not read properties from IOPortTransportStateDisplayPort");
  }

  IOObjectRelease(service);
}

/* -------------------------------------------------------------------------- */

int sh_cmd_diag(bool with_logs) {
  char sys[128];
  sh_system_summary(sys, sizeof sys);

  printf("== System ==\n");
  printf("  %s\n", sys);
  printf("  screen-helper %s\n", SH_VERSION);

  printf("\n== Displays ==\n");
  sh_display_t list[SH_MAX_DISPLAYS];
  size_t n = sh_display_scan(list, SH_MAX_DISPLAYS);
  sh_display_print_header(stdout);
  for (size_t i = 0; i < n; i++) sh_display_print_row(&list[i], stdout);
  printf("\n  layout:\n");
  sh_display_print_layout(stdout, list, n);

  printf("\n== DisplayPort link ==\n");
  print_dp_link();

  printf("\n== Menu bar modules ==\n");
  int sm = -1, disp = -1;
  sh_icon_read(SH_MODULE_SCREEN_MIRRORING, &sm);
  sh_icon_read(SH_MODULE_DISPLAY, &disp);
  printf("  %-28s %s\n", "ScreenMirroring", sh_icon_value_name(sm));
  printf("  %-28s %s\n", "Display", sh_icon_value_name(disp));

  printf("\n== Private API ==\n");
  printf("  %-28s %s\n", "SkyLight framework", sl_available() ? "loaded" : "unavailable");
  printf("  %-28s %s\n", "SLSConfigureDisplayIndependentOutput",
         sl_has_independent_output() ? "found" : "MISSING");
  printf("  %-28s %s\n", "SLSConfigureDisplayEnabled",
         sl_has_enabled() ? "found" : "MISSING");

  if (with_logs) {
    printf("\n== Recent ControlCenter display logs (30m) ==\n");
    const char *argv[] = {
      "/usr/bin/log", "show", "--last", "30m", "--style", "compact",
      "--predicate",
      "process == \"ControlCenter\" AND subsystem == \"com.apple.controlcenter\" "
      "AND (category == \"offlineDisplays\" OR category == \"mirroring\" OR category == \"menu\")",
      NULL
    };
    sh_run(argv);
  }
  return SH_OK;
}
