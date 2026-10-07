# Project 3：从一批 tuple 到完整 SQL 查询

先读 [总入口](README.md)，确保 P1 / P2 可以支持表与索引访问。这一篇先复现你的 P3 Task 1—3，再给出尚未完成部分的具体补齐路线。

官方范围：[Fall 2025 P3](https://15445.courses.cs.cmu.edu/fall2025/project3/) 的必做范围是访问执行器、聚合与连接、Grace Hash Join 及优化、外部归并排序、LIMIT、窗口函数。你的本地 `p3.17-topn.slt` 明确标注 0 分，Top-N 在本文作为拓展练习，不能替代外部排序。

## 1. 先选对历史版本

| 阶段 | 对照入口 |
| --- | --- |
| Task 1 | `4f1b87e` / `solution-task1`，提示提交 `670ee70` |
| Task 2 | `82b394b` / `solution-task2`，提示提交 `5670810` |
| 主线整合 | `2e9c834`，保留了手写尝试分支 `wip-manual-task1-task2` |
| Task 3 | `fc7ee34` / `solution-task3` |
| P3 后期整理 | `bd69d66`、`26ea632` |
| P4 中为事务测试补的排序 / LIMIT | `d96c1d5`、`ef110d3` 附近的修改 |

**不要用 main 的 Insert / Update / Delete 当作 P3 初始答案。** P4 已将它们改成 MVCC 版本。单独查看旧函数用 `git show solution-task1:bustub-master/src/execution/update_executor.cpp`。

当前 `HashJoinExecutor` 用全量右侧内存哈希表；`ExternalMergeSortExecutor` 收集全部 entries 后 std::sort；`IntermediateResultPage` 与 MergeSortRun 迭代器仍为空 / UNIMPLEMENTED；WindowFunctionExecutor 仍抛未实现异常。

另一个版本差异：当前排序比较器的默认 NULL 位置与 Fall 2025 页面要求相反。按课程目标重做时，默认 ASC 是 NULLS FIRST，默认 DESC 是 NULLS LAST，显式 NULLS FIRST/LAST 优先。

## 2. 第一步：读懂执行器接口

入口：[abstract_executor.h](../../src/include/execution/executors/abstract_executor.h)、[executor_factory.cpp](../../src/execution/executor_factory.cpp)、[values_executor.cpp](../../src/execution/values_executor.cpp)、[filter_executor.cpp](../../src/execution/filter_executor.cpp)。

你的版本采用批量接口：

```text
Init()：开始 / 重新开始执行，初始化子执行器和当前状态
Next(tuple_batch, rid_batch, batch_size)：最多提供 batch_size 行
返回 true：本次有输出；false：已经耗尽
GetOutputSchema()：输出 tuple 如何解码
```

每次 Next 开始清空两个输出容器。剩余不足一批仍返回 true；下一次才返回 false。流式算子不要把“某一批过滤后没有结果”误认为整条输入耗尽，应继续拉后续批。

记录下面几个词：

- Tuple 是一行序列化数据，需要 Schema 解码。
- RID 是真实表行位置，扫描输出后供 Update / Delete 使用。
- PlanNode 描述算子“要做什么”，Executor 保存“做到哪里”。
- Expression::Evaluate 用 tuple 与其 schema 求值；EvaluateJoin 用左右两行和各自 schema。
- 输出 schema 与子输出 schema 可能不同，求输入表达式用子 schema。

典型计划：

```text
Projection
    Aggregation
        SeqScan
```

根调用 Next，子算子被逐层拉取。聚合 / 排序这类必须看完输入才能给出整体结果的算子会打断流式流水线。

**验收：** 读 Values / Filter 的实现，用 batch_size=1 手算父子四次 Next；解释为什么 `Init` 必须允许重复调用。

## 3. 第二步：SeqScan（P3 版本）

修改 [seq_scan_executor.cpp](../../src/execution/seq_scan_executor.cpp) 和头文件。P3 先做简单可见性，暂不遍历 undo。

Init 从 catalog 按 table_oid 取 TableInfo，初始化表迭代器。Next：

1. 清空输出。
2. 取迭代器当前 RID 与 tuple/meta，推进迭代器。
3. 当前删除标记则跳过。
4. 若有 filter_predicate，用表 schema 求值；只有非 NULL 且 true 才保留。
5. 输出 tuple 与同位置 RID，直到 batch 满或表结束。

不要把 iterator 保存在局部变量中导致每次 Next 从头扫描。不要只按遍历次数计 batch_size；跳过删除 / 不匹配行不占输出额度。

**验收：** `p3.01-seqscan.slt`；空表、全过滤、跨多批、最后不足一批。P4 会替换第 3 步，先别同时写两个项目的协议。

## 4. 第三步：Insert 和 Delete

修改 [insert_executor.cpp](../../src/execution/insert_executor.cpp)、[delete_executor.cpp](../../src/execution/delete_executor.cpp)。

Insert 的 Next 通常耗尽子输入、写表、维护所有索引，再输出只有一个整数的 tuple：实际插入行数。它不是一批插入对应一次计数输出。

```text
Init: child.Init; done=false
Next:
    done → false
    count=0
    while child.Next:
        对每个 tuple：TableHeap.InsertTuple；成功后为每个 index InsertEntry；count++
    输出一行 count；done=true；true
```

索引 key 用 `KeyFromTuple(table_schema,key_schema,key_attrs)`，不能猜 key 是第一列。检查插入失败返回值，不要计数仍增加。

Delete 获取 child RID，把表条目标记删除，并删掉相应索引项；key 来自被删旧 tuple。不要把逻辑删表行误认为立即回收其 table page。

两个算子都要处理空输入：第一次输出 count=0，之后 false。P4 的 Delete 保留索引项，是另一个版本协议，P3 这里先维护普通索引。

**验收：** `p3.02-insert.slt`、`p3.04-delete.slt`，以及删除后再次索引查找。

## 5. 第四步：Update 与 Halloween 问题

修改 [update_executor.cpp](../../src/execution/update_executor.cpp)。P3 先按当时任务约定采用删除旧记录 / 插入新记录并维护索引，P4 后来改成原地 MVCC 更新。

对每个 child tuple，用全部 target_expressions 求新列值。每个表达式都基于**同一份旧 tuple**；例如 `SET a=b,b=a` 应交换，不能先改 a 再让 b 读到新 a。

建议先把所有候选 `(RID,old_tuple,new_tuple)` 收集完，再修改；否则扫描索引时更新索引 key，可能再次看到刚搬到后方的行，重复更新，这叫 Halloween 问题。

实现次序：准备所有目标 → 移除旧表 / 索引条目 → 写入新表 / 索引条目 → 输出原逻辑行数一次。具体失败路径遵循本地 P3 接口，不冒充具备 P4 事务回滚。

**验收：** `p3.03-update.slt`；普通列 / 索引列改变、表达式依赖、连续更新、空目标、不同 batch 大小。

## 6. 第五步：IndexScan

修改 [index_scan_executor.cpp](../../src/execution/index_scan_executor.cpp)。需要两种模式：

1. 有 pred_keys：逐个 ScanKey 得到 RID，重复 key / 重复 RID 去重，再回表。
2. 无 pred_keys 的有序索引扫描：用 B+ 树 iterator 获取 RID，回表输出。

索引存 key→RID，不能直接输出 key 当成完整表 tuple。回表后还要按 P3 删除标记检查、保留原 filter 再求值。

`WHERE a=1 OR a=1` 不能把同一行输出两次。Init 重置 RID 缓冲和游标，不能把上次执行剩余状态留下。

你当前 IndexScan 对特定 B+ 树别名做 dynamic_cast；这是本地受支持索引类型的实现方式。要知道这不是任意索引的通用代码，cast 结果不可盲用。

## 7. 第六步：SeqScan → IndexScan 优化

修改 [seqscan_as_indexscan.cpp](../../src/optimizer/seqscan_as_indexscan.cpp)。先把优化器当“计划树上的安全替换规则”，不改执行器状态。

通用流程：递归优化孩子 → CloneWithChildren → 检查当前节点是否匹配 → 匹配才替换，否则保留。

匹配语法：单一列与常量等值，支持常量在左；或同一列多个等值用 OR 连接。AND 的复杂过滤、不同列的 OR、不等式都保持 SeqScan。

你的 CollectEqualityKeys 用递归解析 OR 树并检查所有列索引一致，是不错的分解。提取后查 catalog 中是否存在相应单列索引；匹配才建立 IndexScanPlanNode，并保留原谓词作为复核。

**验收：** `p3.05-index-scan-btree.slt`、`p3.06-empty-table.slt`；用 `EXPLAIN (o)` 检查计划，不要只看结果。结果对而没用索引仍可能没实现优化要求。

## 8. 第七步：聚合哈希表

阅读 [aggregation_executor.h](../../src/include/execution/executors/aggregation_executor.h)、[aggregation_plan.h](../../src/include/execution/plans/aggregation_plan.h)，修改 CombineAggregateValues 与 [aggregation_executor.cpp](../../src/execution/aggregation_executor.cpp)。

group key 是 group-by 列值数组，aggregate value 是每个聚合函数的当前结果。Init 耗尽 child，逐行 InsertCombine，最后建立哈希表输出迭代器。

输入示例：

```text
(dept=A, salary=10)
(dept=A, salary=NULL)
(dept=B, salary=20)
```

对 A：COUNT(*)=2，COUNT(salary)=1，SUM=10。分组比较中两个 NULL 应视作同一个分组；不要用 SQL 的 NULL=NULL→UNKNOWN 直接作哈希 key 等价关系。

| 类型 | 合并时做什么 |
| --- | --- |
| CountStar | 每行加 1 |
| Count(expr) | expr 非 NULL 才计数 |
| Sum | 跳 NULL；原结果 NULL 时采用第一个非 NULL |
| Min / Max | 跳 NULL；首次有效值初始化，后续比较 |

你的本地骨架对空聚合初值有特定约定：CountStar 初值 0，其他初值 integer NULL。按这里的测试实现，不要盲目套别的数据库的空 COUNT(expr) 行为。

无 group-by 且输入为空，需要插入一个空 group 输出初值；有 group-by 的空输入则零行输出。HAVING 由外层 Filter 执行，不在 Aggregation 再实现一遍。

**验收：** `p3.07`—`p3.09`；NULL group、全 NULL aggregate、空输入、DISTINCT、重复 Init。unordered_map 构建结束后再生成 iterator，避免 rehash 使早先 iterator 失效。

## 9. 第八步：NestedLoopJoin 的状态机

修改 [nested_loop_join_executor.cpp](../../src/execution/nested_loop_join_executor.cpp)。先支持 INNER / LEFT，使用 EvaluateJoin，拼接左全部列再右全部列。

每一行左侧需要遍历全部右侧。批量 Next 在中途停止时必须记住：当前左 tuple、右侧当前批、批内位置、当前左是否曾匹配。

```text
无当前左行 → 从左 child 取得一行；右 child.Init；matched=false
还有右行 → 判断谓词；命中则输出，matched=true
右耗尽 → LEFT 且 !matched 时输出左+右侧 typed NULL；结束当前左
输出满 batch → 保留上述状态，返回
```

右表为空时 LEFT 对每行左输出一行 NULL；INNER 输出零行。NULL 谓词不匹配。右侧补 NULL 用每列的真实类型，不把全部都写成 integer NULL。

**验收：** `p3.10`—`p3.12`；一左行匹配多个右行且跨 batch、右为空、重复执行。`matched` 不能在每次 Next 重置。

## 10. 第九步：NestedIndexJoin

修改 [nested_index_join_executor.cpp](../../src/execution/nested_index_join_executor.cpp)。对每行 outer 求 key_predicate，ScanKey 得到 inner RID，回表，再拼接；没有匹配时 LEFT 补 NULL。

它只有一个 child：左侧 outer；右侧来自 catalog 的表和索引。多 RID 返回时也应保存探测结果游标以满足 batch 上限，不能一口气追加超过 batch_size。当前实现主要依赖唯一索引返回一个 RID 的范围。

在 P4 中，这个入口的直接 GetTuple 还需加入 MVCC 重建才能支持历史版本；当前 main 的该执行器仍是 P3 读取方式，不能推断任意事务查询路径已全面覆盖。

**验收：** `p3.13-nested-index-join.slt`，并对照 NLJ 的结果与 `EXPLAIN`。

## 11. 第十步：先复现内存 Hash Join

修改 [hash_join_executor.cpp](../../src/execution/hash_join_executor.cpp) 与头文件。你提交采用右 build、左 probe，对 LEFT JOIN 较直观。

build：右 join key→**所有**右 tuples 的 vector，不能只存一个。probe：每行左求复合 key，找到匹配 vector，逐个输出；保留 match_idx，输出满批后下次继续。

举例：左有 key=1 两行，右有 key=1 三行，结果应是六行。不同 key 的哈希值碰撞不表示相等，map 还需要完整 key 比较。

任何等值连接 key 分量为 NULL 时，该行不参与相等匹配；LEFT probe 的 NULL key 要输出补 NULL 行。不要将包含 NULL 的 key 放进使用 `CompareEquals==CmpTrue` 的 unordered_map，因为这种相等关系连 `key==key` 都可能为 false，不符合容器等价关系要求。

**验收：** `p3.14` / `p3.15`，复合等值、重复 key、无匹配 LEFT、多对多跨 batch。此时仅完成内存版本，继续下面的 Grace 实现。

## 12. 第十一步：NLJ → Hash Join 优化

修改 [nlj_as_hash_join.cpp](../../src/optimizer/nlj_as_hash_join.cpp)。从 AND 树递归提取全部等值连接，每个条件都必须是左列=右列，允许左右书写顺序颠倒。

不匹配时保留 NLJ：OR、非等值、同侧比较、有无法保留的剩余谓词。别提取两个等值条件后把第三个 `a>b` 悄悄丢掉。

左 / 右的区分依据 ColumnValueExpression 的 tuple_idx，不能根据表达式在等号左边就判为左表。取出的 key 表达式最后按对应 child schema 求值，别与连接输出的列偏移混淆。

**验收：** 正例转换后结果不变、反例保持原计划；多级 AND、等号两边翻转、多个 join 层。

## 13. 第十二步：先设计公共中间页

你的 [intermediate_result_page.h](../../src/include/storage/page/intermediate_result_page.h) 尚未实现。它是 Grace 分区和外部排序共用的基础，先完成它会避免两套重复的临时存储。

一种紧凑且适合 VARCHAR 的页布局：

```text
低地址：固定 header {tuple_count, free_begin, free_end}
        slot[0]={offset,length}, slot[1]...
        未使用区域
高地址：tuple 的连续序列化字节，向低地址增长
```

Append 前检查 `serialized_size + sizeof(slot) <= free_end-free_begin`；不够返回 false，不写半条。一条 tuple 放不下当前页就换新页；若连空页也放不下，必须明确报错，不能无限创建空页。

注意 `Tuple::SerializeTo` 包含长度前缀，`GetLength()` 是数据体长度。你的页可以保存 SerializeTo 全格式，或存原始 GetData 字节并保存长度；写入和读回必须采用同一协议，计算容量包含实际序列化开销。

GetTuple(i) 应返回拥有自己数据的 Tuple 副本，别把即将 Drop 的页地址泄露为长期指针。page 里只保存整数、slot 与字节，不保存 vector / Tuple 对象的堆指针。

**验收：** 固定整数、不同长度字符串、正好容量边界、满页失败不改元数据；unpin 后被淘汰，再读页仍可恢复相同 tuple。

## 14. 第十三步：把内存 Hash Join 改为 Grace

按两个阶段拆：

```text
Partition：
    L 和 R 用相同 H1(key)%P 分区号，分别写入临时页链
    只保留有限的写缓冲和 page_id 元数据

Join each partition i：
    读 R_i，建立受预算限制的内存哈希表
    流式读 L_i，probe 并输出
    完成后释放哈希表，删除 L_i / R_i 的临时页
```

哈希相等的 key 必须进同一分区。分区函数与分区内哈希表可以使用不同的混合 / 种子；但左右在同一阶段必须一致。

内存预算按序列化字节计，而不是“几千行”。保留元数据可以，不能把完整输入藏进 vector，再把临时页当摆设。输出也必须按 Next 生成，不把所有连接结果物化到内存。

LEFT 的未匹配判断在该左行对应的分区内完成；NULL probe key 要保留补 NULL 语义。build NULL key 可以跳过。

如果某分区仍超预算，可递归重新分区并限制深度。单个热点 key 可能所有行永远进同一分区，必须有终止方案，例如以有限右侧块分次扫描该左分区，并跨块记录左行是否匹配，最终才补 NULL。不要递归到栈溢出。

临时页资源建议单独 owner 管理：全部 page_id、哪些已回收、当前 guard。正常耗尽、重复 Init、上游 LIMIT 提前结束、异常和析构都要释放；DeletePage 前先 Drop guard。

**验收：** 把预算调小迫使分区、多页字符串、严重 key 倾斜、一左行产生很多输出；统计最大内存和临时页回收，而不是只比较 SQL 结果。

## 15. 第十四步：写排序键与比较器

修改 [execution_common.cpp](../../src/execution/execution_common.cpp)。SortKey 是每个 ORDER BY 表达式在 tuple 上求值后的数组。

比较器按字典序：第一项相等再看第二项；ASC 看小于，DESC 看大于；全部相等返回 false，不能返回 true 破坏 strict weak ordering。

先处理 NULL，再调用普通 Value 比较。两个 NULL 在该排序项相等；单个 NULL 用显式 / 默认 NULL 位置。Fall 2025 默认 ASC NULLS FIRST、DESC NULLS LAST。当前代码用 `DEFAULT && DESC` 判 nulls_first，与你要重做的课程目标相反。

**验收：** ASC / DESC、多列混合、两个 NULL、单个 NULL、相同全部 key；比较同一个 entry 必须 false。用一个只有 `[NULL,1,2]` 的输入就能定位默认排序差异。

## 16. 第十五步：外部归并排序的 run 迭代器

阅读 [external_merge_sort_executor.h](../../src/include/execution/executors/external_merge_sort_executor.h)。MergeSortRun 保存一段有序输出的 page_id 序列，run 内每页有序，页间也有序。

Iterator 保存 run 身份、页序号、页内 tuple 序号、当前 read guard：

- Begin 找首个非空页；空 run 的 Begin==End。
- operator* 从中间页读出当前 tuple 副本。
- ++ 先走槽，再走下一页，只 pin 当前必要页。
- == 同时比较 run 身份和位置；两个不同 run 的同下标不能当作相等。

**验收：** 空 run、一页、多页、跨页、End；两迭代器并行走不同 run。不要让每个 iterator pin 该 run 的全部页。

## 17. 第十六步：生成初始 runs

修改 [external_merge_sort_executor.cpp](../../src/execution/external_merge_sort_executor.cpp)。

1. Init child，清理上一轮遗留临时页。
2. 拉 tuple，累积到一个受容量限制的工作区，至少按“能装入一页”计算。
3. 只对这小块 SortEntry 用 std::sort。
4. 将排序 tuple 写到 IntermediateResultPage，记录形成的 run。
5. 持续处理直到输入耗尽，尾部小块也生成 run。

不能继续沿用现有 `entries_` 收全表再 std::sort。这只是供 P4 测试使用的内存基线，不符合外部排序。

例子：内存每次最多 3 行，输入 `[8,2,5,1,9,3,7]`，初始 runs 为 `[2,5,8]`、`[1,3,9]`、`[7]`。

## 18. 第十七步：二路归并与回收

```text
while runs.size > 1:
    两两取 run A/B
    a=A.Begin，b=B.Begin
    选比较器更靠前的 tuple 写入输出页，推进相应 iterator
    一边耗尽就追加另一边
    产出新 run；释放输入 guard 后删除旧页
    奇数个 run 的最后一个可直接转移所有权
最终 Next 顺着唯一 run 批量输出
```

上面例子第一轮得到 `[1,2,3,5,8,9]`、`[7]`，第二轮得到完整序列。理论上只需同时 pin 两个输入页和一个输出页，加上有限工作空间；小 buffer pool 下验证不会因保留所有 guard 耗尽 frame。

旧输入页只有在输出已完整写好、迭代器不再引用它时才能删。最终 run 也要在耗尽或析构时回收，LIMIT 提前停止不能泄漏它。

**验收：** 空输入、单 run、多轮、奇数 run、VARCHAR、NULL、跨 batch；检查临时页生命周期。全量内存排序也能通过部分结果测试，因此必须检查资源行为。

## 19. 第十八步：LIMIT

修改 [limit_executor.cpp](../../src/execution/limit_executor.cpp)。保存 emitted，Init 清零。每次要求 child 提供 `min(batch_size,limit-emitted)` 行，更新 emitted；到达 limit 后不再拉 child。

如果 child 可能多返回，则需要保留剩余批内容 / 游标，而不是无条件丢弃；先把批量接口合同保持一致。当前实现依赖 child 遵守请求上限。

**验收：** limit=0、输入不足、恰好等于、跨多个批、调用 Init 后再次执行；`p3.16-sort-limit.slt`。本学期不必实现 OFFSET。

## 20. 第十九步：窗口函数

阅读 [window_plan.h](../../src/include/execution/plans/window_plan.h)，修改 [window_function_executor.cpp](../../src/execution/window_function_executor.cpp)。区别于 GROUP BY，窗口函数**不减少行数**，而是给每个输入行附加计算结果。

分解实现：

1. 读取输入并记录稳定行编号。
2. 为每个窗口函数独立求 partition key；不同函数可以使用不同分组。
3. 有 ORDER BY 时按共同排序要求处理；同一个窗口内按其 partition 分别维护状态。
4. 无 ORDER BY：计算整个 partition 的聚合，给每行附上相同结果。
5. 有 ORDER BY：计算该 partition 从开始到当前行的前缀聚合，逐行保存结果。
6. 输出普通 columns 表达式；窗口占位列从 `window_functions_` 按输出列下标填值。

手算：dept=A 的 salary 为 10、20、30；`SUM OVER(PARTITION BY dept)` 每行 60，带 ORDER BY salary 则 10、30、60。

窗口的默认 frame / peer 行语义按本地测试与课程支持范围确定。不要扩展出未要求的完整 SQL 窗口 frame 引擎。特别是普通前缀聚合不能代替 RANK 的并列处理。

RANK：同 partition 中排序 key 相同共享 rank；新的不同 key 的 rank 是该 partition 当前行序号（从 1 起）。key `[10,10,30]` 对应 `[1,1,3]`，不是 `[1,2,3]`，也不是 DENSE_RANK `[1,1,2]`。对复合排序键比较全部项，NULL 相等规则与排序保持一致。

你本地窗口类型只列 CountStar、Count、Sum、Min、Max、Rank；网页 AVG 示例是概念说明，不意味着你要给该 enum 增加 AVG。

多个 partition 的行交错时用分组状态表或每个 partition 的行索引，不能全局重置一个累加器。不同窗口函数要分开状态，不能因共同 ORDER BY 就共用 group map。

**验收：** `p3.20-window-function.slt`；空表、没有 partition、没有 order、不同 partition、多个窗口、并列 rank、NULL。检查每行普通列与窗口结果对应同一原行。

## 21. Top-N 拓展（非本学期必做）

你的仓库保留 [topn_executor.cpp](../../src/execution/topn_executor.cpp)、[sort_limit_as_topn.cpp](../../src/optimizer/sort_limit_as_topn.cpp)，仍未实现。想练习时，在必做外部排序之后做。

保持最多 N 个元素的堆，堆顶是当前保留集合中最差的一行。每来一行，更好就替换堆顶，最后把保留集合按最终顺序排好输出。时间 `O(M log N)`，存储 O(N)。ASC 时最大值在堆顶，DESC 时最小值在堆顶。

只把语义适合的 `Limit(Sort(child))` 改成 TopN；不能让它破坏强制测试外部排序的路径。测试文件 `p3.17-topn.slt` 带 ensure:topn，若没实现这条可选规则，宽泛跑全部 test-p3 会失败；这个失败要与必做项区分。

## 22. 运行与排错

```bash
cmake --build build --target sqllogictest -j4
./build/bin/bustub-sqllogictest test/sql/p3.01-seqscan.slt --verbose --in-memory
./build/bin/bustub-sqllogictest test/sql/p3.14-hash-join.slt --verbose --in-memory
./build/bin/bustub-sqllogictest test/sql/p3.16-sort-limit.slt --verbose --in-memory
./build/bin/bustub-sqllogictest test/sql/p3.20-window-function.slt --verbose --in-memory
ctest --test-dir build -R 'SQLLogicTest\.p3\.(0[1-9]|1[0-6]|1[89]|20)-' --output-on-failure
```

按阶段选 01—06、07—13、14—15、16 / 18 / 19 / 20；上面的 CTest 正则排除 0 分 Top-N 与排行榜。SQLLogicTest 是 SQL、期望结果和计划检查混合的文件，wrong result 时先找到对应 SQL，再缩为小输入。

| 表现 | 优先检查 |
| --- | --- |
| 重复行 / 少行 | 批游标、连接匹配游标、OR 去重 |
| 再执行为空 | Init 未重置状态或 iterator |
| 修改次数翻倍 | done 标志、更新过程中重扫新条目 |
| NULL 情况错 | 三值谓词、分组相等、typed NULL |
| 结果对但 ensure 失败 | 优化计划未触发 / 错误触发 |
| 排序结果有 NULL 时错 | 默认 NULL 位置与显式指定的优先级 |
| 输入大就耗内存 | 全量 vector / 全量哈希仍未外部化 |
| 临时页越跑越多 | Init、提前终止或异常未回收 |

## 23. 合上文档自测

- Next 返回 false 为什么意味着整个输入结束，而不是这一批没匹配？
- 一左行匹配三右行、batch_size=1 时如何续跑？
- 复合连接 key 为什么既要 hash 又要等价比较？
- Grace 的左右为什么必须用同一个分区函数？
- 外部排序如何保证不 pin 全部页？
- RANK 为什么会跳过数字？
- 你能准确区分“现有代码已实现”“结果暂时通过”“符合外部执行要求”吗？
