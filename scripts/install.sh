#!/usr/bin/env bash
#
# screen-helper 安装脚本（随发布包一起分发）
#
#   ./install.sh                    安装到 /usr/local/bin
#   PREFIX=~/.local ./install.sh    装到别处，不需要 sudo
#
# 从网上下载的包会被 Gatekeeper 打上 com.apple.quarantine，命令行工具又没有
# 「仍要打开」这种入口，所以这里顺手清掉扩展属性并补一次 ad-hoc 签名。

set -euo pipefail

BIN="screen-helper"
PREFIX="${PREFIX:-/usr/local}"

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$SRC_DIR/$BIN"

[ -f "$SRC" ] || { echo "install: $SRC not found — run this from the unpacked tarball" >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

cp "$SRC" "$STAGE/$BIN"
chmod 0755 "$STAGE/$BIN"

# 清掉 quarantine 等扩展属性，否则直接执行会被 Gatekeeper 拦下。
xattr -c "$STAGE/$BIN" 2>/dev/null || true

# ad-hoc 签名：Apple Silicon 上签名无效就起不来，重签一次最稳。
codesign --force --sign - "$STAGE/$BIN" >/dev/null 2>&1 || true

DEST="$PREFIX/bin"

# 目标目录可能一层都不存在，往上找到第一个真实存在的目录来判断要不要 sudo。
probe="$DEST"
while [ ! -e "$probe" ] && [ "$probe" != "/" ]; do
  probe="$(dirname "$probe")"
done

SUDO=""
if [ ! -w "$probe" ]; then
  SUDO="sudo"
fi

echo "install: $DEST/$BIN"
$SUDO install -d "$DEST"
$SUDO install -m 0755 "$STAGE/$BIN" "$DEST/$BIN"

echo
echo "installed. try:"
echo "  $BIN status"
echo
case ":$PATH:" in
  *":$DEST:"*) ;;
  *) echo "\"$DEST\" is not in your PATH, add it:"
     echo "  export PATH=\"$DEST:\$PATH\"" ;;
esac
