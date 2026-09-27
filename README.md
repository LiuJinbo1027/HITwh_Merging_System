# 机场拼车匹配工具

哈尔滨工业大学（威海）计算机课程设计 · 双人项目

解决去机场拼车不方便的问题：拼车者提交人数、出发时间段、性别与性别偏好，后端将时间窗有交集的拼车者组合成团（每车最多 4 人），目标是"把车拼满"。

- 前端：Vue 3 + Vite 管理台（匹配池 / 成团 / 事件流实时可视化，支持虚拟乘客流演示）
- 后端：C++17 + CMake + cpp-httplib + nlohmann/json（数据结构与算法为核心得分点）
- 文档：接口契约见 [docs/contract.md](docs/contract.md)

## 快速开始

```bash
# 后端（需 cmake ≥ 3.16，g++ 支持 C++17）
cmake -S . -B build && cmake --build build
./build/backend/merging_server          # 监听 127.0.0.1:8080

# 前端（需 node ≥ 18）
cd frontend && npm install && npm run dev   # 监听 5173，/api 代理到 8080
```

## 开发流程

- 分支：`a/<step>-<名>`（后端/算法）与 `b/<step>-<名>`（前端/文档），PR 合并，禁止直接推 main
- 提交信息：`type(scope): summary`（feat/fix/test/docs/refactor/chore）
- 格式：`scripts/format_check.sh` 通过后才能合并
- 接口变更：先改 `docs/contract.md`（唯一事实源），双人确认后同 PR 提交

## 目录结构

见 [docs/contract.md](docs/contract.md) 与课程设计文档。
