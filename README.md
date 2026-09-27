# 机场拼车匹配工具

[![CI](https://github.com/LiuJinbo1027/HITwh_Merging_System/actions/workflows/ci.yml/badge.svg)](https://github.com/LiuJinbo1027/HITwh_Merging_System/actions/workflows/ci.yml)

哈尔滨工业大学（威海）计算机课程设计 · 双人项目

解决去机场拼车不方便的问题：拼车者提交人数、出发时间段、性别与性别偏好，后端将时间窗有交集的拼车者组合成团（每车最多 4 人），目标是"把车拼满"。

- 前端：Vue 3 + Vite 管理台（匹配池 / 成团 / 事件流实时可视化，支持虚拟乘客流演示）
- 后端：C++17 + CMake + cpp-httplib + nlohmann/json（数据结构与算法为核心得分点）
- 文档：接口契约见 [docs/contract.md](docs/contract.md)（机器可读镜像 [docs/contract.yaml](docs/contract.yaml)）

## 双人协作入口

两份开发手册（分发给对应成员，按手册分阶段推进）：

- [docs/manual_A.md](docs/manual_A.md) —— A：后端与算法（匹配引擎、HTTP 接口、状态机、P4 优化器）
- [docs/manual_B.md](docs/manual_B.md) —— B：前端与文档（管理台 GUI、联调脚本、任务书/测试/结题报告）

手册含环境准备、协作纪律、代码地图、P0-P4 分阶段任务与逐项验收标准（AC）。

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
- CI：GitHub Actions 对每次 PR 自动跑「构建 + ctest + 格式检查 + 前端构建」，全绿才能合并（P2 起追加 smoke 端到端 job）；本地提交前先跑相同命令自查
- 格式：`scripts/format_check.sh` 通过后才能合并
- 接口变更：先改 `docs/contract.md`（唯一事实源），双人确认后同 PR 提交

## 目录结构

见 [docs/contract.md](docs/contract.md) 与课程设计文档。
