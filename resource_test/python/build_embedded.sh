#!/bin/bash
# build_embedded.sh — 将 csv_verify 二进制嵌入 Python 可视化脚本
# 产出 plot_gui.py + plot_profile.py 可独立部署，无需额外二进制文件

set -e
cd "$(dirname "$0")"

# 1. 编译 csv_verify
echo "🔨 编译 csv_verify..."
gcc -std=c11 -O2 -D_GNU_SOURCE \
    ../src/app_profile/csv_verify.c \
    ../src/app_profile/sign.c \
    -o /tmp/csv_verify_embed -lcrypto
echo "   ✅ $(wc -c < /tmp/csv_verify_embed) bytes"

# 2. Base64 编码
B64=$(python3 -c "import base64; print(base64.b64encode(open('/tmp/csv_verify_embed','rb').read()).decode())")
echo "   ✅ base64: ${#B64} chars"

# 3. 嵌入 plot_gui.py
for TARGET in plot_gui.py plot_profile.py; do
    cp "$TARGET" "$TARGET.bak"
    python3 -c "
src = open('$TARGET').read()
b64 = '$B64'
src = src.replace('REPLACED_BY_BUILD_SCRIPT', b64)
open('$TARGET', 'w').write(src)
"
    echo "   ✅ $TARGET ($(wc -c < "$TARGET") bytes)"
done

rm /tmp/csv_verify_embed
echo ""
echo "🎉 完成！ plot_gui.py + plot_profile.py 已可独立部署。"
echo "   部署时只需拷贝这两个 .py 文件，无需 csv_verify 二进制。"
