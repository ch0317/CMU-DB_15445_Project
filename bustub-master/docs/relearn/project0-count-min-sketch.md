# Project 0：从数组计数到并发 Count-Min Sketch

先读 [总入口](README.md) 的工作树与构建说明。本篇从最普通的计数器开始，最后做出可以移动、合并并支持并发插入的数据结构。

官方范围：[Fall 2025 P0](https://15445.courses.cs.cmu.edu/fall2025/project0/) 是 Count-Min Sketch，包含构造、移动、插入、估计、清空、合并与 Top-K；插入需要并发安全和性能验证。下面的实现推演主要来自你的接口、提交和本地测试。

## 1. 当前代码与阅读入口

| 文件 / 提交 | 你应当看什么 |
| --- | --- |
| [count_min_sketch.h](../../src/include/primer/count_min_sketch.h) | 类接口、维度、种子、哈希 lambda |
| [count_min_sketch.cpp](../../src/primer/count_min_sketch.cpp) | 当前基本实现、显式实例化 |
| [count_min_sketch_test.cpp](../../test/primer/count_min_sketch_test.cpp) | 具体结果、MoveTest、并发与竞争测试 |
| `b257c80` → `0ff25b5` | CMS 实现与格式整理 |

现有实现适合解释基础算法，但重做应修正两点：每次 Insert 锁住整个对象，无法从结构上获得真正并行累加；移动直接移动哈希函数，而哈希函数仍捕获旧 `this`。

## 2. 第一步：理解为什么不直接用 map

准确统计可以写 `map[key]++`，但需要给每个不同 key 保留条目。CMS 固定占用 `depth × width` 个计数器，输入再多也不会新增 key 条目，代价是结果可能偏大。

把二维表想成 `depth` 行独立的计数系统。每行用自己的哈希把 key 分到某一列：

```text
Insert(x): 每行找到一格，各加 1
Count(x): 每行找到同样的一格，取这些计数中的最小值
```

冲突会把别人的次数加到同一个桶里，所以在非负累加且未溢出的条件下，估计不会低于真实值。最小值降低冲突造成的额外计数。它不是“准确查出 key 是否存在”的结构，也不能单独枚举出现过哪些 key。

**手算练习：** 自己假设宽 4、深 3，A 的位置为 `[0,2,1]`，B 的位置为 `[0,1,3]`。依次插入 A、B、A，写出整个表，再查询 A / B。A 的候选计数应是 `[3,2,2]`，返回 2；B 返回 1。这些位置是教学假设，不是代码真实哈希结果。

**验收：** 不看资料说明为什么取 min，而不是平均值或 max。

## 3. 第二步：明确接口与存储

先写下你的设计：

```text
width_：每行列数，必须 > 0
depth_：行数，必须 > 0
hash_functions_[r](x)：返回 [0, width_) 的列号
counters：depth_ * width_ 个计数器，初始全部 0
```

起步可用 `vector<vector<uint32_t>>` 完成单线程逻辑；最终并发版可以用固定长度的原子计数器数组。扁平数组下标是 `r * width_ + col`。计算分配长度时用 `size_t`，先检查乘法与分配可行性，避免窄整数先溢出。

不要添加保存所有 key 的集合来实现 Count；那会破坏固定空间的目的。TopK 接口已经提供候选集合。

模板定义在 `.cpp` 中时，使用测试所需的类型必须被显式实例化。当前文件底部有 string、int64_t、int 的实例。增加新类型却遇到 undefined reference，先检查这里，而不是怀疑哈希算法。

## 4. 第三步：构造与哈希

实现次序：

1. 在分配内存前检查宽度、深度非零，失败抛 `invalid_argument`。
2. 存储宽度 / 深度并把所有计数器初始化为零。
3. `reserve(depth_)`，按行号调用骨架提供的 `HashFunction(i)`。
4. 不另造哈希公式、不更改骨架种子；用当前模板提供的函数。

你仓库中的种子常量带 `@spring2026` 注释，但本篇按该本地接口实施；不要为了匹配网页年份而改掉种子。

**自查：** 为什么每行必须使用不同 seed？如果所有行完全一样，增加 depth 还会提高抗冲突能力吗？

**验收：** 两种零维度异常、普通构造、全新对象 Count 返回 0；width=1 时所有输入都冲突，所有 key 的估计都等于总插入次数。

## 5. 第四步：先写单线程 Insert / Count

算法伪代码：

```text
Insert(item):
    for row in [0, depth):
        col = hash_functions[row](item)
        counters[row, col] += 1

Count(item):
    answer = 最大 uint32_t
    for row in [0, depth):
        col = hash_functions[row](item)
        answer = min(answer, counters[row, col])
    return answer
```

不要在 Count 中重新推导一个不同的哈希公式。Count 必须和 Insert 命中完全相同的格子。返回类型是 uint32_t，初始化最小值归约时要使用同类型上界，并明确计数溢出不在上述“不会低估”的前提内。

**验收：** `BasicTest1`、`BasicTest2`、`EdgeTest1`、`EdgeTest2`。小宽度可能发生冲突，不要随意把“估计必须精确”写成所有场景的断言。

## 6. 第五步：Clear 与 Merge

Clear 遍历所有格子设为 0，不改维度和哈希规则。Merge 是**将 other 加到当前对象**，不是返回一个新对象；以本地函数签名为准。

```text
Merge(other):
    若 width / depth 不同：抛异常，当前对象不应先被改一半
    对每个同位置格子：this.counter += other.counter
```

相同维度只是这里的兼容性检查；之所以能相加，是两个对象使用同一套按行生成的哈希。若随意给每个对象用随机种子，就算尺寸相同也不能正确合并。

**手算：** 一个 sketch 插入 A 三次，另一个插入 A 两次，合并后相应计数器增加到五次。还要想一想自合并的定义；当前 API 可以理解为自身计数翻倍，别让实现为了同时锁两把相同 mutex 死锁。

并发合同只要求并发 Insert。可以明确要求 Clear / Merge / 移动在没有并发操作时进行。不要因为给 Merge 加了当前对象的锁，就误以为它也保护了正在被其他线程修改的 other。

**验收：** `ClearTest`、`MergeTest`；清空后可以重新插入；尺寸不兼容时抛异常且保留原状态。

## 7. 第六步：TopK

输入是候选 key 列表，输出是 `(key, estimated_count)`，按估计次数降序，最多 k 个。

先实现最直观的版本：对每个候选调用 Count，排序，再截断到 `min(k, candidates.size())`。这是你现有实现的思路，复杂度是 `O(C × depth + C log C)`，C 为候选数量。

之后可做长度为 k 的最小堆，复杂度 `O(C × depth + C log k)`。先确保结果正确再优化。

**需明确的边界：** k=0、空候选、k 大于候选数、相同估计值。并列顺序与候选重复的处理以本地测试 / API 约定为准；不要自行引入字典序或去重规则改变期望结果。

**验收：** 三个 TopK 测试。你应能说明为什么 CMS 无法在没有 candidates 的情况下列出 Top-K。

## 8. 第七步：正确实现移动语义

“移动资源”是把数组等所有权交给新对象；mutex 不可移动，目标对象保留自己的 mutex。最隐蔽的部分是 `HashFunction` 返回的 lambda：

```cpp
return [seed, this](const KeyType &item) { /* 使用 width_ */ };
```

这个 lambda 保存的是创建它时的对象地址。`std::move(hash_functions_)` 只转移函数对象，不会把其中的地址改成新对象。

现有 MoveTest 的源对象在查询时仍活着，源 width 也没改变，因此这种错误可能暂时不暴露。源对象销毁或重新赋值为另一尺寸后，目标可能读取已失效地址，或算出与目标数组宽度不匹配的下标。

**重做可采用的具体方案：** 保留骨架禁止修改区；另外增加一个 `const` helper，按骨架完全相同的 seeded HashUtil 公式，直接计算目标对象的桶号。Insert / Count 统一使用这个 helper，避免调用绑定旧地址的函数对象：

```cpp
auto BucketFor(size_t row, const KeyType &item) const -> size_t {
  const auto h1 = std::hash<KeyType>{}(item);
  const auto h2 = HashUtil::CombineHashes(row, SEED_BASE);
  return HashUtil::CombineHashes(h1, h2) % width_;
}
```

这不是另外选择哈希算法，而是消除骨架同一公式对 lambda 对象地址的依赖。若使用的评分模板要求所有调用都必须经其 HashFunction，先复核那个模板的具体约束，再采用相应的重绑定设计；本地接口允许新增辅助函数。

移动构造只复制维度、移动计数器所有权；移动赋值先检查 `this != &other`，然后转移同样的资源。目标的 mutex 留在目标；不需要把 hash_functions_ 中捕获旧 this 的 callable 转过去。将源对象维度置零，并约定其只可析构或重新赋值，不对它继续 Count / Insert。正常构造仍可以按骨架初始化已有哈希成员，最终算法统一调用 BucketFor。

这样移动路径不需要重新创建 lambda 或分配内存，符合 `noexcept`。不要在 `noexcept` 移动函数里调用会分配 vector / std::function 的重建逻辑而忽略失败语义。完成最终原子存储版时，移动的是计数器数组 unique_ptr；移动前也必须没有并发 Insert。

**关键验收：** 自移动赋值应安全；目标保留原统计；源对象销毁后目标仍可查询；源重新赋值为不同宽度后目标仍正确。新增小程序或测试比只运行现有 MoveTest 更有意义。

## 9. 第八步：并发 Insert，从一把锁走向原子累加

两个线程都执行 `counter++`，本质是“读、加、写”。若两者都读到 7，就都写回 8，正确答案 9 被丢掉了。全局 mutex 能避免丢失，但把所有 Insert 排成一列。

重做建议使用每格原子累加：

```text
counter.fetch_add(1, memory_order_relaxed)
Count 时使用 counter.load(memory_order_relaxed)
```

这里 relaxed 保证单格计数原子化；它不表示一次 Insert 的所有行更新在某瞬间一起可见，也不表示同时发生的 Count 能获得一致的全表快照。按本项目并发 Insert 的合同，线程全部 join 后读取最终结果即可。

C++17 的 atomic 不是普通可复制值。不要直接依赖 `vector<atomic<T>>::resize` 会像整数 vector 一样工作。可以用固定长度 `unique_ptr<atomic<uint32_t>[]>`，构造后逐格 `store(0)`，移动转移 unique_ptr。也可使用能满足构造约束的其他布局。

保持 width / depth / 哈希函数在并发 Insert 时不变；Insert 只修改原子计数器。Clear / Merge 逐格 load / store 或 fetch_add，但仍不承诺可以与其他操作随意并发。

**验收：** `ParallelTest`、`ComplexParallelTest`，以及单独运行 `ContentionRatioTest`。性能受机器负载影响，单次比值不稳定；不能因一次碰巧超过阈值就证明全局锁设计正确。

## 10. 建议执行顺序

```bash
cmake --build build --target count_min_sketch_test -j4
./build/test/count_min_sketch_test --gtest_filter='CountMinSketchTest.Basic*:CountMinSketchTest.Edge*'
./build/test/count_min_sketch_test --gtest_filter='CountMinSketchTest.ClearTest:CountMinSketchTest.MergeTest:CountMinSketchTest.TopK*'
./build/test/count_min_sketch_test --gtest_filter='CountMinSketchTest.MoveTest'
./build/test/count_min_sketch_test --gtest_filter='CountMinSketchTest.*ParallelTest'
./build/test/count_min_sketch_test --gtest_filter='CountMinSketchTest.ContentionRatioTest'
```

失败定位：普通计数错先查下标 / 种子；仅 Merge 错查兼容性 / 对应位置；仅移动后错查 lambda 捕获；仅并发少计数查非原子更新；只有性能失败查锁粒度和测试环境。

## 11. 额外练习：你的 SkipList 提交

文件：[skiplist.h](../../src/include/primer/skiplist.h)、[skiplist.cpp](../../src/primer/skiplist.cpp)、[skiplist_test.cpp](../../test/primer/skiplist_test.cpp)。这不计入 Fall 2025 P0 必做项，可在 CMS 之后练。

1. 先实现只有第 0 层的有序链表，明确 header 不存用户 key。
2. 查找从最高层向右移动，不能继续就下降；每层保存最后一个小于目标的前驱。
3. 插入先检查重复，生成高度，再逐层接入 `prev → new → old_next`。
4. 删除找到前驱，逐层绕过目标，并在顶层空时降低有效高度。
5. Clear 释放节点，重置层数和计数；按当前接口决定读写锁粒度。
6. 固定种子或记录生成高度调试，避免“偶尔才错”的随机结构无法复现。

执行 `cmake --build build --target skiplist_test -j4`，再运行 `./build/test/skiplist_test --gtest_also_run_disabled_tests`。能手绘同一组 key 在不同层的链接，再看 `387b5ba` / `8ef6673` 对照实现。

## 12. 合上文档自测

- CMS 为什么可能高估？什么时候“不会低估”的结论失效？
- Merge 为什么要求相同哈希规则？
- 为什么 std::move 不会修正 lambda 的 this？
- 原子格子为什么不等于原子整次 Insert？
- 为什么 TopK 必须有 candidates？
- 如果不用任何完成版源码，你能独立写出 Insert、Count 和移动后生命周期的验证吗？
