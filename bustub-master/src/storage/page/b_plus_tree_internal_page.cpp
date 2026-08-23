//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_internal_page.cpp
//
// Identification: src/storage/page/b_plus_tree_internal_page.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <sstream>

#include "common/exception.h"
#include "storage/page/b_plus_tree_internal_page.h"

namespace bustub {
/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * @brief Init method after creating a new internal page.
 *
 * Writes the necessary header information to a newly created page,
 * including set page type, set current size, set page id, set parent id and set max page size,
 * must be called after the creation of a new page to make a valid BPlusTreeInternalPage.
 *
 * @param max_size Maximal size of the page
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetSize(0);
  SetMaxSize(max_size);
}

/**
 * @brief Helper method to get/set the key associated with input "index"(a.k.a
 * array offset).
 *
 * @param index The index of the key to get. Index must be non-zero.
 * @return Key at index
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType { return key_array_[index]; }

/**
 * @brief Set key at the specified index.
 *
 * @param index The index of the key to set. Index must be non-zero.
 * @param key The new value for key
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) { key_array_[index] = key; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetValueAt(int index, const ValueType &value) { page_id_array_[index] = value; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueIndex(const ValueType &value) const -> int {
  for (int i = 0; i < GetSize(); i++) {
    if (page_id_array_[i] == value) {
      return i;
    }
  }

  return -1;
}

/**
 * @brief Helper method to get the value associated with input "index"(a.k.a array
 * offset)
 *
 * @param index The index of the value to get.
 * @return Value at index
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType { return page_id_array_[index]; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertAt(int index, const KeyType &key, const ValueType &value) {
  int size = GetSize();
  for (int i = size; i > index; i--) {
    key_array_[i] = key_array_[i - 1];
    page_id_array_[i] = page_id_array_[i - 1];
  }
  key_array_[index] = key;
  page_id_array_[index] = value;
  ChangeSizeBy(1);
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::Split(BPlusTreeInternalPage *new_node) -> KeyType {
  int size = GetSize();

  int mid = size / 2;

  /*
   * 这个key提升给parent
   */
  KeyType middle_key = key_array_[mid];

  /*
   * 右节点第一个child
   *
   * 注意：
   *
   * separator右边的child也要过去
   */
  new_node->page_id_array_[0] = page_id_array_[mid];

  int new_index = 1;

  /*
   * 搬右边的key和child
   *
   * key[mid+1...]
   * value[mid+1...]
   */
  for (int i = mid + 1; i < size; i++) {
    new_node->key_array_[new_index] = key_array_[i];

    new_node->page_id_array_[new_index] = page_id_array_[i];

    new_index++;
  }

  new_node->SetSize(new_index);

  /*
   * 左边保留:
   *
   * key:
   *
   * [invalid, ...]
   *
   * value:
   *
   * [...]
   *
   */
  SetSize(mid);

  return middle_key;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::Lookup(const KeyType &key, const KeyComparator &comparator) const -> ValueType {
  /*
   * InternalPage:
   *
   * key:
   *
   * [invalid, 20, 50, 80]
   *
   *
   * value:
   *
   * [p0, p1, p2, p3]
   *
   *
   * 找最大的 key <= target
   *
   * 返回对应 child page id
   */

  int left = 1;
  int right = GetSize() - 1;

  int result = 0;

  while (left <= right) {
    int mid = (left + right) / 2;

    if (comparator(KeyAt(mid), key) <= 0) {
      // mid 的 key <= target
      // 当前 child 可能是答案
      result = mid;

      left = mid + 1;

    } else {
      right = mid - 1;
    }
  }

  return ValueAt(result);
}
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::RemoveAt(int index) {
  int size = GetSize();

  for (int i = index; i < size - 1; i++) {
    key_array_[i] = key_array_[i + 1];
    page_id_array_[i] = page_id_array_[i + 1];
  }

  ChangeSizeBy(-1);
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeInternalPage *recipient, const KeyType &middle_key)
    -> KeyType {
  int size = recipient->GetSize();

  // recipient整体右移
  for (int i = size; i > 0; i--) {
    recipient->key_array_[i] = recipient->key_array_[i - 1];

    recipient->page_id_array_[i] = recipient->page_id_array_[i - 1];
  }

  // 原separator进入recipient
  recipient->key_array_[1] = middle_key;

  recipient->page_id_array_[0] = page_id_array_[GetSize() - 1];

  recipient->ChangeSizeBy(1);

  ChangeSizeBy(-1);

  // 被借走的key成为新的parent key
  return key_array_[GetSize()];
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeInternalPage *recipient, const KeyType &middle_key)
    -> KeyType {
  int size = recipient->GetSize();

  recipient->key_array_[size] = middle_key;

  recipient->page_id_array_[size] = page_id_array_[0];

  recipient->ChangeSizeBy(1);

  KeyType new_key = key_array_[1];

  for (int i = 1; i < GetSize() - 1; i++) {
    key_array_[i] = key_array_[i + 1];

    page_id_array_[i] = page_id_array_[i + 1];
  }

  ChangeSizeBy(-1);

  return new_key;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveAllTo(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  int recipient_size = recipient->GetSize();

  int current_size = GetSize();

  /*
   * middle_key 放到 left 最后一个 key
   */
  recipient->key_array_[recipient_size] = middle_key;

  recipient->page_id_array_[recipient_size] = page_id_array_[0];

  recipient_size++;

  /*
   * 搬 key/value
   */
  for (int i = 1; i < current_size; i++) {
    recipient->key_array_[recipient_size] = key_array_[i];

    recipient->page_id_array_[recipient_size] = page_id_array_[i];

    recipient_size++;
  }

  recipient->SetSize(recipient_size);

  SetSize(0);
}

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub
