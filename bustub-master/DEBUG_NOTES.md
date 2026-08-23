# CMU 15-445 BusTub B+ Tree 作业开发文档

## 测试文件

`test/storage/b_plus_tree_delete_test.cpp`，共 3 个测试用例：

| 测试名 | 说明 |
|--------|------|
| `DeleteTestNoIterator` | 插入 {1,2,3,4,5}，删除 {1,5,3,4}，验证剩余 key=2，再删 2，验证树为空 |
| `OptimisticDeleteTest` | 验证乐观删除路径的 read/write 次数符合预期 |
| `SequentialEdgeMixTest` | 带 tombstone 缓冲区（NumTombs=2）的混合删除 |

---

## 问题一：`leaf->Remove()` 静默返回 false（影响 Test 1 & 3）

### 现象
- Test 1：`size` 为 5（期望 1），说明所有删除均未生效
- Test 3：`res` 为 false，说明某次删除返回失败

### 根因
`LeafPage::Remove()` 中，当 tombstone 缓冲区已满时：

```cpp
if (num_tombstones_ >= LEAF_PAGE_TOMB_CNT) {
    return false;  // BUG: 静默失败
}
```

当 `LEAF_PAGE_TOMB_CNT = 0`（即 `NumTombs=0`，默认模板参数）时，条件 `0 >= 0` 恒为真，所有删除操作直接返回 false。

### 修复
`src/storage/page/b_plus_tree_leaf_page.cpp`：tombstone 满时回退到物理删除。

```cpp
if (num_tombstones_ >= LEAF_PAGE_TOMB_CNT) {
    RemoveAt(i);
    return true;
}
```

---

## 问题二：乐观删除路径缺失（影响 Test 2）

### 现象
- `new_reads - base_reads` 为 0（期望 > 0）：所有页面访问均通过 WritePage，没有用 ReadPage
- `new_writes - base_writes` 为 5（期望 1）：悲观路径加写锁到所有节点

### 根因
`Remove()` 没有乐观路径，直接走悲观（全写锁）流程。

### 修复
新增 `TryOptimisticRemove()`：用 `FindLeafPageOptimistic`（ReadPage）遍历到叶子，确认叶子有余量后仅对叶子加写锁删除。`Remove()` 首先尝试该函数，成功则直接返回。

---

## 问题三：`InternalPage::Split` 的 `SetSize` 错误（影响 Test 1）

### 现象
删除 {1,5,3,4} 后只剩 key=2，再删 key=2，`root_page_id` 仍为 7（期望 `INVALID_PAGE_ID`）。

### 根因
`src/storage/page/b_plus_tree_internal_page.cpp::Split()`：

```cpp
SetSize(mid + 1);   // BUG：应为 SetSize(mid)
```

以 size=4、mid=2 的分裂为例：

- 分裂前：`val = [P0, P1, P2, P3]`，P2 的分隔键 K2 被提升到父节点，P2 及其右侧转移到 `new_node`
- 正确结果：左节点保留 `[P0, P1]`，`SetSize(mid=2)`
- 实际结果：`SetSize(3)` 使左节点仍包含 P2 的**过期引用**（stale entry）

过期引用在正常搜索中不被访问（父节点路由已将 >= K2 的查询导向 `new_node`），但在删除的合并/借用流程中，`GetSize()` 返回的 3 导致多余的子节点被参与运算，最终将已删除的页面 ID 传播到其他节点，造成数据污染。

### 修复
```cpp
SetSize(mid);   // 正确保留左节点的 mid 个子节点
```

---

## 问题四：无兄弟节点（no-sibling）场景处理错误（影响 Test 1）

### 现象
当叶子节点是父节点的唯一子节点时，删除后树结构未正确清理。

### 根因
原代码中 `!has_left && !has_right` 分支缺失：代码直接进入 `else` 分支，调用 `parent->ValueAt(child_index + 1)`，而 `child_index=0`、`parent.size=1` 时该访问越界（UB）。

最初的修复尝试改为 `break`（跳过合并，依赖阶段3收缩根），但该方案忽略了两点：
1. `GetMinSize() = max_size / 2 = 3/2 = 1`（整数除法），内部节点 size=1 **不算下溢**，阶段3的收缩不会触发连锁处理
2. 空叶子节点未被清理，后续遍历读到空节点中的过期指针

正确做法：回退到 `parent->RemoveAt(child_index)` + `pages_to_delete`，让下溢向上传播。

---

## 问题五：`MoveAllTo` 在 source.size=0 时复制过期指针（影响 Test 1）

### 现象
删除级联导致内部节点 size=0，随后被合并进兄弟节点，将过期的 `page_id_array_[0]` 带入，最终以已删除的页面 ID 作为树根。

### 根因
`MoveAllTo` 无论 source 大小，都会无条件复制：

```cpp
recipient->page_id_array_[recipient_size] = page_id_array_[0];  // 当 size=0 时为过期值
```

`RemoveAt(0)` 只递减 size，不清零内存，所以 `page_id_array_[0]` 保留了被移除子节点的页面 ID。

### 修复
在 `b_plus_tree.cpp` 的内部节点合并调用处添加 guard：

```cpp
if (current->GetSize() > 0) {
    current->MoveAllTo(left, parent->KeyAt(child_index));
}
```

---

## 问题六：阶段3及迭代收缩根未处理 `size=0` 的根（影响 Test 1）

### 现象
删除级联最终使根的 size 降为 0，但 `root_page_id` 未被清空。

### 根因
阶段3只处理 `GetSize() == 1`（单子节点收缩），不处理 `GetSize() == 0`（根已无子节点）。

### 修复

**阶段3**（`b_plus_tree.cpp`）：

```cpp
} else if (root_internal->GetSize() == 0) {
    header->root_page_id_ = INVALID_PAGE_ID;
    pages_to_delete.push_back(old_root_page_id);
}
```

**迭代收缩根循环**：

```cpp
if (root_internal->GetSize() == 0) {
    hdr->root_page_id_ = INVALID_PAGE_ID;
    root_guard.Drop(); hdr_guard.Drop();
    bpm_->DeletePage(root_pid);
    break;
}
```

---

## 修复文件汇总

| 文件 | 修改内容 |
|------|----------|
| `src/storage/page/b_plus_tree_leaf_page.cpp` | `Remove()`：tombstone 满时回退到物理删除 |
| `src/storage/page/b_plus_tree_internal_page.cpp` | `Split()`：`SetSize(mid+1)` → `SetSize(mid)` |
| `src/include/storage/index/b_plus_tree.h` | 声明 `TryOptimisticRemove` |
| `src/storage/index/b_plus_tree.cpp` | 新增 `TryOptimisticRemove`；`Remove()` 优先走乐观路径；修复 `UpdateAncestorMinKey` 参数；no-sibling 用 `RemoveAt`；`MoveAllTo` 加 size>0 guard；阶段3和迭代循环处理 `size=0` 根 |

---

## 根本原因总结

所有 bug 均源于删除路径的边界情况未被覆盖：

1. tombstone 缓冲区满时静默失败
2. 乐观删除路径缺失
3. 内部节点分裂留下过期引用（SetSize 错误）
4. 无兄弟节点时访问越界 + 空节点不清理
5. 合并空节点时复制过期指针
6. 树根 size=0 时未将树置空
