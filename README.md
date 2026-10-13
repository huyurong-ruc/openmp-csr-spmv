# OpenMP CSR SpMV 实验一

## 主要文件

```text
.
├─ archive/
│  ├─ experiments/
│  │  ├─ 0_static_default.cpp
│  │  ├─ 1_dynamic.cpp
│  │  ├─ 2_guided.cpp
│  │  ├─ 3_hybrid.cpp
│  │  └─ 4_nnz_aware.cpp
│  ├─ 中间产物/doc/
│  └─ result.md
├─ results/
│  ├─ main/
│  │  ├─ raw.csv
│  │  ├─ summary.csv
│  │  └─ overview.csv
│  ├─ size/
│  │  ├─ raw.csv
│  │  ├─ summary.csv
│  │  └─ overview.csv
│  ├─ seed/
│  │  ├─ raw.csv
│  │  ├─ summary.csv
│  │  └─ overview.csv
│  └─ perf/
│     ├─ environment.txt
│     ├─ serial.txt
│     ├─ thread.txt
│     ├─ dynamic.txt
│     ├─ long_row.txt
│     └─ summary.csv
├─ csr_spmv_lab.cpp
├─ run_main.sh
├─ analyze.py
├─ 实验方案.md
├─ 实验报告.md
└─ README.md
```

早期生成的候选实现和非最终测量已归档至 `archive/`，仅用于记录实验过程。

归档候选代码包括：

- 方法1：`schedule(dynamic, 16)`
- 方法2：`schedule(guided, 16)`
- 方法3：把超长行拆成多段，在行内并行求和
- 方法4：根据累计nnz给线程划分连续行区间

归档的前期记录见 [archive/result.md](archive/result.md)，其中的性能数据和策略结论均不用于正式实验。

## 当前实验状态

实验方案已经冻结，详细配置见 [实验方案.md](实验方案.md)。主实验、规模验证、seed 鲁棒性验证和 Linux `perf stat` 分析均已完成；所有正式性能数据均在当前本机 WSL 环境中实际运行获得。

当前主要探索 `dynamic`、`guided`、`long-row` 三种策略；`static` 和 `nnz-aware` 作为已有参考，不是本次主要探索对象。

正式结果统一保存在 `results/`：

- `results/main/`：主实验原始记录、统计汇总和15行 overview。
- `results/size/`：small、medium、large 规模验证结果。
- `results/seed/`：三个随机 seed 的鲁棒性验证结果。
- `results/perf/`：运行环境、四个单策略的原始 `perf stat` 输出和汇总。

其中 `raw.csv` 保存进程级正式记录，`summary.csv` 保存各逻辑配置的 median / mean / min / max，`overview.csv` 是用于人工检查和报告整理的机械汇总，不包含新的实验测量。

最终实验结论见 [实验报告.md](实验报告.md)。
