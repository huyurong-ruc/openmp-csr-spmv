# OpenMP CSR SpMV 实验一

本仓库实现并比较 CSR SpMV 的 OpenMP runtime scheduling、long-row splitting 与 `std::thread` 基线，并保存主实验、规模验证、随机种子验证、perf 和 breakdown 结果。

## 目录结构

```text
.
├─ csr_spmv_lab.cpp
├─ run_main.sh
├─ analyze.py
├─ doc/
│  ├─ 实验方案.md
│  ├─ 实验报告.md
│  ├─ 实验报告.pdf
│  ├─ 心路历程.md
│  └─ 心路历程.pdf
├─ image/
│  ├─ correctness-check.png
│  ├─ ai-parameter-selection.png
│  ├─ ai-experiment-plan.png
│  ├─ image1.png
│  ├─ image2.png
│  └─ image3.png
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
├─ breakdown实验/
│  ├─ csr_spmv_breakdown.cpp
│  ├─ run_breakdown_pilot.sh
│  ├─ breakdown_pilot.csv
│  ├─ breakdown实验设计.md
│  └─ breakdown实验报告.md
└─ README.md
```

## 文档

- [实验方案](doc/实验方案.md)
- [实验报告（Markdown）](doc/实验报告.md) / [实验报告（PDF）](doc/实验报告.pdf)
- [心路历程（Markdown）](doc/心路历程.md) / [心路历程（PDF）](doc/心路历程.pdf)
- [Breakdown 实验设计](breakdown实验/breakdown实验设计.md)
- [Breakdown 补充报告](breakdown实验/breakdown实验报告.md)

## 编译

```bash
g++ -O2 -std=c++17 -fopenmp -pthread csr_spmv_lab.cpp -o csr_spmv_lab
```

## 主实验复现

`run_main.sh` 要求 WSL 中可见 16 个 CPU，并固定以下 OpenMP 运行时环境：

```text
OMP_WAIT_POLICY=PASSIVE
OMP_DYNAMIC=FALSE
OMP_PROC_BIND=false
OMP_PLACES unset
```

运行主实验：

```bash
bash run_main.sh
```

脚本生成 `results/main/raw.csv` 和 `results/main/summary.csv`。随后运行：

```bash
python3 analyze.py
```

生成 `results/main/overview.csv`。

## Breakdown 复现

Breakdown 固定使用 Level 3、medium、16线程、seed 20260926及与主实验相同的 OpenMP 环境。脚本拒绝覆盖已存在的目标文件。

```bash
bash breakdown实验/run_breakdown_pilot.sh original-500x5
bash breakdown实验/run_breakdown_pilot.sh instrumented-100x5
bash breakdown实验/run_breakdown_pilot.sh instrumented-500x5
bash breakdown实验/run_breakdown_pilot.sh instrumented-1000x15
```

运行全部四组并写入新的默认结果文件：

```bash
bash breakdown实验/run_breakdown_pilot.sh all
```

也可以将第二个参数指定为新的输出路径。