# Seed 鲁棒性报告

本报告记录已完成的 seed robustness validation，用于检查主实验中的策略排名和主要趋势是否依赖单一随机矩阵实例。本文是 seed 验证阶段材料，不是最终课程实验结论。

## 1. 验证目的

正式主实验使用 `seed = 20260926`。为检查结论是否依赖该随机矩阵实例，本阶段新增 `seed = 12345` 和 `seed = 54321`，并与原正式数据进行比较。

实验固定 medium size 和16线程。Dynamic 与 guided 继续采用主实验中各 Level、16线程条件下最低 median 对应的 chunk，不重新扫描或调整参数。比较内容为三个 seed 下 dynamic、guided 和 long-row 的相对排名。

## 2. 固定实验配置

Medium size 固定为：

- `rows = 20000`
- `cols = 100000`
- `long_rows = 200`
- `threads = 16`

三个 Level 的行长度参数如下：

| Level | normal_nnz | long_nnz |
| ---: | ---: | ---: |
| 1 | 90 | 690 |
| 2 | 75 | 2175 |
| 3 | 50 | 4650 |

Dynamic 和 guided 的固定 chunk 如下：

| Level | Dynamic chunk | Guided chunk |
| ---: | ---: | ---: |
| 1 | 16 | 8 |
| 2 | 32 | 2 |
| 3 | 64 | 4 |

这些 chunk 是主实验16线程下观测到的最低 median 配置，不代表其他线程数或矩阵场景中的全局最优值。

本阶段比较的 seed 为：

- `20260926`
- `12345`
- `54321`

## 3. 测量方法

Seed robustness validation 包含 `3 Levels × 3 strategies × 3 seeds = 27` 个逻辑配置。每个配置包含5个独立进程的 process-level runtime，共135条 raw records。

- `seed = 20260926` 的9个逻辑配置及45条 raw records直接复用正式主实验数据。
- `seed = 12345` 和 `seed = 54321` 的18个逻辑配置重新运行，共产生90条新记录。
- 每个进程先执行1次 warm-up，再以 `repeats=10` 完成正式计时。
- Process-level runtime 为10次正式调用总时间除以10。
- 每个逻辑配置以5个 process-level runtime 的 median 作为主结果，同时保留 mean、min 和 max。
- 所有135条记录的 correctness 均为 `pass`。
- 27个逻辑配置中均未出现 `max/min > 5` 的明显波动。

## 4. 核心结果

下表列出9个 `Level × seed` 场景下三种策略的 median，单位为 ms。

| Level | Seed | Dynamic | Guided | Long-row | Best |
| ---: | ---: | ---: | ---: | ---: | --- |
| 1 | 20260926 | 0.457 | 0.459 | 0.687 | dynamic |
| 1 | 12345 | 0.480 | 0.462 | 0.777 | guided |
| 1 | 54321 | 0.467 | 0.481 | 0.767 | dynamic |
| 2 | 20260926 | 0.472 | 0.511 | 0.771 | dynamic |
| 2 | 12345 | 0.484 | 0.457 | 0.830 | guided |
| 2 | 54321 | 0.443 | 0.560 | 0.928 | dynamic |
| 3 | 20260926 | 0.417 | 0.464 | 0.937 | dynamic |
| 3 | 12345 | 0.411 | 0.453 | 0.830 | dynamic |
| 3 | 54321 | 0.480 | 0.438 | 0.993 | guided |

## 5. 观察

### Level 1

Dynamic 和 guided 的结果整体较接近。`seed = 20260926` 时 dynamic 领先约0.44%，`seed = 12345` 时 guided 领先约3.90%，`seed = 54321` 时 dynamic 领先约3.00%。三个 seed 中 dynamic 两次领先、guided 一次领先，策略排名会随 seed 改变。

### Level 2

三个 seed 中 dynamic 两次领先、guided 一次领先。`seed = 20260926` 时 dynamic 领先约8.26%，`seed = 12345` 时 guided 领先约5.91%，`seed = 54321` 时 dynamic 领先约26.41%。相对排名和策略间差距均随 seed 变化，当前数据表明该 Level 的比较结果对随机矩阵实例较敏感。

### Level 3

`seed = 20260926` 和 `seed = 12345` 中 dynamic 分别领先约11.27%和10.22%。`seed = 54321` 中 guided 反超 dynamic，领先约9.59%。因此，在当前高 skew 场景中，主实验观察到的 dynamic 优势没有对所有 seed 保持成立。

### Long-row

Long-row 在9个 `Level × seed` 场景中均未取得第一，该结果在三个 seed 下保持一致。但这一观察仅适用于当前 synthetic workload、固定规模、固定线程数和当前实现，不能泛化为 long-row 方法在所有矩阵上无效。

## 6. 阶段结论

1. Dynamic 与 guided 的相对排名存在 seed 敏感性。
2. 单一 seed 不足以支持“某一策略稳定最优”的强结论。
3. Level 2 和 Level 3 均观察到 dynamic 与 guided 的排名翻转。
4. Long-row 未取得第一的结果在当前三个 seed 下相对稳定，但不能推广到其他矩阵场景。
5. Seed robustness 进一步支持“策略选择具有场景依赖性”。
