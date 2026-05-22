# SLB 控制信息 Polar 编码器

本仓库实现了 T/XS 10001-2025 中 SLB 控制信息传输的信道编码流程，
主要对应 `6.2.6.1.5 物理层控制信息传输`，并实现其引用的
`6.2.6.1.2` 到 `6.2.6.1.4` 中的 Polar 编码、速率匹配、信道交织和码块级联流程。

项目使用 C++17 编写，采用 CMake 构建，包含源码、测试、设计文档、测试报告，以及表 C.1 的可靠度排序序列抽取脚本。

English README: [README.md](README.md)

## 已实现范围

模块边界从码块分段之后开始。也就是说，输入已经是每个码块的
`c_r(0)..c_r(K_r-1)` 比特序列。

每个控制信息码块会依次执行：

1. 控制信息 Polar 参数推导；
2. 按母码长度从表 C.1 可靠度序列中筛选当前 `Q_N`；
3. 信息位、冻结位、PC 位选择；
4. Polar 编码变换；
5. 按 `rvid` 进行速率匹配；
6. 使用 PDF 中 3 组固定 `Iseq` 做信道交织；
7. 多码块级联，输出最终 `g_k`。

主 PDF 在 `6.2.6.1.6.1` 标题处结束，因此第一类/第二类数据信息传输的后续规则不在本项目范围内。

## 目录结构

```text
.
├── CMakeLists.txt
├── README.md
├── README.zh-CN.md
├── include/slb/control_polar_encoder.hpp
├── src/control_polar_encoder.cpp
├── src/polar_reliability_sequence.cpp
├── tests/test_control_polar_encoder.cpp
├── tools/extract_c1_reliability.py
├── docs/control_info_encoder_design.md
├── docs/control_info_encoder_test_report.md
├── docs/table_c1_extraction_report.md
├── SLB技术要求和测试方法.pdf
└── 表 C.1 极化码的可靠度排序序列.pdf
```

## 构建要求

- CMake 3.16 或更高版本
- 支持 C++17 的编译器
- Poppler 的 `pdftotext`，仅在重新生成表 C.1 时需要

编码库本身没有第三方运行时依赖。

## 构建与测试

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/control_polar_encoder_tests
```

期望结果：

```text
100% tests passed, 0 tests failed
Executed 21 tests successfully.
```

## 主要 API

头文件：

```cpp
#include "slb/control_polar_encoder.hpp"
```

典型用法：

```cpp
#include "slb/control_polar_encoder.hpp"

#include <cstdint>
#include <vector>

int main() {
    using slb::control::Bit;
    using slb::control::ControlBlockInput;

    std::vector<ControlBlockInput> blocks = {
        {
            std::vector<Bit>{1, 0, 1, 1, 0, 0, 1, 0, 1, 0},
            64,  // E0：首次传输速率匹配后的长度
            62,  // E：当前传输速率匹配后的长度
            0,   // rvid
        },
    };

    auto config = slb::control::makeStandardEncoderConfig();
    auto result = slb::control::encodeControlInfo(blocks, config);

    // result.bits 是最终级联后的 g_k 输出。
    return static_cast<int>(result.bits.empty());
}
```

主要入口：

```cpp
slb::control::EncoderConfig slb::control::makeStandardEncoderConfig();

slb::control::EncodeResult slb::control::encodeControlInfo(
    const std::vector<slb::control::ControlBlockInput>& blocks,
    const slb::control::EncoderConfig& config);
```

`makeStandardEncoderConfig()` 使用内置的表 C.1 标准可靠度排序序列。
如果需要专用制式或定向测试，也可以通过 `makeEncoderConfig(...)` 注入自定义可靠度序列。

## 输入参数

每个 `ControlBlockInput` 包含：

| 字段 | 含义 | 合法值 |
|---|---|---|
| `bits` | 码块比特 `c_r` | 非空，元素只能是 `0` 或 `1` |
| `firstTransmissionLength` | `E0`，首次传输速率匹配后的长度 | 正整数 |
| `transmissionLength` | `E`，当前传输速率匹配后的长度 | 正整数，不超过 `62 * 128` |
| `redundancyVersion` | `rvid` | `0`、`1`、`2` 或 `3` |

调用 `encodeControlInfo` 前，应已完成码块分段。

## 输出

`EncodeResult` 包含：

| 字段 | 含义 |
|---|---|
| `bits` | 最终级联输出 `g_k`，等于 `f_0 || f_1 || ... || f_{C-1}` |
| `blocks` | 每个码块的调试和一致性检查信息 |

每个码块的调试信息包括：

- `K`、`E0`、`E`、`rvid`；
- 推导出的 `nPC`、`nPCwm`、`K+nPC`、`n`、`N`；
- `T`、临时冻结位、信息位、冻结位、PC 位；
- 速率匹配的 `M`、`k0`、源索引；
- 中间序列 `u`、`d`、`e`、`f_r`。

这些中间量便于与外部工具、设备日志或标准向量逐级对拍。

## 控制信息规则

控制信息传输部分使用：

```text
nmax = 10

if K_r < 18 or K_r > 25:
    nPC = 0
else:
    nPC = 3
```

当 `18 <= K_r <= 25` 时：

```text
if E_r - K_r + 3 > 192:
    nPCwm = 1
else:
    nPCwm = 0
```

实现中使用了等价的安全比较，避免无符号整数下溢。

## 表 C.1 可靠度序列

`src/polar_reliability_sequence.cpp` 由以下 PDF 自动生成：

```text
表 C.1 极化码的可靠度排序序列.pdf
```

重新生成命令：

```sh
python3 tools/extract_c1_reliability.py \
  "表 C.1 极化码的可靠度排序序列.pdf" \
  --cpp-out src/polar_reliability_sequence.cpp \
  --report-out docs/table_c1_extraction_report.md
```

脚本同时解析 `pdftotext -raw` 和 `pdftotext -layout` 两种输出。
只有两条路径完全一致，并满足以下条件时才生成源码：

- 1024 行；
- 8192 个 `(W, Q)` 对；
- `W` 完整覆盖 `0..8191`；
- `Q` 完整覆盖 `0..8191`；
- `Q` 无重复。

当前抽取哨兵值：

```text
前 16 个 Q：
0, 1, 2, 4, 8, 16, 32, 3, 5, 64, 9, 6, 17, 10, 18, 128

后 16 个 Q：
8179, 8181, 7935, 8182, 8185, 8063, 8186, 8183,
8188, 8187, 8175, 8127, 8190, 8191, 8159, 8189
```

单元测试会再次检查生成后的表。

## 参数校验

非法输入会抛出 `std::invalid_argument`，包括：

- 空码块列表；
- 空码块；
- 输入比特不是 `0` 或 `1`；
- `E0 == 0` 或 `E == 0`；
- 非法 `rvid`；
- `K+nPC > N`；
- 当前 `N` 下可靠度序列缺失或重复；
- 交织序列不是合法排列；
- `E > Col * Rowmax`。

## 测试覆盖

当前共有 21 个测试用例，覆盖：

- 固定交织序列排列合法性；
- 母码长度和 PC 参数推导；
- 表 C.1 完整性和哨兵值；
- Polar 变换小向量和 GF(2) 线性性质；
- 信息位、冻结位、PC 位选择；
- 打孔和缩短分支；
- 速率匹配 `M`、`k0` 和源索引；
- 信道交织行/bank 输出顺序；
- 单码块、多码块端到端 golden 输出；
- 标准表 C.1 下的端到端 golden 输出；
- 异常输入校验；
- 最大交织容量；
- 780 个合法参数组合的确定性性质测试。

完整测试报告见：

```text
docs/control_info_encoder_test_report.md
```

## 商业集成建议

- 标准 SLB 场景使用 `makeStandardEncoderConfig()`。
- 集成初期保留 `EncodeResult::blocks`，方便逐级对拍。
- 最终认证建议使用标准方或设备侧官方 golden vector 进行外部一致性验证。
- 不要手工编辑 `src/polar_reliability_sequence.cpp`，应通过抽取脚本生成。
- 如果后续标准修订表 C.1，应重新运行抽取脚本，并保留新的抽取报告。
