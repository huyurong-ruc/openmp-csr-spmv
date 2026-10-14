# Breakdown 实验设计

## 实验目的

针对 long-row 负优化案例，分析最终实现中的主要额外开销，包括预处理、普通行计算、long-row partial computation、同步等待和 merge等，进一步解释 long-row 为什么没有超过 dynamic/guided。

## 代表场景

固定使用以下场景：

- Level 3
- medium
- 16T
- seed=20260926
- 与正式实验相同的 OpenMP runtime 环境

选择此场景是因为 Level 3 的 skew 最强，16T 更容易放大同步和负载不均衡问题，同时也与实验报告中的 long-row 负优化分析保持一致。

## 测量层次

计时分成两个层次。

### 1. 全局 wall-clock

直接测量两个调用级时间：

- Preprocess wall-clock
  - 扫描 rows
  - 构造 long_row_ids
  - 分配/清零 partial_sums
- 整个 OpenMP parallel region wall-clock

用来观察预处理和并行区域各自的总体开销。

全局层面最终保留以下指标：

- Original kernel runtime
- Instrumented kernel runtime
- Preprocess wall-clock
- Parallel region wall-clock

### 2. Parallel region 内部的线程级阶段时间

每个线程分别累计：

- Ordinary-row work time
- Long-row partial computation time
- Barrier wait + synchronization overhead
- Merge work time

Barrier 部分由每个线程分别记录：

```text
t_before_barrier
#pragma omp barrier
t_after_barrier

barrier_time = t_after_barrier - t_before_barrier
```

这个时间同时包含线程等待其他线程到达 barrier 的时间和 barrier 自身的同步管理开销。

Ordinary-row computation 使用 nowait，不同线程可能在不同时间进入 long-row partial computation，所以这些阶段不是严格串行的全局阶段。

每项线程级时间统计：

- mean across threads
- max across threads

由于并行区域的完成时间通常更容易受到最慢线程影响，所以重点看 max across threads。Sum across threads 表示累计线程时间，可保留在原始结果中作为辅助信息，但不作为正文核心指标。逐线程原始数据也保留，便于检查异常线程；min 不作为正文主要指标。

## 阶段划分

为了避免拆得太细、计时器开销占比过多，所以只拆成四块：

### 1. Preprocess

- 扫描 rows
- 构造 long_row_ids
- 分配/清零 partial_sums

### 2. Ordinary-row computation

- 普通行计算

### 3. Long-row partial computation + synchronization

- partial sums
- Barrier等待与同步开销

### 4. Merge

- long-row merge

Preprocess 使用全局 wall-clock。Parallel region 内的后三块使用线程级计时，不把它们强行解释成严格串行的全局阶段。

## 计时扰动控制

考虑到 kernel 总时间不到 1 ms，插入计时点本身可能改变程序性能。因此 breakdown 同时保留两组总 kernel runtime：

1. 未插桩原始版本的总 kernel runtime；
2. 插桩版本的总 kernel runtime。

用于检查 instrumentation overhead。先观察未插桩版本5个独立进程本身的自然波动，再比较插桩版本与未插桩版本的 median 偏移。

本实验暂时采用5%作为判断阈值：如果插桩版总 runtime 的 median 相对未插桩版偏移超过5%，并且该偏移明显大于未插桩版本5次运行的自然波动，就认为 instrumentation 对性能产生了不可忽略的扰动。如果未插桩数据本身的波动已经接近或超过5%，就不能机械使用该阈值，需要结合实际波动判断。

如果插桩造成的扰动不可忽略，阶段计时只作为定性或辅助结果，不用于给出精确的百分比结论。

重复次数通过 pilot 确定。先用较少次数做 pilot，从100次开始，再尝试500次。如果100次和500次得到的阶段统计已经较稳定，就停止增加；如果500次仍有明显波动，再考虑1000次。

最终运行5个独立进程取 median。正式计时前仍单独进行 warm-up。

## Sanity check

正式分析前检查：

1. 插桩版本的总 runtime 相比原始版本是否有明显偏移；
2. 各线程级阶段统计是否稳定，是否出现异常波动。

Preprocess 位于 parallel region 之前，两者在程序结构上前后串行，因此可以检查：

```text
Preprocess wall-clock + Parallel region wall-clock
≈ Instrumented total call wall-clock
```

用于检查全局计时是否基本自洽。

## 最终关注的问题

- Preprocess 是否占明显成本？
- Ordinary-row 计算是否存在明显的线程间不均衡？
- Long-row partial computation 本身是否昂贵？
- Barrier等待与同步开销是否明显？
- Merge 是否构成显著额外开销？
- 哪一类成本最能解释 long-row 最终慢于 dynamic/guided？