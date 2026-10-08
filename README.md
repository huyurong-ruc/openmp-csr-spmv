# OpenMP CSR SpMV 实验一

## 文件

```text
experiments/
├─ 1_dynamic.cpp
├─ 2_guided.cpp
├─ 3_hybrid.cpp
├─ 4_nnz_aware.cpp
```

实验阶段只把当前方法写进 `spmv_omp_student_1`，`spmv_omp_student_2` 保留串行占位。

- 方法1：`schedule(dynamic, 16)`
- 方法2：`schedule(guided, 16)`
- 方法3：把超长行拆成多段，在行内并行求和
- 方法4：根据累计nnz给线程划分连续行区间

前期测到的效果统一写在 [result.md](result.md)用来筛选方向，最终数据在确定代码和环境后重新跑。

## PowerShell运行方法

先进入任意一个方法文件夹，然后粘贴下面这一行：

```powershell
$src=(Get-ChildItem *.cpp | Select-Object -First 1).FullName; $exe=Join-Path $env:TEMP 'csr_spmv_test.exe'; g++ -O2 -std=c++17 -fopenmp -pthread "$src" -o "$exe"; if ($LASTEXITCODE -eq 0) { & "$exe" --threads 8 --repeats 10 }; if (Test-Path "$exe") { Remove-Item "$exe" -Force }
```

这条命令会完成编译和运行。临时exe放在Windows临时目录。

看到下面两行说明OpenMP和正确性检查都正常：

```text
OpenMP enabled, _OPENMP=...
All correctness checks passed.
```

测试其他线程数时，只改命令末尾的 `--threads`。本机可以测试1、2、4、8、16线程。