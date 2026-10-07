# Project 1：一步一步建立缓冲池

阅读 [总入口](README.md) 后开始。本篇的目标是：上层只给一个 page_id，你能安全地把页带到内存，交出受保护的访问入口，并在必要时写回磁盘。

官方范围：[Fall 2025 P1](https://15445.courses.cs.cmu.edu/fall2025/project1/) 包含 ARC、Disk Scheduler、Buffer Pool Manager；页保护对象是缓冲池正确性的组成部分。下面的设计步骤依据你的本地接口与实现。

## 1. 你的历史和入口文件

`04b5a32` 实现 ARC，`015f201` 加入缓冲池等组件。按这个顺序重做很合适，但不要把历史实现里的并发细节无条件照搬。

| 模块 | 文件 |
| --- | --- |
| 替换策略 | [arc_replacer.h](../../src/include/buffer/arc_replacer.h)、[arc_replacer.cpp](../../src/buffer/arc_replacer.cpp) |
| 异步 I/O | [disk_scheduler.h](../../src/include/storage/disk/disk_scheduler.h)、[disk_scheduler.cpp](../../src/storage/disk/disk_scheduler.cpp) |
| 缓冲池 | [buffer_pool_manager.h](../../src/include/buffer/buffer_pool_manager.h)、[buffer_pool_manager.cpp](../../src/buffer/buffer_pool_manager.cpp) |
| 页保护 | [page_guard.h](../../src/include/storage/page/page_guard.h)、[page_guard.cpp](../../src/storage/page/page_guard.cpp) |
| 检查入口 | `test/buffer/`、`test/storage/disk_scheduler_test.cpp`、`test/storage/page_guard_test.cpp` |

## 2. 第一步：区分五个概念

| 概念 | 直观含义 | 不能误认为 |
| --- | --- | --- |
| page | 数据库里的一页内容，用 page_id 标识 | 永久固定的某个内存地址 |
| frame | 内存中能装一页的槽，用 frame_id 标识 | 固定绑定某个 page 的身份 |
| pin_count | 还有多少访问者保留这页 / 等待使用这页 | 页锁是否已被某线程拿到 |
| page latch | 同一页的数据读写保护 | 整个缓存映射保护 |
| dirty | 内存内容可能比磁盘新 | 页被访问过 |

读取本地 `common/config.h` 确认 `BUSTUB_PAGE_SIZE`，不要硬写你从旧教程看到的页大小。

缓存映射例子：

```text
page_table:  page 42 → frame 0
             page 99 → frame 1
frame 0: bytes of page 42, pin=2, dirty=true
frame 1: bytes of page 99, pin=0, dirty=false
```

只有 frame 1 可以淘汰。换成 page 100 后，frame 1 仍是同一内存槽，但 page_table 必须不再把 page 99 指向它。

**验收：** 能解释为什么 shared_ptr 保证 frame 对象存活，却不保证 frame 里的内容仍属于原 page。

## 3. 第二步：画出 ARC 四个列表

约定表头最新、表尾最旧：

```text
T1 = mru_       最近首次进入的常驻页 / frame
T2 = mfu_       被再次访问的常驻页 / frame
B1 = mru_ghost_ 从 T1 淘汰后的 page 历史
B2 = mfu_ghost_ 从 T2 淘汰后的 page 历史
p  = mru_target_size_，T1 的目标大小
c  = replacer_size_，总 frame 容量
```

ghost 只保存身份与历史，不保存页字节，也不保留一个可用 frame。你当前使用 `alive_map_` 以 frame_id 查常驻记录、`ghost_map_` 以 page_id 查历史，列表迭代器存在 `FrameStatus` 中。这让移动 / 删除列表条目不必遍历查找。

先写三条不变量：

1. 一个常驻 frame 在 T1 / T2 中恰好出现一次。
2. `curr_size_` 等于常驻且 evictable 的条目数，不等于列表总长度。
3. 每个 map 中保存的 iterator 必须指向对应列表中的有效条目。

你现有 ARC 的 resident hit 注释说“MRU or MFU”，代码实际都放到 MFU；学习时以状态变化与接口为准，不靠注释猜算法。

## 4. 第三步：先完成 Size / SetEvictable / Remove

这几个函数决定“谁可以被赶出去”。

`SetEvictable(fid, flag)`：

1. 检查 frame_id 合法范围，不能只检查非负。
2. 不存在的条目按接口约定处理。
3. 若状态没变化，不改 curr_size。
4. false→true 增一，true→false 减一。

`Remove(fid)`：只有 evictable 的常驻条目可以移除；清除常驻列表和 alive_map，减少 curr_size。它用于页删除，与 Evict 留下 ghost 历史的行为不同。

每个公共方法独立锁住 ARC 的 mutex；不要在已经持有非递归 mutex 的公共函数里再调用另一个会锁同一 mutex 的公共函数。需要复用逻辑时做不自行上锁的 private helper。

**手算：** 记录一个 frame 后 Size 仍为 0；设 true 变 1；重复 true 仍 1；设 false 变 0。

## 5. 第四步：实现 RecordAccess 的四种情况

先写分支表，再写代码：

| 访问情况 | p 的变化 | 条目最终在哪 |
| --- | --- | --- |
| T1 / T2 命中 | 不变 | T2 表头 |
| B1 命中 | `p=min(c,p+max(1,|B2|/|B1|))` | 从 B1 移出，T2 表头 |
| B2 命中 | `p=max(0,p-max(1,|B1|/|B2|))` | 从 B2 移出，T2 表头 |
| 完全未命中 | 不变，必要时裁剪 ghost | T1 表头 |

比例按整数除法，分母是**删除命中条目前**的大小。命中该 ghost 列表，分母必非零。`size_t` 是无符号数，下降时先比较 delta 与 p，别先减后取 max，那样已发生下溢。

完全未命中时，按本地骨架对应的四列表容量规则裁剪：`|T1|+|B1|==c` 时移除 B1 尾；否则总历史达 `2c` 时移除 B2 尾。BPM 的 Evict 与 RecordAccess 配合维持容量。调试时应能说明裁剪条件下为什么该 ghost 有元素，而不是用空列表判断默默遮住不变量损坏。

每次迁移都同步处理 map / list / iterator；新常驻记录初始不可淘汰，由 BPM 决定何时改变。resident promotion 不应改变已有 evictable 标志和 curr_size。

**当前具体遗漏：** `arc_replacer.cpp` 的 B2 命中只写了 `|B2|>=|B1|` 时 p 减 1，没有写 `|B1|>|B2|` 时按比例下降。比如 p=4，B1=6，B2=2，正确 p 应减 3 变 1，现有分支保持不变。重做必须覆盖两种比例方向。

## 6. 第五步：实现 Evict

```text
若 curr_size == 0：返回 nullopt
若 |T1| >= p：首选 T1，否则首选 T2
从首选列表尾向前找第一个 evictable frame
若没有，从另一列表找
仍没有：返回 nullopt
找到：常驻移出，page_id 放入对应 ghost 表头，curr_size--，返回 frame_id
```

不能直接淘汰表尾，表尾可能还被 pin。不能把 frame_id 当成 ghost 的持久身份，因为这个 frame 马上可能被另一页复用。

**验收：** 启用 `arc_replacer_test` 的 disabled 测试，另外手工覆盖 B2 比例下降、首选列表全 pin 时的回退、反复 SetEvictable 和 frame 重用。为了复现状态错误，可以临时打印四列表与 p，但不要让公共 Size 返回这些列表的长度。

## 7. 第六步：Disk Scheduler

调度器是“前台排队，后台干活”。请求中有读写方向、page_id、数据地址和完成通知 promise。

实现顺序：

1. 理解 `Channel` 的 Put / Get；Get 在没有任务时阻塞。
2. Schedule 遍历请求 vector，把每个请求 move 进队列。promise 不可复制。
3. worker 循环 Get；读调用 ReadPage，写调用 WritePage。
4. I/O 完成后 `callback_.set_value(true)`，唤醒等待 future 的线程。
5. 空 optional 是结束信号；析构先放信号，再 join 工作线程。

正确的等待顺序：

```text
promise → get_future → 请求入队 → future.get → 可以复用 / 释放数据缓冲区
```

请求的 `data_` 是裸地址。请求入队不意味着该字节数组已经被复制；I/O 完成前必须让内存保持有效。若异常路径不履行 promise，前台可能永久等候，或得到 broken promise；应有一致的错误传播设计。

**验收：** 先写某页、等 future、再读同页比较字节；批量请求全部获得完成通知；无请求时析构也能退出；有排队请求时按 FIFO 处理到结束信号。

## 8. 第七步：页保护对象到底负责什么

ReadPageGuard / WritePageGuard 是页资源的所有权凭证。通常进入 guard 前，BPM 已经 pin；guard 构造获取共享 / 独占页锁，析构释放页锁并 unpin。

必须约定 **只由一处增加 pin_count**。不要 BPM pin 一次、guard 构造再 pin 一次，最后只减一次。

| 操作 | 必须产生的行为 |
| --- | --- |
| 默认 guard | invalid，不释放任何页 |
| move 构造 | 接管所有成员和 valid 标志；源失效 |
| move 赋值 | 自移动保护；先 Drop 当前页，再接管源 |
| Drop | invalid 则直接返回；有效时只释放一次 |
| 析构 | 调用 Drop |
| As / GetData | valid 检查后返回只读地址 |
| AsMut / GetDataMut | 按本地约定确保写访问被标记 dirty |

你当前 WritePageGuard 构造采用保守方式标记 dirty：取得写 guard 就认为可能修改。这可以多写一些磁盘页，但比漏标脏更安全。重做若只在 GetDataMut 标记，确认所有写入口都会经过它。

`already_locked` 是你后来添加的参数，不是重做必须添加的 API。不要为了对照历史改变现有公有接口；先实现基础所有权协议。

## 9. 第八步：把 pin / unpin 的交接设计正确

建议先采用便于证明的协议：

```text
Fetch:
    持 BPM mutex：找到 frame、pin++、SetEvictable(false)
    释放 BPM mutex
    获取页 latch（可能等待）
    返回 guard

Drop:
    guard 失效标志，避免重复释放
    释放页 latch
    持 BPM mutex：pin--；若降到 0，SetEvictable(true)
```

重点是 pin 的递减与“降到零后可以淘汰”作为同一个受保护的状态转换。你现有 Drop 在取得 BPM mutex **之前** fetch_sub，存在一个应复核的交接窗口。

具体交错：A 把 pin 减到 0 但尚未更新 replacer；B 抓取该 frame，pin=1、不可淘汰；B 再 Drop 并设可淘汰；C 淘汰并 Reset frame；A 最后才进入 BPM 区域，可能把已经 free / 已换页的 frame 标为可淘汰。仅使用 atomic pin 并不能保证映射、frame 重置与替换状态一致。

**验收：** 画 A/B/C 的时间线，能证明任何正在等待页 latch 的抓取者也被 pin 保护；同一 guard 多次 Drop 不会重复减 pin；移动后的源析构不影响目标。

## 10. 第九步：先做 BPM 命中和空闲 frame 路径

BPM 成员职责：

- `frames_` 保存实际字节和页锁。
- `page_table_` 保存 page→frame。
- `free_frames_` 保存未使用 frame。
- 你添加的 `frame_to_page_` 用于从受害 frame 找到旧 page。
- replacer 只提供可淘汰 frame，scheduler 只执行 I/O。

先完成命中：锁 BPM，查映射，pin++，RecordAccess，SetEvictable(false)，释放 BPM，再构造 guard。

然后做未命中且有空闲 frame：

1. 取 free frame，初始化干净状态。
2. 调度目标 page 的读取并等待完成。
3. 建 page_table / frame_to_page 映射。
4. 设置 pin=1，记录访问，设不可淘汰。
5. 释放 BPM，再获取页锁并交出 guard。

第一版可在 BPM mutex 下等待 I/O，以减少“同一页被加载两次”的状态复杂度。它限制吞吐，但便于建立正确基线。之后若释放锁等待 I/O，就必须加入 loading 状态和等待协议，不能只把 unlock 提前。

本地 NewPage 主要分配递增 page_id，不会立即返回某个页保护对象。不要按旧版 `NewPage(&page_id)` 教程实现。

## 11. 第十步：加入淘汰

```text
缓存未命中，free list 为空：
    victim = replacer.Evict()
    没有 victim → CheckedRead/WritePage 返回 nullopt
    若 victim dirty → 写旧 page，等待完成
    删除旧 page↔frame 映射
    Reset frame
    读新 page，等待完成
    新建映射，pin=1，RecordAccess，不可淘汰
```

绝不能先覆盖受害 frame 字节再发出写回旧页的请求。后台写使用同一个数据地址，届时可能把新页字节写进旧页。

**手算：** 缓冲池只有两个 frame。写 A、写 B，两个 guard 不释放，读 C 应失败；Drop A 后读 C 成功；再读 A，验证之前字节已落盘且能恢复。

`ReadPage` / `WritePage` 包装函数在资源不足时会终止程序；需要处理不足时用 Checked 版本。B+ 树及外部排序 pin 太多页也会触发这个问题。

## 12. 第十一步：Flush 与 Delete

Flush 必須回答三个问题：是否驻留？flush 期间身份会不会变？写盘后是否还能把 dirty 清掉？

Safe Flush 建议流程：在 BPM mutex 下确认驻留并临时 pin；释放 BPM mutex；获取页写锁；写盘等待完成；清 dirty；释放页锁；在 BPM mutex 下 unpin 并恢复 evictable。不能调用会把非驻留页重新读入的 Fetch 来改变“非驻留返回 false”的语义。

你现有 FlushPage 持 BPM mutex 等待页写锁。一个线程若持该页 guard 又请求另一页，就可能等待 BPM mutex；Flush 同时等待它释放页锁，形成循环。BPM 的 Fetch 已避开这个锁顺序，Flush 也应采用同样原则。

ReadPageGuard::Flush 如果修改普通 bool dirty，多个读 guard 同时 Flush 会有共享锁下写元数据的风险。重做需要额外同步 dirty 的更新，或明确采用安全的专门 flush 协议。不能把“页字节只读”推成“所有元数据只读”。

Unsafe Flush 按接口不取页 latch；按其调用约定处理 dirty。它不是可在任意并发时调用的 Safe Flush 替代品。

DeletePage：未驻留则释放磁盘空间并成功；驻留且 pin>0 则失败且不改状态；可删除则移出 replacer / 映射，Reset，放回 free list，DeallocatePage。不必先保存已经决定删除的数据。

FlushAll 可以先在 BPM mutex 下取 page_id 快照，再逐页安全 flush，避免遍历过程映射被修改或递归锁死。具体 API 是否要求调用者持锁，应在你的实现中统一。

## 13. 分阶段验证

```bash
cmake --build build --target arc_replacer_test disk_scheduler_test page_guard_test buffer_pool_manager_test -j4
./build/test/arc_replacer_test --gtest_also_run_disabled_tests
./build/test/disk_scheduler_test --gtest_also_run_disabled_tests
./build/test/page_guard_test --gtest_also_run_disabled_tests
./build/test/buffer_pool_manager_test --gtest_also_run_disabled_tests
```

| 失败表现 | 优先检查 |
| --- | --- |
| Size 不对 | 重复切换 evictable、Evict / Remove 少减一次 |
| 读到别页内容 | 映射残留、旧 I/O 未结束就复用 frame |
| 所有页都不可淘汰 | guard 没 Drop、移动源又保留 pin |
| 多线程偶发失败 | pin / replacer 交接、map 访问未锁 |
| 测试卡住 | 持 BPM 等页锁、promise 没履行、重复锁同页 |
| 再加载后数据丢失 | dirty 漏标、写回在覆盖字节之后 |

## 14. 合上文档自测

- 为什么 ghost 按 page_id 记录而不是 frame_id？
- Size 为什么不是四列表长度之和？
- 等待页锁的线程为什么也需要 pin？
- dirty=true 时淘汰需要怎样的 I/O 顺序？
- 为什么锁住整个 BPM 等页 latch 可能死锁？
- 你能用两个 frame 和三页手算完一个完整缓存生命周期吗？

满足这些再进入 P2：索引会长期依赖页保护；若 P1 的所有权或锁交接不清楚，P2 的错误会很难定位。
