# OpenMP CSR SpMV 实验一

## 当前目录结构

```text
.
├─ csr_spmv_lab.cpp
├─ run_main.sh
├─ analyze.py
├─ 实验方案.md
├─ 实验报告.md
├─ 心路历程.md
├─ image.png
├─ image/
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

## 核心文件

- `csr_spmv_lab.cpp`：主程序，包含 serial、starter `std::thread`、OpenMP runtime scheduling 和 long-row splitting。
- `run_main.sh`：主实验运行脚本。
- `analyze.py`：从主实验 summary 生成 main overview 的分析脚本。
- `实验方案.md`：主实验、规模验证和 seed 验证的参数设计。
- `实验报告.md`：最终实验分析。
- `breakdown实验/`：long-row 测量版本、已有数据、实验设计和补充报告。

## 编译

```bash
g++ -O2 -std=c++17 -fopenmp -pthread csr_spmv_lab.cpp -o csr_spmv_lab
```

## 主实验

在16个可见CPU的WSL环境中运行：

```bash
bash run_main.sh
```

脚本固定使用以下OpenMP运行时设置，并执行主实验的 dynamic、guided、chunk、Level 和线程数组合：

```text
OMP_WAIT_POLICY=PASSIVE
OMP_DYNAMIC=FALSE
OMP_PROC_BIND=false
OMP_PLACES unset
```

`run_main.sh`生成 `results/main/raw.csv` 和 `results/main/summary.csv`。随后可运行：

```bash
python3 analyze.py
```

生成 `results/main/overview.csv`。

## Breakdown脚本

`breakdown实验/run_breakdown_pilot.sh`固定使用 Level 3、medium、16线程、seed 20260926及与正式实验相同的OpenMP运行时设置。脚本总是创建新CSV；目标路径已存在时会拒绝覆盖。

分别运行已有数据中的四组设置：

```bash
bash breakdown实验/run_breakdown_pilot.sh original-500x5
bash breakdown实验/run_breakdown_pilot.sh instrumented-100x5
bash breakdown实验/run_breakdown_pilot.sh instrumented-500x5
bash breakdown实验/run_breakdown_pilot.sh instrumented-1000x15
```

按当前 `breakdown_pilot.csv` 的顺序运行全部四组：

```bash
bash breakdown实验/run_breakdown_pilot.sh all
```

各模式都有不会覆盖现有 `breakdown_pilot.csv` 的默认输出文件名。也可以把第二个参数指定为新的输出路径，例如：

```bash
bash breakdown实验/run_breakdown_pilot.sh instrumented-1000x15 /tmp/breakdown_1000x15.csv
```

## 结果文件说明

- `raw.csv`：进程级原始测量记录。
- `summary.csv`：按逻辑配置整理的统计汇总。
- `overview.csv`：面向人工检查和报告整理的概览。

`results/main/raw.csv` 与 `results/main/summary.csv` 由 `run_main.sh` 生成，`results/main/overview.csv` 由 `analyze.py` 生成。
