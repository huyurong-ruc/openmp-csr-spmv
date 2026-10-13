初始测试中16线程出现异常性能下降。进一步复测发现，该现象并非算法本身导致，而与OpenMP运行时等待策略高度相关。在当前WSL/libgomp环境下，显式设置OMP_WAIT_POLICY=PASSIVE后，代表性dynamic配置从约5.5 ms降至约0.39 ms。因此后续实验统一固定该运行时设置，以避免线程等待行为成为混杂变量。


一开始只控制了算法参数和矩阵参数，却没有把 OMP_WAIT_POLICY 这种 OpenMP runtime 参数 也当成实验环境的一部分固定下来。


更具体地说，当时我们控制了：
threads、chunk、skew、size、seed、编译参数

但漏了类似：
OMP_WAIT_POLICY、OMP_PROC_BIND、OMP_PLACES、OMP_DYNAMIC这些 OpenMP runtime 条件。
后来又因为 OMP_DISPLAY_ENV 显示了 PASSIVE，一度错误地把：“默认显示PASSIVE”理解成“不显式设置时和 OMP_WAIT_POLICY=PASSIVE 完全等价”。

而实测证明当前环境下二者性能行为根本不等价。

所以现在应该修正为：
export OMP_WAIT_POLICY=PASSIVE
export OMP_DYNAMIC=FALSE