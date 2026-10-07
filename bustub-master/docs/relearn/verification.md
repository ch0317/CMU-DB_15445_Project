# 验证记录与当前代码边界

记录日期：2026-10-07。源码快照：`ef110d3196f09937318c89216fac11a75e1d9047`。本次只新增学习文档；下面发现的实现问题没有替你修改。

## 1. 本次做了哪些验证

读取五个课程页面、本地接口与实现、Git 主线 / 学习分支提交以及测试。使用现有 `build` 目录重新构建以下相关目标后执行测试，构建配置为 Debug，GCC，`BUSTUB_SANITIZER=address`。ASan 错误属于本次实际输出；未单独进行完整的 TSan / 所有内存泄漏路径验证。

```bash
cmake --build build --target count_min_sketch_test arc_replacer_test b_plus_tree_tombstone_test txn_mvcc_regression_test sqllogictest -j4
cmake --build build --target disk_scheduler_test page_guard_test buffer_pool_manager_test txn_timestamp_test txn_scan_test txn_executor_test txn_index_test txn_index_concurrent_test txn_abort_serializable_test -j4
```

测试分别执行，单元测试中的 disabled 案例用 `--gtest_also_run_disabled_tests` 启用。P0 性能案例独立运行以避免与其他测试争用 CPU。测试子进程设置了 45 秒超时；下面记录没有超时。

## 2. 实际结果

| 目标 / 案例 | 本次结果 | 能说明什么 |
| --- | --- | --- |
| CMS 除竞争性能外的全部案例 | 12/12 通过 | 现有公开功能输入通过；不覆盖源对象销毁后的移动捕获问题 |
| CMS ContentionRatioTest | 失败，speedup≈0.9973，要求 >1.2 | 本次运行未达到并发性能阈值，当前 Insert 仍用对象级 mutex |
| arc_replacer_test | 3/3 通过 | 这三个公开序列正确，不能排除未覆盖的 B2 比例分支遗漏 |
| disk_scheduler_test | 1/1 通过 | 公开调度案例通过 |
| page_guard_test | 2/2 通过 | 公开所有权案例通过，不能排除特定线程交错 |
| buffer_pool_manager_test | 7/7 通过 | 公开缓冲池案例通过，不能替代锁交接证明 |
| b_plus_tree_tombstone_test | 失败，未跑完 4 个案例 | BasicTest 删除后重新插入时预期 tomb 清空 / 新 RID 恢复失败；随后测试对空结果访问引发 ASan SEGV |
| p3.16-sort-limit.slt | 失败 | 执行到文件第 68 行附近的默认 ASC NULL 排序查询出现 wrong result；与当前默认 NULL 位置代码差异一致 |
| p3.20-window-function.slt | 失败 | WindowFunctionExecutor 抛 not implemented |
| txn_timestamp_test | 2/2 通过 | 时间戳 / watermark 公开案例通过 |
| txn_scan_test | 3/3 通过 | 版本重建、日志收集、快照扫描公开案例通过 |
| txn_executor_test | 11/11 通过 | MVCC 写入、undo、冲突、GC 的公开案例通过 |
| txn_index_test | 5/5 通过 | 公开主键、复用和更新案例通过 |
| txn_index_concurrent_test | 3/3 通过 | 公开并发索引案例通过 |
| txn_abort_serializable_test | 4/4 通过 | 公开回滚与可串行化案例通过 |
| txn_mvcc_regression_test | 6/6 通过 | 你补充的六个回归案例通过 |

P4 总计 **28 个公开事务案例 + 6 个补充案例通过**。这不是 Gradescope 结果，不代表任意查询路径、隐藏案例或长期压力测试都正确。P4 能通过这些案例与 P2 tombstone 失败并不矛盾：默认树通常 `NumTombs=0`，P4 又保留主键索引映射，不经过同一套物理 tomb 删除路径。

P2 的 ASan 崩溃发生在测试的 `rids[0].GetSlotNum()`；它是前面重插没有恢复记录、返回空 vector 后的后果。不能仅凭这一条栈把原因解释成 RID 类自身损坏。

## 3. 可以复跑的命令

```bash
./build/test/count_min_sketch_test --gtest_filter='-CountMinSketchTest.ContentionRatioTest'
./build/test/count_min_sketch_test --gtest_filter='CountMinSketchTest.ContentionRatioTest'
./build/test/arc_replacer_test --gtest_also_run_disabled_tests
./build/test/disk_scheduler_test --gtest_also_run_disabled_tests
./build/test/page_guard_test --gtest_also_run_disabled_tests
./build/test/buffer_pool_manager_test --gtest_also_run_disabled_tests
./build/test/b_plus_tree_tombstone_test --gtest_also_run_disabled_tests
./build/bin/bustub-sqllogictest test/sql/p3.16-sort-limit.slt --in-memory
./build/bin/bustub-sqllogictest test/sql/p3.20-window-function.slt --in-memory
./build/test/txn_timestamp_test --gtest_also_run_disabled_tests
./build/test/txn_scan_test --gtest_also_run_disabled_tests
./build/test/txn_executor_test --gtest_also_run_disabled_tests
./build/test/txn_index_test --gtest_also_run_disabled_tests
./build/test/txn_index_concurrent_test --gtest_also_run_disabled_tests
./build/test/txn_abort_serializable_test --gtest_also_run_disabled_tests
./build/test/txn_mvcc_regression_test --gtest_also_run_disabled_tests
```

先编译再复跑。性能比值受负载影响；一次测试运行是证据，不是对机器性能的恒定预测。

## 4. 静态审查发现，尚未单独动态复现

这些是教学重点与应补测试的场景，区别于上一节实际失败：

| 位置 | 根据源码发现的边界 | 指导位置 |
| --- | --- | --- |
| CMS 移动 | HashFunction 捕获 this，移动函数 vector 仍指原对象 | P0 第七步 |
| ARC RecordAccess | MFU ghost 命中且 B1 大于 B2 时未按比例降 p | P1 第四步 |
| PageGuard Drop | unpin 在 BPM mutex 外，与 replacer / frame 复用之间存在交接窗口 | P1 第八步 |
| BPM FlushPage | 持 BPM mutex 等页写 latch，可能与持页者请求其他页构成死锁 | P1 第十一步 |
| ReadPageGuard Flush | 共享页锁下更新普通 dirty bool，需要另外同步 | P1 第十一步 |
| B+ 树叶迁移 | 借位 / 合并没有完整同步 tomb 下标和删除顺序 | P2 第十二步 |
| InternalPage 右借 | 来源 ptr[0] 未推进，留下旧孩子指针 | P2 第十步 |
| UpdateAncestorMinKey | 消耗仍被 Remove 依赖的 write_set 路径，调用时含叶页 | P2 第九步 |
| Begin / iterator 构造 | 同线程对同一叶重复获取读 latch | P2 第十三步 |
| 乐观 insert / remove | 释放整条读路径后仅锁旧叶，未完整验证路由与安全性 | P2 第十四步 |
| HashJoin | 全右侧存在内存，缺少 Grace 分区存储 | P3 第十三步 |
| 外部排序 | 全输入 std::sort，中间页 / run 迭代器未实现 | P3 第十二、十五至十七步 |
| NestedIndexJoin | inner 直接读最新 tuple，不经过 MVCC 重建 | P3 第九步 / P4 适用范围 |

## 5. 本次没有做的检查

没有运行所有 P2 插入 / 删除 / 压力目标、P3 全套 SQL、排行榜、长期并发压力、Release 全套、clang-tidy / 格式检查，也没有重新提交到 Gradescope。没有修改源码去验证手册中的替代设计。历史 P4_STUDY 提到过基准和 lint 成功，本次不把历史记录重标为新验证。

文档交付前检查了相对文件链接、代码围栏配对和 Git diff 的空白错误；阅读命令与目标名按本地 CMake / Git 核对。shell 示例中的路径基于当前仓库目录结构。

## 6. 课程与本地依据

| 项目 | 官方页面 | 本地主要证据 |
| --- | --- | --- |
| P0 | [C++ Primer](https://15445.courses.cs.cmu.edu/fall2025/project0/) | CMS 的 header / cpp / 13 个测试及 `b257c80`、`0ff25b5` |
| P1 | [Buffer Pool Manager](https://15445.courses.cs.cmu.edu/fall2025/project1/) | ARC、scheduler、BPM、guard 和对应测试 |
| P2 | [Database Index](https://15445.courses.cs.cmu.edu/fall2025/project2/) | 叶 / 内部页、树与 iterator，tombstone 测试 |
| P3 | [Query Execution](https://15445.courses.cs.cmu.edu/fall2025/project3/) | P3 学习分支、executor / optimizer、SQLLogicTest、中间页骨架 |
| P4 | [Concurrency Control](https://15445.courses.cs.cmu.edu/fall2025/project4/) | 四个 P4 分支、事务实现、28 个公开案例与 6 个补充案例 |

课程引用用于确定任务范围与版本差异；函数分解、状态演算和代码风险来自你仓库的实现审查。正文不是官方题面翻译，也不是某份官方答案。
