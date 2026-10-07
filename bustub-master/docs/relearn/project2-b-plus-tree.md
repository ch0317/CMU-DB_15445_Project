# Project 2：把 B+ 树真正放进数据库页

阅读 [总入口](README.md)，并先完成 P1 的页保护与缓冲池。这里的树不是一堆 `new Node`，而是一组由 page_id 连接的数据库页。

官方范围：[Fall 2025 P2](https://15445.courses.cs.cmu.edu/fall2025/project2/) 包含页格式、查找 / 插入 / 删除、迭代器和并发；叶子带有延迟物理删除的 tombstone 缓冲。以下演算、伪代码和排错重点来自你的当前页布局、辅助函数与本地测试。

## 1. 你的提交怎样串起来

`9740755` 是较早的树实现，`15188bc` 达到插入删除能编译，`0a5eb33` / `444d0c8` 持续修插入，`2124edf` 是后续索引阶段提交。`08776f4` 恢复了手动编辑遗漏的 TryOptimisticRemove 声明。

阅读顺序：

1. [b_plus_tree_page.h](../../src/include/storage/page/b_plus_tree_page.h) 与其 `.cpp`。
2. [b_plus_tree_leaf_page.h](../../src/include/storage/page/b_plus_tree_leaf_page.h)、[leaf 实现](../../src/storage/page/b_plus_tree_leaf_page.cpp)。
3. [b_plus_tree_internal_page.h](../../src/include/storage/page/b_plus_tree_internal_page.h)、[internal 实现](../../src/storage/page/b_plus_tree_internal_page.cpp)。
4. [b_plus_tree.h](../../src/include/storage/index/b_plus_tree.h)、[树实现](../../src/storage/index/b_plus_tree.cpp)。
5. [index_iterator.h](../../src/include/storage/index/index_iterator.h)、[迭代器实现](../../src/storage/index/index_iterator.cpp)。

把并发与 tombstone 边界当成重做的一部分；不要因提交名是 Database Index 就认定所有这些部分都正确。

## 2. 第一步：画一棵两层树

```text
header.root = page R
                 R: key=[无意义, 30, 60]
                    ptr=[A, B, C]
              /           |           \
A: [10,20] → B: [30,40,50] → C: [60,70]
```

内部页的 `size` 是**孩子数量**。三个孩子只有两个有效 separator。叶子的 size 是物理 key/RID 数量，RID 指向表堆位置，不是下一层树页。

查 40 应从 R 的 B 路由；查 30 也应到 B，separator 相等时走其右侧对应子树。扫范围只需到达起始叶，再沿 next_page_id 向右走。

写下最重要的不变量：

- 有效叶 key 按比较器排序，key 与 RID 同步移动。
- 内部页 key[0] 不参与比较；ptr[0] 仍是有效孩子。
- separator 正确界定左右子树范围；本教程采用右子树物理最小 key 的维护方式。
- 同一层叶子通过 next 指针保持顺序。
- 节点字节都由 BPM 的 guard 保护，裸指针不得比 guard 活得更久。

## 3. 第二步：实现 BasePage 与 Init

BasePage 的 getter / setter 很简单，但它们的含义会影响所有分裂阈值。先明确：

```text
page_type = leaf / internal
size = 当前已占用的槽数
max_size = 本次树实例允许的大小，不一定等于物理数组容量
min_size = 当前代码采用 max_size / 2
```

先支持测试常用的小 max_size，例如 4，方便触发结构变化。对于奇数容量，分裂左右计数应与 min_size 约定一致；不要从不同教程混入不同的 ceil / floor。

叶 Init：类型 leaf、size=0、max_size、next=INVALID、tombstone 数量=0。内部 Init 同样设置其公共头。Header 页存 root_page_id。

页对象不是普通堆对象：`guard.AsMut<LeafPage>()` 解释 frame 中的字节。不要把拥有堆内存的 vector / string 放进持久页布局；vector 只能作为页操作过程中的临时工作区。布局变化必须重新计算容量，避免结构超过一个页。

## 4. 第三步：只做叶子有序插入和物理删除

暂时用 `NumTombs=0`，先掌握数组操作。

`InsertAt(i,key,rid)`：从最后一个元素向右搬，再写 i，size++。从左到右右移会覆盖还没复制的值。

`RemoveAt(i)`：从 i 后向左搬，size--。此函数表示物理删除，与逻辑 Remove 区分。

二分定位统一用 lower_bound：找第一个 `key_at >= target` 的位置。位置可能是 size，先检查边界再 KeyAt，避免空页和大于最大 key 的访问越界。

**手算：** `[10,30,50]` 插入 20，返回位置 1；删 30 后 `[10,20,50]`。同时写 RID 数组，确认映射没有错位。

## 5. 第四步：内部页路由

内部 Lookup 可找第一个 `separator > target`，返回其前一个孩子；只在 key[1..size) 中查找。

```text
keys=[无意义,30,60], ptr=[A,B,C]
target=5  → A
target=30 → B
target=59 → B
target=60 → C
target=99 → C
```

只有一个孩子时直接返回 ptr[0]。比较使用 comparator，返回负 / 零 / 正，不要对 GenericKey 用普通 `<`。

你的代码多处复制相似二分逻辑。重做可以实现页级 Lookup / LowerBound 辅助函数并复用，降低不同路径路由不一致的风险。

## 6. 第五步：空树插入与点查

先不实现任何分裂：

1. 锁 header；若 root 无效，NewPage 分配叶页，Init，写第一个 key/RID，更新 header。
2. 非空树按内部页 Lookup 下行，最终叶中 lower_bound。
3. 相同 live key 返回 false，不能增加 size。
4. 查找命中返回 RID；不存在返回 false，不应向 result 追加条目。

点查锁耦合：持父读 guard 时先取得子读 guard，再放父。header 到 root 也需要这样的交接，避免读到 root id 后根在空隙中被换掉 / 回收。

**验收：** 空树查询、单个 key、重复插入、key 小于最小 / 大于最大。只在叶页内测试时也检查 GetRootPageId。

## 7. 第六步：叶子分裂

先明确 max_size 的约定。你当前叶页在插入后 `size==leaf_max_size` 时分裂；内部页通常先允许到 max_size，下一次添加孩子时再分裂。这个不对称可以成立，但安全判断必须分别匹配，不能一律写 `size<max_size`。

每次插入前，都要确保数组有位置存本次新增槽。数组物理容量等于测试 max_size 的极限情况也不能越界；内部溢出可用临时数组组织分裂，而不是先写出数组边界。

例子：叶 `[10,20,30,40]` 分为 `[10,20]` 与 `[30,40]`。

1. 分配并初始化右叶。
2. 按 split_point 把后半 key/RID 拷到右叶。
3. 更新两边 size。
4. `right.next=old.next`，`old.next=right`。
5. 将 `right.KeyAt(0)=30` 与 right page_id 交给父节点。

叶 separator 是**复制**到父，不从右叶移除 30。

**验收：** 连续、逆序、乱序插入到首次分裂；所有点查正确，叶链按序，root 变内部页。

## 8. 第七步：向父传播分裂

用 Context 保存从根到叶的 write guards。叶分裂后，用原孩子 page_id 在父 ptr 数组找到位置，插入 `(up_key,right_pid)` 到它后面，比仅按 key 推测插入位置更容易证明父子关系正确。

若旧叶就是根：创建内部根，size=2，ptr[0]=旧叶，key[1]=右叶最小 key，ptr[1]=右叶，更新 header.root。

父也溢出时做内部分裂。例如：

```text
原 keys=[无意义,20,40,60,80], ptr=[A,B,C,D,E]
提升 40：
左 keys=[无意义,20], ptr=[A,B]
右 keys=[无意义,60,80], ptr=[C,D,E]
```

与叶分裂不同，40 不作为右内部页的有效 key 保留；它负责在父上隔开这两个内部子树。右 ptr[0] 必须是 C。

你的 InternalPage::Split 大体按这个方式写：保存 key[mid]，将 ptr[mid] 作为右 ptr[0]，再搬 mid+1 后的条目。

**验收：** 小容量插入足够多的 key，形成三层树；每个内部节点有效 separator 数量=size−1；每个孩子都能通过 parent 的范围路由到。

## 9. 第八步：先完成 NumTombs=0 的删除

删除并不是插入的逆向代码。它需要恢复半满限制，并在根上作特例。

```text
找到目标叶并删除
若根是叶：处理空根
若未下溢：必要时更新 separator，结束
否则：尝试向左借；不行向右借；都不行合并
合并导致父少一个孩子 → 向上继续修复
最后根只剩一个孩子 → 该孩子成为新根
```

结构修复前记录当前节点在 parent 中的 child_index；优先使用同一父节点的相邻兄弟，不能直接把 next leaf 当成一定可借的兄弟。

## 10. 第九步：叶子借位与合并

左借：左兄弟末元素移到当前叶头；父描述当前叶的 separator 更新为当前新最小 key。

```text
left=[10,20,30], current=[40]
变为 left=[10,20], current=[30,40]
parent separator: 40 → 30
```

右借：右兄弟首元素移到当前叶尾；父描述右兄弟的 separator 更新为右兄弟新的最小 key。

合并：把右叶全部追加到左叶，`left.next=right.next`，父删除 right 对应孩子条目。先从结构中断开旧页，等所有相关 guard 释放后才能 DeletePage。

保持物理 min key 的实现需要向祖先传播边界改变；沿最左孩子链没有当前层 separator，直到遇到它在某祖先中的 index>0 再更新。

**你的 Context 风险：** UpdateAncestorMinKey 会 pop write_set；Remove 的其他部分却依赖 write_set 保存整条路径，且调用时末尾含叶本身。重做应让“维护 separator”用索引只读遍历 Context 的 guard 容器，不消耗后续还要用于下溢修复的路径，也不把叶页当 parent 解释。

## 11. 第十步：内部页借位与合并

内部页借位要让 parent separator 下降，而不是只搬一个 key/RID。

右借例子：

```text
parent separator=40
current: keys=[无意义,20], ptr=[A,B]
right:   keys=[无意义,60,80], ptr=[C,D,E]

借后 current: keys=[无意义,20,40], ptr=[A,B,C]
      right: keys=[无意义,80], ptr=[D,E]
      parent separator=60
```

关键：右页 ptr[0] 必须从 C 改为 D。你现有 `MoveFirstToEndOf` 只从下标 1 开始搬 page_id_array，没更新下标 0。这会让右页继续指向已经借出的 C，丢掉应有的 D 路由。重做用上面例子逐格检查。

左借同理：左页最后孩子进入 current.ptr[0]，旧 parent separator 变 current.key[1]，左页最后有效 key 上升为新 parent separator。

内部合并：将 parent separator 作为左页新增有效 key，对应右 ptr[0]，然后追加右的 key[1..] 与 ptr[1..]；parent 删除右孩子，再向上修复。

**验收：** 独立手算左借、右借、合并；构造删除序列使树从三层缩到一层，所有余下 key 仍可查询。

## 12. 第十一步：引入 tombstone，不改物理 KeyAt

现在才启用 `NumTombs>0`。它是保存“已经逻辑删除但仍占数组槽”的下标队列，按删除时间从旧到新。

```text
keys=[10,20,30,40], tombs=[1,3]
物理 size=4；逻辑 live keys=[10,30]
KeyAt(1) 仍是 20；GetValue(20) 应报告不存在
GetTombstones() 返回 [20,40]
```

buffer 满时不是物理删除**本次新请求的 key**，而是处理最旧 tombstone，然后把本次删除追加到队尾。你现有 Remove 满时调用 `RemoveAt(i)` 删除当前目标，不符合这个 FIFO 过程。

例子 k=2，先删 20 再删 40，再删 30：

1. 最旧 20 被物理删除：keys 变 `[10,30,40]`。
2. 原 tombstone 40 的下标从 3 变 2。
3. 本次目标 30 的下标也由 2 变 1。
4. 新 tombs 为 `[2,1]`，对应 `[40,30]`，不是按下标升序。

物理删槽 i 时所有大于 i 的 tomb 下标减一；物理插槽 i 时所有大于等于 i 的 tomb 下标加一。

重复删除已有 tomb 不再添加。重新插入一个 tomb key，应更新该槽 RID、移除该 key 的 tomb 标记，使它重新有效；现有树 Insert 对所有物理重复都返回 false，遗漏了这个场景。

**验收：** k=0、1、2、3；满队列处理顺序；删除后重插不同 RID；点查与 scan 都忽略 tomb，但 KeyAt 保留物理条目。

## 13. 第十二步：tombstone 随结构变化一起搬

这是最容易在“普通插入删除都通过”后漏掉的部分。

分裂：按原物理下标落到左右，右下标减 split_point；每个页保留其中 tomb 的相对删除顺序。你当前 Split 已做这类映射。

借位：移动条目可能本身是 tomb；来源页剩余 tomb 下标修正，目的页旧 tomb 因数组移动修正，被移入条目的 tomb 按其来源顺序加入。不能只搬 key/RID。

合并：先得到 `recipient旧删除序列 + donor删除序列`；来源页删除按项目规则视为更新的删除。转换到合并后下标，再按容量处理最旧项，物理压缩时继续修正余下下标。

一个便于理解的实现技巧：在变动过程中临时把 tomb 表示为 key 和顺序，数组组织完成后重新定位对应物理下标；由于 key 唯一可定位。这只是临时工作区，不把 vector 放到页内。随后可优化为直接下标变换。

当前三个叶迁移方法都没有完整处理 tomb 状态；需要作为一组重新设计。

结构是否下溢主要根据物理占用约定判断，逻辑条目数用于可见性。你当前乐观删除用 live size，悲观删除用 physical size，根空时还会回收全 tomb 叶。重做必须统一，并保留测试要求的“逻辑为空但仍有物理 tomb 页”的状态；Begin 可以是 End，而 root / 物理树仍存在。

## 14. 第十三步：实现迭代器

迭代器需要当前 page_id、槽下标、guard 和 BPM 引用。`*it` 返回当前 key/RID；`++it` 前移，跳 tomb，页末沿 next 切页；尾标记统一用 INVALID_PAGE_ID。

抽一个 NormalizePosition helper：循环跳 tomb / 空叶 / 全 tomb 叶，直到找到 live 槽或 End。构造、Begin(key) 和 ++ 都用它，避免只有 ++ 会跳 tomb、起始位置却没处理。

Begin() 从根沿 ptr[0] 找最左叶；Begin(key) 找叶后 lower_bound，如果目标在页尾，应跨到后续叶继续找。它应给出第一个不小于 key 的 live 条目。

**锁所有权要点：** 你当前 Begin 持叶读 guard，又调用迭代器构造重新 ReadPage 同一叶；同线程重复取得 shared latch 不具有可移植的安全保证。更好的方式是把已经持有的 ReadPageGuard move 给迭代器，不能先解锁再毫无协议地假设这页仍属于同一路径。

不要先在 operator* 内调用 guard->As 再 assert guard 有效。无效检测必须在解引用之前。

**验收：** 空树、全 tomb 页、跨页、Begin(不存在 key)、大于最大 key、迭代结束。operator* 返回引用时，下一次移动 guard 后旧引用不再可用。

## 15. 第十四步：先证明悲观并发，再做乐观

基础 crabbing：自顶向下拿锁，先拿子再释放可放掉的祖先。子节点安全的含义是“本次操作不会继续影响父”，不是单纯当前未满。

- 插入安全：加本次条目后无需分裂。
- 删除安全：本次操作后无需借 / 合并；也考虑边界 key 更新。
- root / header：可能改变根时保持相应保护。

第一版可以保留整条写路径以易于推理，但它限制并发且需要足够 frame；后续在安全节点释放祖先，符合项目并发目标。

乐观路径：读锁下行，只有叶修改；如果叶不安全，释放并重新从根走悲观路径。read→write 通常不是原子升级，因此不能“读到叶 pid → 清空所有 guard → 等它的写锁”后直接沿用旧判断。

你当前 TryOptimisticInsert 正是这种路径：清空读 guard 后只取旧叶写锁，并未重新检查安全容量、叶的路由范围和可达性。期间它可能已被分裂 / 合并。Remove 虽重新检查 live 数，也没证明路由仍然有效。

可选设计：保持父读保护时转换叶锁并重查容量 / key 范围；若 parent/leaf 同时可能变动，回到根重试。根为叶要保留 header 保护。避免读锁升级等待造成自锁，明确采用“释放叶读锁、保持路径保护、获取叶写锁”的具体协议。

兄弟锁保持一致顺序；悲观路径与乐观路径都必须遵守同一套方向。不要以进程级大 mutex 替代每页锁的证明。

## 16. 分阶段运行

```bash
cmake --build build --target b_plus_tree_insert_test b_plus_tree_delete_test b_plus_tree_tombstone_test b_plus_tree_concurrent_test b_plus_tree_sequential_scale_test -j4
./build/test/b_plus_tree_insert_test --gtest_also_run_disabled_tests
./build/test/b_plus_tree_delete_test --gtest_also_run_disabled_tests
./build/test/b_plus_tree_tombstone_test --gtest_also_run_disabled_tests
./build/test/b_plus_tree_concurrent_test --gtest_also_run_disabled_tests
./build/test/b_plus_tree_sequential_scale_test --gtest_also_run_disabled_tests
```

建议每实现一个 helper 就在纸上检查；大测试的 segfault 往往只是早先数组或指针损坏的后果。

| 表现 | 先检查 |
| --- | --- |
| 只在 separator 相等时错 | Lookup 的 <= / < 选择 |
| 分裂后丢 key | 内部 ptr[0]、提升 key、叶链 |
| 右借后部分 key 消失 | 右 internal 的 ptr[0] 是否前移 |
| 删除后扫描错 | tomb 下标 / FIFO / next 链 |
| 重插 deleted key 失败 | 物理重复与 live 重复混淆 |
| 删除很少几次就崩溃 | Context 路径被错误 pop、leaf 被当 internal |
| 只在并发错 | 根到子锁交接、升级空窗、兄弟锁顺序 |

## 17. 合上文档自测

- 内部页 size=4，究竟有几个有效 key、几个孩子？
- 叶分裂与内部分裂的 separator 保留规则为什么不同？
- 用纸画出一次内部页右借，列出每个 ptr。
- tomb 队列满后为什么物理删最旧条目？
- Begin()==End 是否能推出 root_page_id 无效？
- 读锁释放再写锁获取期间，哪些旧结论不再有效？

独立做到这些，再进入 P3。扫描执行器会直接暴露你的叶链、迭代器和 RID 正确性。
