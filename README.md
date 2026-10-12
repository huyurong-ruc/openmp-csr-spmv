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
│  └─ result.md
├─ 实验方案.md
└─ README.md
```
早期AI生成的候选实现和非最终测量已归档至 `archive/`，仅用于记录实验过程。

归档代码在实验阶段只把当前方法写进 `spmv_omp_student_1`，`spmv_omp_student_2` 保留串行占位。

- 方法1：`schedule(dynamic, 16)`
- 方法2：`schedule(guided, 16)`
- 方法3：把超长行拆成多段，在行内并行求和
- 方法4：根据累计nnz给线程划分连续行区间

归档的前期记录见 [archive/result.md](archive/result.md)，其中的性能数据和策略结论均不用于正式实验。

## 当前实验阶段

当前实验方案已经冻结，详细配置见 [实验方案.md](实验方案.md)。后续所有正式性能数据在当前本机 WSL 环境中重新运行获得。

当前主要探索 `dynamic`、`guided`、`long-row` 三种策略；`static` 和 `nnz-aware` 作为已有参考，不是本次主要探索对象。

实验分为三个阶段：

- 主实验：比较不同策略、偏斜程度和线程数，并对 `dynamic` / `guided` 检查不同 chunk。
- 规模验证：在 small、medium、large 规模下检查性能趋势。
- seed 鲁棒性验证：在代表性场景中检查策略排名和趋势是否稳定。