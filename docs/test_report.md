# 拼车匹配工具 · 测试报告

> 逐阶段累积验收证据。约定：每条 AC 记录「验收标准 / 命令 / 预期 / 实测 / 证据」。

## G0（P0 环境脚手架）验收记录

| AC | 内容 | 结果 | 证据 |
| --- | --- | --- | --- |
| G0-1 | cmake 构建退出码 0 | 通过 | `cmake -S . -B build && cmake --build build` → exit 0，产物 `build/backend/merging_server` |
| G0-2 | `/api/health` 返回统一信封 code=0 | 通过 | `curl http://127.0.0.1:8080/api/health` → `{"code":0,"data":{"uptime_ms":...},"message":"ok"}` |
| G0-3 | 前端经 Vite 代理访问后端 | 通过 | `curl http://127.0.0.1:5173/api/health` → `{"code":0,...}`；页面 `<title>机场拼车匹配工具</title>` |
| G0-4 | clang-format 可用且 format_check 通过 | 通过 | `scripts/format_check.sh` → "格式检查通过 (3 个文件)"（clang-format 23.1.1，pip 安装于 ~/.local/bin） |
| G0-5 | a/p0-* 与 b/p0-* 分支经 PR 流程合入 main | 通过（本地模拟） | 合并提交 3d758d1（a/p0-scaffold）、a34143b（b/p0-frontend）；远端 push 待用户授权 |

## G1~G4 验收记录

（待各阶段完成后追加）
