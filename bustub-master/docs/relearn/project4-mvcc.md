# Project 4：沿时间线实现 MVCC

先读 [总入口](README.md)，完成 P1 / P2 和 P3 基础执行器。本项目把“表里的一行”变成“同一 RID 在不同时间的多个版本”。第一遍请多画时间线，少急着写大函数。

官方范围：[Fall 2025 P4](https://15445.courses.cs.cmu.edu/fall2025/project4/) 是乐观多版本并发控制，包含时间戳、版本重建与扫描、MVCC 写操作、主键索引及可串行化验证。下面按照你的四个学习分支组织，细节来自本地实现和回归测试。

## 1. 对照你的四个阶段

| 阶段 | 提交 / 分支 | 本篇步骤 |
| --- | --- | --- |
| Task 1 | `908f553` / `p4-solution-task1` | 时间戳、watermark |
| Task 2 | `80a8ac3` / `p4-solution-task2` | UndoLog、重建、收集、快照扫描 |
| Task 3 | `d96c1d5` / `p4-solution-task3` | 写入、合并 undo、Commit、Abort、GC |
| Task 4 | `ef110d3` / `p4-solution-task4` | 主键、索引可见性、主键更新、验证 |

原 [P4_STUDY.md](../../P4_STUDY.md) 是较短的历史学习记录，本篇把其中关键不变量展开成可重做的步骤。

入口文件：

- [transaction.h](../../src/include/concurrency/transaction.h)：事务状态、undo 和 write set。
- [transaction_manager.h](../../src/include/concurrency/transaction_manager.h)、[实现](../../src/concurrency/transaction_manager.cpp)：Begin / Commit / Abort / GC / Verify。
- [watermark.h](../../src/include/concurrency/watermark.h)、[watermark.cpp](../../src/concurrency/watermark.cpp)。
- [execution_common.cpp](../../src/execution/execution_common.cpp)：重建、收集、undo 生成和通用写入。
- `seq_scan_executor.cpp`、`index_scan_executor.cpp`、三个写执行器。
- [transaction_manager_impl.cpp](../../src/concurrency/transaction_manager_impl.cpp)：已有辅助 API；文件注明不可修改且不进入评分包，应阅读并调用。
- [txn_mvcc_regression_test.cpp](../../test/txn/txn_mvcc_regression_test.cpp)：你的六个补充回归测试。

P4 采用固定长度表数据及原地修改协议。P3 的旧 SQL 测试可能不再适用，尤其变长更新和非主键索引行为，不要混合两版实现。

## 2. 第一步：先说清楚三个存储位置

```text
TableHeap：RID → 最新完整 tuple + TupleMeta {ts, is_deleted}
TransactionManager：RID → 最新 UndoLink
各 Transaction：自己的 undo_logs 数组、write set、scan predicates
```

UndoLink 是 `(txn_id,log_index)`，不是指向堆上 UndoLog 的裸指针。UndoLog 保存从当前版本退回上一版本所需的信息，链条向过去走。

例子：

```text
表堆最新：value=30, ts=8
head → log1 {old value=20, ts=5}
        prev → log2 {old value=10, ts=2}
```

read_ts=6 的事务应读到 20，read_ts=3 读到 10。表里删掉某行不意味着旧事务再也不能看到它，删除也是一个版本状态。

**验收：** 不看代码说明 undo 为什么记录旧值而不是新值；说明 log.ts 是被恢复版本的时间戳，而不是创建日志的事务提交时间。

## 3. 第二步：实现 Begin 与时间戳

区分三种数：

| 数值 | 含义 |
| --- | --- |
| transaction id | 唯一事务身份；本地从 `TXN_START_ID=1LL<<62` 起 |
| read_ts | 事务开始时的快照边界 |
| commit_ts | 成功提交后分配的小范围递增时间戳 |

`GetTransactionTempTs()` 在你的实现中直接返回 txn_id。未提交的 tuple 用这个临时时间戳标识所有者，已提交 tuple 用 commit_ts。

Begin 在 txn_map_mutex 下分配身份、存入 map、把 read_ts 设为 last_commit_ts，然后向 watermark 注册。开始事务不会增加 last_commit_ts；两个同时开始的事务可以有相同 read_ts。

此时先写最小 Commit：串行分配 commit_ts，设置事务状态与 read_ts 登记；等有 write set 后再扩充逐行发布逻辑。不能把 transaction id 用作 commit_ts，否则可见性比较完全变形。

## 4. 第三步：Watermark

watermark 是运行事务中的最小 read_ts；没有运行事务时等于最新 commit_ts。

你采用 `unordered_map<timestamp,count>` 加 `set<timestamp>`：计数处理重复 read_ts，有序集合维护最小值。

```text
Add(ts): count[ts]++；set.insert(ts)；watermark=set.begin
Remove(ts): count--；为零才从 map 和 set 删；重算最小值或 commit_ts
UpdateCommitTs(ts): 更新无运行事务时的基准
```

手算：运行 `[3,3,5]`，watermark=3；移除一个 3 仍为 3；移除第二个变 5；移除 5 后回到最新提交边界。

不要每次更新都扫全部事务，性能测试会检查规模扩展。普通 map / set 不线程安全；你的 Watermark 本身没独立 mutex，外部事务管理器锁负责保护它，重做必须保持这个合同。

**验收：** `txn_timestamp_test`，特别是重复时间戳和性能案例。

## 5. 第四步：先只实现 ReconstructTuple

这个函数只做“执行给定 undo”，**不选择时间戳**，不去事务管理器继续取日志。

输入：完整 schema、base tuple、base meta、从新到旧排列的日志列表。输出 optional<Tuple>，最终版本 deleted 则 nullopt。

算法：

1. 从 base tuple 读全部列值，保存 base 的 deleted 状态。
2. 对每条 log，根据 modified_fields 构造只含被修改列的 undo schema。
3. 用两个下标：完整列下标 i、紧凑 undo 列下标 j。true 位才读取 undo[j] 覆盖 values[i]。
4. deleted 更新为 log.is_deleted，继续处理后续日志。
5. 全部日志应用结束，再按最终 deleted 决定是否返回 tuple。

例子：schema `[a,b,c]`，base `[9,8,7]`；mask `[true,false,true]`，undo tuple `[1,3]`，结果 `[1,8,3]`。不能把 undo[2] 当 c，因为 undo 只有两列。

删除后又恢复的链不能在遇到 deleted=true 时提前结束；后续更旧日志可能恢复为有效版本。NULL 是真实值，不等于“这一列没记入日志”。

**验收：** `DISABLED_TupleReconstructTest`；无日志、稀疏列、连续多日志、删除 / 复活、NULL。

## 6. 第五步：CollectUndoLogs

它负责“选择足够退到快照的日志”，与重建函数分开。

```text
若 base.ts <= read_ts：返回一个存在但为空的日志 vector
若 base.ts == 自己临时时间戳：同样返回空 vector
否则：
    依次取 head 日志并加入 vector
    若 log.ts <= read_ts：返回 vector
    否则沿 prev_version 继续
走到链尾仍没找到版本：返回 nullopt
```

两个返回值区别很重要：

- 空 vector：base 就是可见版本，交给重建处理删除状态。
- nullopt：快照时还不存在这条记录，根本不输出。

**手算：** 最新 30/ts8，日志 20/ts5、10/ts2。read_ts=6 收一条；read_ts=3 收两条；read_ts=1 到尾仍不可见。若最新属于自己，无须退回旧版本；属于别人未提交，就沿 undo 找历史。

**验收：** `DISABLED_CollectUndoLogTest`。不要因为 base 当前 deleted 就直接认为过去也 deleted。

## 7. 第六步：快照 SeqScan

替换 P3 的删除标记直接过滤：

```text
取得 RID
GetTupleAndUndoLink → 同一观察点的 meta / base / head
CollectUndoLogs → 需要的日志
ReconstructTuple → 可见 tuple
对可见 tuple 求过滤条件
输出 tuple 与原 RID
```

先重建，再过滤。例如最新 balance=100，快照 balance=20，WHERE balance<50 应命中。若先看最新值过滤就会漏行。

GetTupleAndUndoLink 在表页读锁下读取 meta / tuple / head 的一致组合；不要把它拆成 GetTuple 再 GetUndoLink，期间 writer 可修改版本造成混搭。

Reconstruct 返回新 Tuple 时要补上原 RID。Init 重新建迭代器；为后面的 serializable 任务记录 scan predicate。

**验收：** `DISABLED_ScanTest`；同一事务重复读、别人提交后旧快照不变、自己的写能读、未提交新插入对别人不可见。

## 8. 第七步：GenerateNewUndoLog

第一次修改已有 RID，记录它在事务修改之前的状态。

| 修改 | undo 如何保存 |
| --- | --- |
| 旧有效 → 新有效 | 只记改变列的旧值，is_deleted=false |
| 旧有效 → 删除 | 保存全部旧列，is_deleted=false |
| 旧删除 → 复活 | is_deleted=true，可不存旧列值 |
| 真正新建 RID | 事务中新插入，通常无 undo |

日志 ts 取 base meta.ts；prev_version 取原 head。字段比较要能正确比较 NULL，比如使用 CompareExactlyEquals；普通 SQL CompareEquals 会得到 UNKNOWN，不能简单视为未改变。

**验收：** `DISABLED_GenerateUndoLogTest`；只改变一列、全部不变、NULL↔非 NULL、删除 / 复活。

## 9. 第八步：同一事务重复修改时合并 undo

核心不变量：一个事务对一个已有 RID **最多一条 undo**，它必须一直能退回该事务第一次修改之前。

例子：原行 `(a=10,b=20)`，第一次改 a=11，undo `{a:10}`；第二次改 b=21，变 `{a:10,b:20}`；第三次改 a=10，undo 仍保留 `{a:10,b:20}`，不能因新 a 恢复原值就抹掉已经保存的旧 a。

GenerateUpdatedUndoLog：

1. 已经记过的列，保留原日志中的最初旧值。
2. 本次新改变且以前没记录的列，加入当前修改前的旧值。
3. mask 只增不减，ts 与 prev_version 不变。
4. 以前记录的是 deleted 状态时，保留该 deleted undo 的语义。

**验收：** 变化后恢复、更新→删除、删除→复活、事务中新插入→多次修改。不要 AppendUndoLog 每次新增一条把自身串成无限版本。

## 10. 第九步：写冲突与原子 ModifyTuple

合法写入前提：最新版本属于自己；或最新已提交且 `ts<=read_ts`。以下情况冲突：别人未提交的版本，或比自己快照更新的已提交版本。

当前代码的组合检查可表达为：

```text
if meta.ts != my_temp_ts && meta.ts > my_read_ts:
    taint + throw ExecutionException
```

临时时间戳很大，因此涵盖其他未提交者。复用 deleted RID 时还要要求当前 deleted=true，否则是重复主键。

**不能先解锁做冲突检查再重新写。** 两个 writer 可能同时通过旧检查。你当前 ModifyTuple 的重要设计是持表页 write guard 完成以下整体过程：

1. 读取当前 meta / tuple，检查冲突和 deleted 要求。
2. 取得当前 undo head。
3. 自己已写过且 head 指向自己：合并自己的日志。
4. 初次改已有版本：AppendUndoLog 并建立新 head。
5. 原地更新 tuple / meta 为自己 temp ts。
6. 更新 undo link，登记 write set。
7. 离开作用域释放页锁。

跨 meta / tuple / link 的原子性来自共同表页锁，不是几个成员各自使用 mutex 就自动拥有。保持与读取一致的锁顺序，不能持 version mutex 再回头拿表页锁。

事务中新插入且无 undo 的 RID，重复改写不需要突然生成一个自己的“旧版本”；其他事务在它提交前应始终看不到该记录。

## 11. 第十步：三个 MVCC 写执行器

Insert：先不带主键逻辑，写 `{my_temp_ts,false}`，将 RID 加 write set，不为真正新 RID 生成 undo，输出 count 一次。

Update：先耗尽 child，把 `(rid,target_tuple)` 保存；所有目标表达式基于 child 的旧可见 tuple；再调用 ModifyTuple。固定长度支持原地更新，不能继续采用 P3 每次分配新 RID 的方式。

Delete：调用 ModifyTuple(target=nullptr)，meta.deleted=true；tuple 字节保留有效底稿，供 undo 重建与回滚使用。不要直接调用 B+ 树 Remove 删索引项。

遇到写冲突时设 TAINTED 并抛异常。TAINTED 不是 ABORTED；之前成功写入的数据仍在，必须由 Abort 清理。错误发生在写集合登记之前时，要确保任何已经产生的物理修改也能被回滚。

**验收：** `txn_executor_test` 的插入、提交、更新、删除、冲突案例；检查 write set 和 undo 数量，不只看 SQL 最终值。

## 12. 第十一步：Commit 的发布顺序

现在扩充最初的 Commit。你的实现用 commit_mutex 串行化提交，避免两个事务得到同一个 commit_ts，也为后面的验证建立稳定提交顺序。

```text
拿 commit mutex
检查 RUNNING；若 SERIALIZABLE，先验证
candidate_ts = last_commit_ts + 1
遍历 write set：把属于自己的 temp ts 替换为 candidate_ts
全部表条目完成后：
    设置 txn.commit_ts / COMMITTED
    发布 last_commit_ts
    更新 watermark 的 commit 基准
    移除该事务的 read_ts 登记
```

不要先提高 last_commit_ts，再慢慢把 write set 改完。否则新 Begin 获得新快照，却可能只看到本次提交的一半。

更新表 meta 时保留 deleted 状态。Commit 不需要删除 undo；旧快照可能仍需要它。write set 中 RID 去重，否则同一行多次处理 / 回滚可能出错。

**验收：** 两事务提交单调递增、旧读事务仍读旧值、新事务读新值、自身多次更新只发布一次。

## 13. 第十二步：Abort

Abort 同时支持 RUNNING 和 TAINTED。你当前采用“恢复旧状态并把自己的 undo 从链头摘掉”的方案。

对 write set 每个 RID：

1. 取表页 write lock，读取最新 meta / tuple。
2. 若最新不属于自己，按实现协议跳过 / 检查，不覆盖别人的版本。
3. head 指向自己：应用自己的合并 undo，恢复 log.ts / log.is_deleted，再让 head 指向 log.prev_version。
4. 自己新插入且没有 undo：改为 ts=0 的 deleted 条目。
5. 所有行恢复后设置 ABORTED，移除 read_ts 登记。

不删除主键索引项，后续可以复用那个 RID。恢复 deleted 状态时可能无有效旧值，仍需保留表页内合法 tuple 底稿，不写未初始化字节。

**手算：** 原 a=10/ts2，自身改 11 再 12，Abort 应恢复 10/ts2，而不是 11；链头指回事务开始前的 head。

**验收：** 普通更新回滚、插入回滚、删除回滚、冲突后回滚、主键更新中途失败后回滚。`txn_abort_serializable_test` 和回归测试提供入口。

## 14. 第十三步：Stop-the-world GC

事务结束不等于可立即删除事务对象，因为别人的旧快照可能沿 link 访问它的 undo buffer。GC 手动调用时所有表访问暂停，先做易证明的标记 / 清扫。

标记：

```text
W=watermark
遍历所有 RID：
    最新 meta.ts <= W → 无须为了快照读取再沿链
    否则沿 head：
        标记每个 log 所属事务 needed
        包含第一条 log.ts <= W 的边界日志后停止
清扫：已结束且不在 needed 中的事务，才可移出 txn_map
```

**边界日志必须保留。** 最新 ts9，要恢复 watermark=5，如果最后需要 log.ts4，它不是“比 watermark 老所以能删”，它恰恰是恢复目标状态所需的那条。

运行与 TAINTED 事务不能因为没被链引用就删。一个事务任何一条日志仍需要，就保留整个事务对象；第一版不必细分日志回收。

后面 serializable 验证还需要近期 committed write sets，即使其中是“没有 undo 的新插入”。你当前额外保存活跃 serializable 最小 read_ts 之后提交的事务，避免 GC 使幻读检测信息丢失。

**验收：** `DISABLED_GarbageCollection`、`DISABLED_GarbageCollectionWithTainted`，以及 `SerializableIndexPhantomSurvivesGc`。不要只看 map 变小，还要确认旧快照仍能读。

## 15. 第十四步：主键唯一性与固定 key→RID

P4 的索引是寻找版本锚点的工具：**一个 key 一旦对应 RID，后续删除不移除这个索引映射。** 主键删除和旧版本回看由 RID 版本链决定。

InsertTupleMvcc 先 ScanKey：

| 索引结果 / 最新版本 | 操作 |
| --- | --- |
| 没有 key | 新建 RID，写 tuple，登记 write set，InsertEntry |
| 已有 key，当前 live | 重复主键，TAINTED + throw |
| 已有 key，deleted 且版本可写 | 复用 RID，通过 ModifyTuple 复活 |
| 已有 key，其他事务正写或提交晚于快照 | 写冲突 |

不能因为“自己的快照看不见这行”就放心插新 RID；唯一性检查关心最新锚点与写冲突。

两个线程同时插新 key：可能都先 ScanKey 无结果，各自分配行，但只有一个 InsertEntry 成功；失败方 taint，且其新行已登记 write set 供 Abort 清理。检查真实 InsertEntry 返回值，不要把查重与插入当成原子操作。

当前 InsertTupleMvcc 以本项目主键索引范围为前提。多个任意非主键索引、复合主键扫描等拓展要额外验证，不能从普通单列测试推出通用支持。

**验收：** `txn_index_test` 的 IndexInsertTest / InsertDeleteTest / 冲突，`txn_index_concurrent_test` 的 concurrent insert。

## 16. 第十五步：索引扫描也要重建快照

普通 IndexScan 只得到 RID，不能直接输出最新 tuple。

与 SeqScan 共用 `GetTupleAndUndoLink → CollectUndoLogs → ReconstructTuple → predicate`。索引命中但快照行不存在或 deleted 时不输出。deleted key 的映射保留，才让旧事务能查到删除前版本。

重复 pred_keys 的 RID 去重，在 Init 与执行过程中保持明确游标。未命中索引也要记录扫描谓词；后续有人插入命中该谓词的行，正是幻读风险。

当前 IndexScan 的 point-key 构造是单值路径；课程 / 测试支持范围之外的复合 lookup，不能假设已经实现。

**验收：** 同一 key 插入→提交→删除→提交→复活；不同 read_ts 看到不同状态；IndexScan 与 SeqScan 对同一谓词得出同一结果。

## 17. 第十六步：更新主键，先删除全部旧 key

主键变更不能直接把 old key 的 RID 变成另一个 key，同时把 old 索引映射删掉。采用“旧 key 逻辑删除 + 新 key MVCC 插入 / 复活”的语义。

你的实现分两轮：

1. 先读完 child，构造所有目标 tuple，记录哪些 RID 的主键变化。
2. 未改变主键的普通原地更新；改变主键的先全部逻辑删除旧 RID。
3. 再给改变主键的所有目标调用 InsertTupleMvcc。

例子原 key `[1,2,3]`，执行 `SET key=key+1`。如果逐行删 1 插 2，会撞上仍未删的旧 2；先删所有旧 key，再插 `[2,3,4]` 才符合整批更新的设计。

这不是把“一条 update 的所有步骤”当作一个页锁原子操作；正确性靠事务临时时间戳、write set、undo 和冲突处理。中途失败要 Abort 整个事务，不能留下半批迁移。

旧快照通过 old key 的固定 RID 和 undo 仍看到旧数据；当前事务应看到新 key 对应内容。循环移动、重复目标 key、已有未受影响的冲突 key都要测试。

**验收：** UpdatePrimaryKeyTest、PrimaryKeyMoveAbortPreservesOldSnapshot、并发 update / abort。

## 18. 第十七步：先理解快照隔离为什么不够

两个人各读到“两名医生都在岗”。A 让自己下班，B 让自己下班，它们写不同行，不发生直接写写冲突，最终没人值班。这是 write skew。

可串行化验证的想法：我读完后、提交前，别人已提交的写是否改变了我所依赖的读取范围？若改变，就放弃自己的提交。

不仅记录已经读到的 RID，还需记录**扫描谓词**：WHERE salary>100，即使当时零行，之后别人插入 salary=200 也会改变读取结果。这是幻读。

SeqScan / IndexScan 的 Init 调用 AppendScanPredicate；全表扫描可用空 predicate 表示“任意行”。即使查询外层有 Filter，记录更宽扫描范围也是保守正确但可能多 abort 的方案。

## 19. 第十八步：实现 VerifyTxn

你当前采用较保守的扫描谓词验证：

1. 只读事务直接通过。
2. 找已提交且 commit_ts>自身 read_ts 的其他事务。
3. 将它们 write set 中、自己扫描过的表的 RID 合并去重。
4. 对每个候选 RID 沿版本链检查 read_ts 之后的**已提交版本变更**。
5. 检查变更前或变更后是否命中任一已记录谓词，命中则验证失败。
6. 在 commit_mutex 保护的提交序列中验证，失败释放提交锁并 Abort，返回 false。

同时检查 before / after：插入只有 after，删除只有 before，更新可以从范围外进入或从范围内离开。不能只看当前最新值。

例子：我查 `v>100`；别的事务把 50→200 提交，再把 200→50 提交。当前值 50 看似没变化，但中间提交版本 200 命中过我的读取范围，需要沿链看中间过程。

别人未提交的 temp 版本不当作“已提交写”；沿其 undo 退到最新已提交状态，再检查提交历史。不要把 temp ts 很大这一事实当作验证必失败的理由。

保守验证可能拒绝某些可以进一步精确判定安全的事务，这是性能 / 并发接受率代价。本教程复现的是你现有谓词与版本验证方案，不宣称实现了所有优化型 OCC 协议。

**验收：** SerializableTest / ConcurrentSerializableTest；回归覆盖插入和删除幻读、更新前后范围变化、中间版本、未提交版本跳过、GC 后写集合仍能验证。

## 20. 第十九步：调试输出

TxnMgrDbg 应逐 RID 输出 meta.ts / deleted / 最新 tuple，再沿 link 输出事务身份、log 下标、旧 ts 与删除状态。遇到无效 link / 已回收事务要安全终止输出，不把调试函数本身变成崩溃来源。

每个问题同时画四列：

```text
时间 | 各事务 read_ts/state | 最新 tuple/meta | undo 链与 owner
```

例如 old reader ts2，writer 更新 10→20，提交 ts3，reader 仍应退到 10。再 Abort 一个未提交 writer，检查链头指向哪里。

写入错先看 meta 所有者；读取错先看收集日志停止点；回滚错先看第一条 undo 是否保留最初旧值；GC 后错先看边界日志与近期 write set 是否被清扫。

## 21. 分阶段验证命令

```bash
cmake --build build --target txn_timestamp_test txn_scan_test txn_executor_test txn_index_test txn_index_concurrent_test txn_abort_serializable_test -j4
./build/test/txn_timestamp_test --gtest_also_run_disabled_tests
./build/test/txn_scan_test --gtest_also_run_disabled_tests
./build/test/txn_executor_test --gtest_also_run_disabled_tests
./build/test/txn_index_test --gtest_also_run_disabled_tests
./build/test/txn_index_concurrent_test --gtest_also_run_disabled_tests
./build/test/txn_abort_serializable_test --gtest_also_run_disabled_tests
```

你的六个补充测试不在早期 `e7b2355` 重做基线里。完成基础后，可查看当前仓库的 `test/txn/txn_mvcc_regression_test.cpp` 再自行写等价场景；也可将这个测试文件复制到重做工作树并重新运行 CMake，让新目标被发现，然后：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target txn_mvcc_regression_test -j4
./build/test/txn_mvcc_regression_test --gtest_also_run_disabled_tests
```

构建目标由 `test/CMakeLists.txt` glob 发现，新增文件后要重新配置。

当前 P4 使用排序 / LIMIT 辅助事务测试；如果单练 P4，以 `26ea632` 为基线时需先补 P3 中的这些功能。你的完成版排序只做内存排序，这足以解释部分 P4 测试依赖，但不能替代 P3 外部算法验收。

正确性检查后再短时基准：

```bash
cmake --build build --target terrier-bench -j4
./build/bin/bustub-terrier-bench --bench-1 --duration 3000 --threads 4 --terriers 100
./build/bin/bustub-terrier-bench --bench-2 --duration 3000 --threads 4 --terriers 100
```

短运行只用于检查基本不变量和不死锁，不代表排行榜成绩。历史 P4_STUDY 记录过这两个基准，本套文档本次是否重跑以 verification.md 为准。

## 22. 排错表

| 表现 | 优先定位 |
| --- | --- |
| 旧事务读新值 | base 可见性判断 / Collect 停止点 |
| 自己读不到自己写 | 未识别 my_temp_ts |
| 重建列错位 | mask 与紧凑 undo schema 下标 |
| 回滚成中间值 | 每次修改新增日志 / 合并时覆盖最初旧值 |
| 冲突没触发 | 检查最新 meta，而非 child 的历史 tuple |
| 并发才错 | meta、tuple、head 读取或更新未共用页锁 |
| Commit 后只看见一半 | last_commit_ts 发布早于 write set 更新 |
| deleted key 不能再插 | 未复用固定 RID / 索引项删除 |
| 主键平移撞旧 key | 没先删除全部迁移源 |
| GC 后旧快照失败 | watermark 边界日志误删 |
| 仅幻读验证漏掉 | 未记录零结果谓词 / GC 丢 write set |
| 未提交写导致无故失败 | 验证将 temp 版本当作已提交变更 |

## 23. 合上文档自测

- 一个 UndoLog 的 ts、is_deleted 和 tuple 分别描述哪个时间的状态？
- 空日志 vector 与 nullopt 为什么不同？
- 原始值 10→11→10，为什么 undo 不能随便删掉？
- 冲突检查为什么必须和修改处于同一页锁内？
- Commit 怎样让新事务不看到半次提交？
- watermark=5 时，ts=4 的日志可能必须保留，为什么？
- old key 的索引项为什么在删除之后继续存在？
- 可串行化验证为什么既看 before 又看 after，还看中间提交版本？

最后独立写一个带插入、两次更新、旧快照读、Abort 和 GC 的小时间线；能预测每一步的表堆内容、undo 和可见结果，再运行代码验证。做到这里，你已经能用不变量理解这套实现，而不是依赖某个函数看起来像答案。
