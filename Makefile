# screen-helper — macOS 外接显示器扩展 / 显示状态管理工具
#
# 纯 clang + 系统框架，无第三方依赖，无 Xcode 工程。
#
#   make              构建原生 ./screen-helper（本机开发用，最快）
#   make check        构建并跑一次 list
#   make smoke        构建并跑一组只读自检（发版前的手动确认）
#   make universal    构建 arm64 + x86_64 通用二进制到 dist/
#   make dist         构建通用二进制并打包成 dist/*.tar.gz + .sha256
#   make install      安装到 /usr/local/bin
#   make uninstall    卸载
#   make clean        清理构建产物

BIN    := screen-helper
PREFIX ?= /usr/local
CC     := clang

# 版本号的唯一来源是仓库根目录的 VERSION 文件，构建时注入为 SH_VERSION 宏。
VERSION := $(shell cat VERSION 2>/dev/null || echo 0.0.0-dev)

DIST      := dist
STAGE     := $(DIST)/pkg
UNIVERSAL := $(DIST)/$(BIN)
PKGNAME   := $(BIN)-v$(VERSION)-macos-universal
TARBALL   := $(DIST)/$(PKGNAME).tar.gz

SRCS := $(sort $(wildcard src/*.c))
HDRS := $(sort $(wildcard src/*.h))

CFLAGS  := -std=c11 -O2 -Wall -Wextra -Wno-deprecated-declarations
CFLAGS  += -DSH_VERSION=\"$(VERSION)\"

# 同时喂两个 -arch，clang 直接产出 fat 二进制，CI 上不需要两台机器。
ARCHS := -arch arm64 -arch x86_64

FRAMEWORKS := -framework CoreGraphics -framework CoreFoundation -framework IOKit

.PHONY: all check smoke universal dist install uninstall clean

all: $(BIN)

$(BIN): $(SRCS) $(HDRS) VERSION
	$(CC) $(CFLAGS) $(SRCS) $(FRAMEWORKS) -o $@

check: $(BIN)
	./$(BIN) list

smoke: $(BIN)
	./$(BIN) --version
	./$(BIN) help > /dev/null
	./$(BIN) list > /dev/null
	./$(BIN) diag > /dev/null
	./$(BIN) icon status > /dev/null
	@rc=0; ./$(BIN) fix --dry-run > /dev/null || rc=$$?; \
	 case "$$rc" in \
	   0|3) echo "fix --dry-run: exit $$rc (ok)";; \
	   *)   echo "smoke: fix --dry-run exited $$rc"; exit 1;; \
	 esac
	@echo "smoke: ok"

universal: $(UNIVERSAL)

$(UNIVERSAL): $(SRCS) $(HDRS) VERSION
	@mkdir -p $(DIST)
	$(CC) $(CFLAGS) $(ARCHS) $(SRCS) $(FRAMEWORKS) -o $@
	codesign --force --sign - $@ 2>/dev/null || true

# 发布包 = 通用二进制 + 安装脚本 + 文档，解压后直接 ./install.sh。
dist: $(TARBALL)

$(TARBALL): $(UNIVERSAL) scripts/install.sh README.md README_CN.md LICENSE
	@rm -rf $(STAGE)
	@mkdir -p $(STAGE)
	cp $(UNIVERSAL) $(STAGE)/$(BIN)
	cp scripts/install.sh $(STAGE)/install.sh
	chmod +x $(STAGE)/install.sh
	cp README.md README_CN.md LICENSE $(STAGE)/
	tar -czf $@ -C $(STAGE) .
	@rm -rf $(STAGE)
	cd $(DIST) && shasum -a 256 $(PKGNAME).tar.gz > $(PKGNAME).tar.gz.sha256
	@echo "dist: $(TARBALL)"
	@cat $(TARBALL).sha256

install: $(BIN)
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m 0755 $(BIN) "$(DESTDIR)$(PREFIX)/bin/$(BIN)"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/$(BIN)"

clean:
	rm -f $(BIN)
	rm -rf $(DIST)
