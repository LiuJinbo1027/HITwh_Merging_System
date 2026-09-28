#!/usr/bin/env bash
# clang-format 格式检查（CI 之外的本地 gate 工具）
# 用法: scripts/format_check.sh   （检查所有 C++ 文件，未格式化则报错退出）
set -euo pipefail

CLANG_FORMAT="$(command -v clang-format || true)"
if [[ -z "${CLANG_FORMAT}" && -x "${HOME}/.local/bin/clang-format" ]]; then
    CLANG_FORMAT="${HOME}/.local/bin/clang-format"
fi
if [[ -z "${CLANG_FORMAT}" ]]; then
    echo "错误: 未找到 clang-format。请安装（apt install clang-format 或 pip install --user clang-format）" >&2
    exit 2
fi

cd "$(dirname "$0")/.."
# macOS 自带 bash 3.2 无 mapfile，改用 while 循环读入数组（Linux bash 4+ 同样兼容）
FILES=()
while IFS= read -r f; do FILES+=("$f"); done < <(find backend -name '*.h' -o -name '*.cpp' | sort)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "格式检查: 无 C++ 文件，跳过"
    exit 0
fi

UNFORMATTED=0
for f in "${FILES[@]}"; do
    if ! "${CLANG_FORMAT}" --dry-run -Werror "$f" >/dev/null 2>&1; then
        echo "未格式化: $f"
        UNFORMATTED=1
    fi
done

if [[ ${UNFORMATTED} -ne 0 ]]; then
    echo "格式检查失败。运行: ${CLANG_FORMAT} -i <文件> 修复" >&2
    exit 1
fi
echo "格式检查通过 (${#FILES[@]} 个文件, ${CLANG_FORMAT})"
