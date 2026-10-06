# Project 4 学习路线

完整实现最终位于 `main`。四个学习分支按任务累积，每个后续分支包含之前的任务：

| 分支 | 内容 | 推荐入口 |
| --- | --- | --- |
| `p4-solution-task1` | read/commit timestamp、watermark | `watermark.cpp`、`TransactionManager::Begin/Commit` |
| `p4-solution-task2` | UndoLog 重建、快照顺序扫描、调试输出 | `ReconstructTuple`、`CollectUndoLogs`、`SeqScanExecutor` |
| `p4-solution-task3` | 插入、原地更新/删除、undo 合并、提交、回滚、GC | `GenerateNewUndoLog`、`GenerateUpdatedUndoLog`、`ModifyTuple` |
| `p4-solution-task4` | 主键唯一性、删除位置复用、索引快照、主键更新、可串行化验证 | `InsertTupleMvcc`、`IndexScanExecutor`、`TransactionManager::VerifyTxn` |

使用 `git switch p4-solution-task2` 阅读某阶段；使用 `git diff p4-solution-task2..p4-solution-task3 -- src` 查看下一任务的改动。上述命令在 `bustub-master` 目录运行。最后用 `git switch main` 返回完整实现。

## 核心不变量

- 表堆始终存最新版本。已提交版本使用 commit timestamp；未提交版本使用 transaction ID 对应的临时时间戳。
- 一个事务对一个已有 RID 最多维护一条 undo log。再次修改时只增加记录的字段，不丢掉事务开始前的原值。新插入的 RID 不需要 undo log。
- watermark 使用哈希计数加有序集合，维护重复 read timestamp，更新时间为 O(log N)。事务管理器的 map 锁保护 watermark 的访问。
- `GetTupleAndUndoLink` 在表页读锁下读取一致的 tuple、meta 和 undo link。`ModifyTuple` 在同一个表页写锁内检查冲突、合并 undo、更新 tuple 和 link，避免先检查再写入的竞争。
- Abort 使用移除自身 undo 节点的方案：恢复原 tuple 和时间戳，再把头指向前一个 undo。新插入的 tuple 变为 `ts=0` 的删除标记；索引项始终保留。
- 主键索引中的 key 始终指向同一个 RID。插入已删除 key 时复用该 RID；存在其他事务写入或比 read timestamp 更新的提交时，设置 TAINTED 并抛出 ExecutionException。
- UPDATE 先耗尽子执行器并保存结果，避免更新过程中再次读到自己修改的行。主键变化时先删除所有旧 key，再插入新 key，支持整体平移主键。
- 可串行化事务记录扫描谓词。提交时检查 read timestamp 之后提交的写集合，逐版本检查更新前后是否命中谓词，跳过未提交的版本。只读事务直接提交。
- GC 保留 watermark 读取所需的日志，也保留仍运行的可串行化事务校验所需的近期提交写集合。否则无 undo 的插入可能被错误回收，遗漏幻读。

## 依赖与边界

原有 Project 3 的 ORDER BY 和 LIMIT 执行器未实现，Project 4 的基本回滚测试使用 ORDER BY，两个基准的结果展示使用 LIMIT。本次补齐排序比较器、批量排序输出和 LIMIT，当前排序将输入存入内存；没有实现 Project 3 的外部归并排序页和磁盘溢出。这不影响 Project 4 的事务协议。

实现了四个必需 task。排行榜相关的在线 undo 回收、精细谓词锁和并行验证没有额外优化；垃圾回收使用题目要求的 stop-the-world 接口。

## 验证

公开事务测试默认带 `DISABLED_` 前缀，运行时必须显式启用：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target txn_timestamp_test txn_scan_test txn_executor_test txn_index_test txn_index_concurrent_test txn_abort_serializable_test txn_mvcc_regression_test -j4
for name in txn_timestamp_test txn_scan_test txn_executor_test txn_index_test txn_index_concurrent_test txn_abort_serializable_test txn_mvcc_regression_test; do
  ./build/test/$name --gtest_also_run_disabled_tests || break
done
cmake --build build --target check-lint check-clang-tidy-p4
cmake --build build --target submit-p4
```

补充测试覆盖插入/删除幻读、更新前后谓词命中、值恢复到原值、中间提交版本、insert-delete 无净效果、未提交版本跳过、GC 后索引幻读验证和主键移动的回滚。

已验证：28 个公开事务测试、6 个补充回归测试全部通过；Debug 构建开启 AddressSanitizer，测试在支持 LeakSanitizer 的环境运行。两个 Terrier 基准以 `--duration 3000 --threads 4 --terriers 100` 完整运行，最终 token 总量校验通过。这是短时正确性验证，不代表官方排行榜成绩。
