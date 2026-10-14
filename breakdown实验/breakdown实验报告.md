# Breakdown 实验报告

## 1. 实验目的

long-row 实现已经把旧版随长行数量增长的频繁同步改成单个 OpenMP team 内的批量计算与一次统一 barrier，但主实验中仍慢于 dynamic/guided。本实验通过 breakdown 测量，进一步区分 long-row 调用中的主要额外开销，关注以下部分：

- preprocess；
- ordinary-row computation；
- long-row partial computation；
- barrier wait + synchronization overhead；
- merge。

Parallel region 内普通行循环使用 `nowait`，不同线程可能在不同时间进入 long-row partial computation，各线程阶段也可能相互重叠。因此，本实验保留线程级阶段的 mean 和 max，但不把这些时间直接相加，也不强行换算为总 wall-clock 百分比。

## 2. 实验场景与环境

实验固定使用主报告中的代表场景：

| 项目 | 设置 |
| --- | ---: |
| Skew level | Level 3 |
| Size | medium |
| Rows | 20000 |
| Cols | 100000 |
| Normal NNZ/row | 50 |
| Long rows | 200 |
| Long-row NNZ/row | 4650 |
| Threads | 16 |
| Seed | 20260926 |

OpenMP runtime 环境固定为：

- `OMP_WAIT_POLICY=PASSIVE`
- `OMP_DYNAMIC=FALSE`
- `OMP_PROC_BIND=false`
- `OMP_PLACES` unset

选择该场景是因为 Level 3 的 skew 最强、16T 更容易放大同步和负载不均问题，以及该配置与主报告中的 long-row 负优化分析一致，可以直接解释已有正式结果。

## 3. Breakdown 方法

### 3.1 全局 wall-clock

调用级记录以下指标：

- Original kernel runtime；
- Instrumented kernel runtime；
- Preprocess wall-clock；
- Parallel region wall-clock。

Preprocess 包含扫描 rows、构造 `long_row_ids`，以及分配并清零 `partial_sums`。它位于 parallel region 之前，因此两部分在程序结构上前后串行。实验使用下面的关系进行 sanity check：

```text
Preprocess wall-clock + Parallel region wall-clock
≈ Instrumented total call wall-clock
```

### 3.2 线程级阶段时间

Parallel region 内为每个线程分别记录：

- Ordinary-row work time；
- Long-row partial computation time；
- Barrier wait + synchronization overhead；
- Merge work time。

Barrier 时间严格按以下边界测量：

```text
t_before_barrier
#pragma omp barrier
t_after_barrier

barrier_time = t_after_barrier - t_before_barrier
```

因此，该指标同时包含等待其他线程到达 barrier 的时间和 barrier 自身的同步管理开销。每个进程保留全部16个线程的原始记录，并汇总 mean across threads 和 max across threads。Sum across threads 可从原始数据进一步计算，但它代表累计线程时间，不作为正文核心指标。

## 4. Instrumentation 与稳定性验证

每个设置都使用独立进程，正式计时前先 warm-up。未插桩版本用于检查 instrumentation 是否改变了总体性能；插桩版本依次进行100、500和1000 repeats 的 pilot。所有进程均通过 correctness 检查。

### 4.1 未插桩基线

未插桩版本使用500 repeats 和5个独立进程：

| 指标 | Runtime (ms) |
| --- | ---: |
| Median | 0.637839 |
| Min | 0.607657 |
| Max | 0.639309 |

按 `(max-min)/median` 计算，自然波动为4.96%。

### 4.2 Pilot 与 repeats 选择

为降低 kernel 的计时噪声，插桩版本依次尝试了 100、500 和 1000 repeats，并使用多个独立进程检查稳定性。

100 repeats 已能给出各阶段的大致数量级，但部分线程级 max 指标跨进程波动较明显；增加到 500 repeats 后，全局指标和 long-row partial 已更稳定，但 ordinary max 和 barrier max 仍存在较大波动。

继续增加到 1000 repeats 后，大部分指标的 median 变化已小于 4%，说明单进程内部重复次数基本足够。此时剩余的主要波动来自少数独立进程中 parallel region 和 barrier 时间偏高，更像是系统与调度噪声，而不是 repeats 不足。

因此最终固定 `repeats=1000`，将独立进程数从 5 扩展到 15，并使用 median、Q1、Q3 和 IQR 评价集中趋势与跨进程稳定性。

## 5. 最终正式 breakdown 结果

最终结果使用 `1000 repeats × 15 independent processes`。Q1和Q3分别采用15个排序后进程值中的第4个和第12个。单位均为 ms/call。

| 指标 | Median | Q1 | Q3 | Min | Max | IQR/Median |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Instrumented total | 0.638930 | 0.635274 | 0.656160 | 0.624750 | 0.738023 | 3.27% |
| Preprocess | 0.015559 | 0.015193 | 0.015833 | 0.014675 | 0.016077 | 4.12% |
| Parallel region | 0.623371 | 0.620183 | 0.640083 | 0.610075 | 0.722190 | 3.19% |
| Ordinary mean | 0.103436 | 0.101434 | 0.104683 | 0.100756 | 0.107732 | 3.14% |
| Ordinary max | 0.109281 | 0.107434 | 0.110384 | 0.105960 | 0.112948 | 2.70% |
| Long-row partial mean | 0.108514 | 0.107617 | 0.110039 | 0.106104 | 0.118599 | 2.23% |
| Long-row partial max | 0.113184 | 0.111180 | 0.114973 | 0.109036 | 0.124956 | 3.35% |
| Barrier mean | 0.200867 | 0.198966 | 0.215463 | 0.192080 | 0.261116 | 8.21% |
| Barrier max | 0.223432 | 0.210323 | 0.235401 | 0.202040 | 0.282531 | 11.22% |
| Merge mean | 0.000286479 | 0.000278239 | 0.000305940 | 0.000267557 | 0.000321813 | 9.67% |
| Merge max | 0.000360000 | 0.000335667 | 0.000387908 | 0.000312371 | 0.000432715 | 14.51% |

未插桩 median 为0.637839 ms，插桩 median 为0.638930 ms，instrumentation overhead 为+0.17%，远低于5%的阈值，也低于未插桩版本4.96%的自然波动。

Preprocess 占调用级 wall-clock 的比例 median 为2.40%，Q1–Q3为2.31%–2.45%。Sanity check 的最大绝对误差约为 `1.11×10^-16 ms`，15/15个进程全部通过 correctness 检查。

## 6. 结果分析

Instrumentation 的+0.17%偏移表明计时插桩基本没有造成明显扰动。Preprocess 约占调用级 wall-clock 的2.4%，不是主要成本。Merge mean median 约为0.0003 ms；它的相对波动较大，但绝对时间极小，也不是主要瓶颈。

线程级结果中，ordinary-row 与 long-row partial 的 mean median 分别为0.103436 ms和0.108514 ms，处于相近的0.10–0.11 ms数量级。Barrier mean median 为0.200867 ms，max median 为0.223432 ms，整体高于 ordinary/partial。因此，barrier-related time 是 parallel region 中较显著的额外时间项之一。

Barrier mean/max 的 IQR/Median 为8.21%和11.22%，明显高于 ordinary/partial 的2.23%–3.35%，说明 barrier-related 指标更容易受到线程到达差异和系统调度扰动影响。这里的 barrier 时间同时包含等待其他线程到达和 barrier 自身的同步管理开销，不能进一步拆成两类成本。

这些线程级阶段使用 `nowait` 且可能重叠，因此不能把0.20 ms解释为 parallel region wall-clock 的某个百分比，也不能据此认定 barrier 是唯一瓶颈。当前结果支持的结论是：long-row 负优化主要不是 preprocess 或 merge 造成的；更值得关注的是 parallel region 内部的 ordinary/partial 工作与 barrier-related waiting/synchronization，其中 barrier-related 时间最显著。

## 7. 与主实验的关系

本 breakdown 用于解释主报告中已经观察到的 long-row 负优化。回答 long-row 解决了单条长行不可拆但仍没有超过 dynamic/guided 的原因。

结果表明，行内拆分同时引入了 partial computation、线程到达差异和同步等待等额外成本；在当前 workload 下，这些成本没有被负载均衡收益覆盖。该结论只用于解释当前 Level 3 / medium / 16T 代表场景，不外推到其他矩阵或实现。
