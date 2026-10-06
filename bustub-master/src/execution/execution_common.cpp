//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// execution_common.cpp
//
// Identification: src/execution/execution_common.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/execution_common.h"

#include "catalog/catalog.h"
#include "common/exception.h"
#include "common/macros.h"
#include "concurrency/transaction_manager.h"
#include "fmt/core.h"
#include "storage/table/table_heap.h"

namespace bustub {

TupleComparator::TupleComparator(std::vector<OrderBy> order_bys) : order_bys_(std::move(order_bys)) {}

/** TODO(P3): Implement the comparison method */
auto TupleComparator::operator()(const SortEntry &entry_a, const SortEntry &entry_b) const -> bool {
  for (size_t i = 0; i < order_bys_.size(); i++) {
    const auto &[direction, null_order, expr] = order_bys_[i];
    const auto &a = entry_a.first[i];
    const auto &b = entry_b.first[i];
    if (a.IsNull() && b.IsNull()) {
      continue;
    }
    if (a.IsNull() || b.IsNull()) {
      bool nulls_first = null_order == OrderByNullType::NULLS_FIRST ||
                         (null_order == OrderByNullType::DEFAULT && direction == OrderByType::DESC);
      return a.IsNull() == nulls_first;
    }
    if (a.CompareEquals(b) == CmpBool::CmpTrue) {
      continue;
    }
    return direction == OrderByType::DESC ? a.CompareGreaterThan(b) == CmpBool::CmpTrue
                                          : a.CompareLessThan(b) == CmpBool::CmpTrue;
  }
  return false;
}

/**
 * Generate sort key for a tuple based on the order by expressions.
 *
 * TODO(P3): Implement this method.
 */
auto GenerateSortKey(const Tuple &tuple, const std::vector<OrderBy> &order_bys, const Schema &schema) -> SortKey {
  SortKey key;
  for (const auto &[direction, null_order, expr] : order_bys) {
    key.push_back(expr->Evaluate(&tuple, schema));
  }
  return key;
}

/**
 * Above are all you need for P3.
 * You can ignore the remaining part of this file until P4.
 */

/**
 * @brief Reconstruct a tuple by applying the provided undo logs from the base tuple. All logs in the undo_logs are
 * applied regardless of the timestamp
 *
 * @param schema The schema of the base tuple and the returned tuple.
 * @param base_tuple The base tuple to start the reconstruction from.
 * @param base_meta The metadata of the base tuple.
 * @param undo_logs The list of undo logs to apply during the reconstruction, the front is applied first.
 * @return An optional tuple that represents the reconstructed tuple. If the tuple is deleted as the result, returns
 * std::nullopt.
 */
auto ReconstructTuple(const Schema *schema, const Tuple &base_tuple, const TupleMeta &base_meta,
                      const std::vector<UndoLog> &undo_logs) -> std::optional<Tuple> {
  std::vector<Value> values;
  for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
    values.push_back(base_tuple.GetValue(schema, i));
  }
  bool deleted = base_meta.is_deleted_;
  for (const auto &log : undo_logs) {
    std::vector<Column> columns;
    for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
      if (log.modified_fields_[i]) {
        columns.push_back(schema->GetColumn(i));
      }
    }
    Schema undo_schema(columns);
    uint32_t index = 0;
    for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
      if (log.modified_fields_[i]) {
        values[i] = log.tuple_.GetValue(&undo_schema, index++);
      }
    }
    deleted = log.is_deleted_;
  }
  if (deleted) {
    return std::nullopt;
  }
  return Tuple(values, schema);
}

/**
 * @brief Collects the undo logs sufficient to reconstruct the tuple w.r.t. the txn.
 *
 * @param rid The RID of the tuple.
 * @param base_meta The metadata of the base tuple.
 * @param base_tuple The base tuple.
 * @param undo_link The undo link to the latest undo log.
 * @param txn The transaction.
 * @param txn_mgr The transaction manager.
 * @return An optional vector of undo logs to pass to ReconstructTuple(). std::nullopt if the tuple did not exist at the
 * time.
 */
auto CollectUndoLogs(RID rid, const TupleMeta &base_meta, const Tuple &base_tuple, std::optional<UndoLink> undo_link,
                     Transaction *txn, TransactionManager *txn_mgr) -> std::optional<std::vector<UndoLog>> {
  std::vector<UndoLog> logs;
  if (base_meta.ts_ <= txn->GetReadTs() || base_meta.ts_ == txn->GetTransactionTempTs()) {
    return logs;
  }
  while (undo_link.has_value() && undo_link->IsValid()) {
    auto log = txn_mgr->GetUndoLog(*undo_link);
    logs.push_back(log);
    if (log.ts_ <= txn->GetReadTs()) {
      return logs;
    }
    undo_link = log.prev_version_;
  }
  return std::nullopt;
}

/**
 * @brief Generates a new undo log as the transaction tries to modify this tuple at the first time.
 *
 * @param schema The schema of the table.
 * @param base_tuple The base tuple before the update, the one retrieved from the table heap. nullptr if the tuple is
 * deleted.
 * @param target_tuple The target tuple after the update. nullptr if this is a deletion.
 * @param ts The timestamp of the base tuple.
 * @param prev_version The undo link to the latest undo log of this tuple.
 * @return The generated undo log.
 */
auto GenerateNewUndoLog(const Schema *schema, const Tuple *base_tuple, const Tuple *target_tuple, timestamp_t ts,
                        UndoLink prev_version) -> UndoLog {
  UndoLog log{base_tuple == nullptr, std::vector<bool>(schema->GetColumnCount(), false), Tuple::Empty(), ts,
              prev_version};
  std::vector<Value> values;
  std::vector<Column> columns;
  if (base_tuple != nullptr) {
    for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
      auto value = base_tuple->GetValue(schema, i);
      if (target_tuple == nullptr || !value.CompareExactlyEquals(target_tuple->GetValue(schema, i))) {
        log.modified_fields_[i] = true;
        columns.push_back(schema->GetColumn(i));
        values.push_back(value);
      }
    }
  }
  Schema undo_schema(columns);
  log.tuple_ = Tuple(values, &undo_schema);
  return log;
}

/**
 * @brief Generate the updated undo log to replace the old one, whereas the tuple is already modified by this txn once.
 *
 * @param schema The schema of the table.
 * @param base_tuple The base tuple before the update, the one retrieved from the table heap. nullptr if the tuple is
 * deleted.
 * @param target_tuple The target tuple after the update. nullptr if this is a deletion.
 * @param log The original undo log.
 * @return The updated undo log.
 */
auto GenerateUpdatedUndoLog(const Schema *schema, const Tuple *base_tuple, const Tuple *target_tuple,
                            const UndoLog &log) -> UndoLog {
  if (log.is_deleted_) {
    return log;
  }
  auto updated = log;
  std::vector<Column> old_columns;
  for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
    if (log.modified_fields_[i]) {
      old_columns.push_back(schema->GetColumn(i));
    }
  }
  Schema old_schema(old_columns);
  std::vector<Column> columns;
  std::vector<Value> values;
  uint32_t old_index = 0;
  for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
    if (log.modified_fields_[i]) {
      columns.push_back(schema->GetColumn(i));
      values.push_back(log.tuple_.GetValue(&old_schema, old_index++));
    } else if (base_tuple != nullptr &&
               (target_tuple == nullptr ||
                !base_tuple->GetValue(schema, i).CompareExactlyEquals(target_tuple->GetValue(schema, i)))) {
      updated.modified_fields_[i] = true;
      columns.push_back(schema->GetColumn(i));
      values.push_back(base_tuple->GetValue(schema, i));
    }
  }
  Schema undo_schema(columns);
  updated.tuple_ = Tuple(values, &undo_schema);
  return updated;
}

void ThrowWriteConflict(Transaction *txn) {
  txn->SetTainted();
  throw ExecutionException("MVCC write conflict or duplicate primary key");
}

void ModifyTuple(TransactionManager *txn_mgr, Transaction *txn, const TableInfo *table_info, RID rid,
                 const Tuple *target, bool require_deleted) {
  auto *heap = table_info->table_.get();
  auto guard = heap->AcquireTablePageWriteLock(rid);
  auto *page = guard.AsMut<TablePage>();
  auto [meta, base] = heap->GetTupleWithLockAcquired(rid, page);
  if ((meta.ts_ != txn->GetTransactionTempTs() && meta.ts_ > txn->GetReadTs()) ||
      (require_deleted && !meta.is_deleted_)) {
    ThrowWriteConflict(txn);
  }
  auto link = txn_mgr->GetUndoLink(rid);
  auto *before = meta.is_deleted_ ? nullptr : &base;
  if (meta.ts_ == txn->GetTransactionTempTs()) {
    if (link.has_value() && link->prev_txn_ == txn->GetTransactionId()) {
      auto log = txn->GetUndoLog(link->prev_log_idx_);
      txn->ModifyUndoLog(link->prev_log_idx_, GenerateUpdatedUndoLog(&table_info->schema_, before, target, log));
    }
  } else {
    link = txn->AppendUndoLog(
        GenerateNewUndoLog(&table_info->schema_, before, target, meta.ts_, link.value_or(UndoLink{})));
  }
  heap->UpdateTupleInPlaceWithLockAcquired({txn->GetTransactionTempTs(), target == nullptr},
                                           target == nullptr ? base : *target, rid, page);
  txn_mgr->UpdateUndoLink(rid, link);
  txn->AppendWriteSet(table_info->oid_, rid);
}

void TxnMgrDbg(const std::string &info, TransactionManager *txn_mgr, const TableInfo *table_info,
               TableHeap *table_heap) {
  fmt::println(stderr, "debug_hook: {}", info);
  for (auto iter = table_heap->MakeIterator(); !iter.IsEnd(); ++iter) {
    auto rid = iter.GetRID();
    auto [meta, tuple, link] = GetTupleAndUndoLink(txn_mgr, table_heap, rid);
    fmt::println(stderr, "RID={}/{} ts={} deleted={} tuple={}", rid.GetPageId(), rid.GetSlotNum(), meta.ts_,
                 meta.is_deleted_, tuple.ToString(&table_info->schema_));
    while (link.has_value() && link->IsValid()) {
      auto log = txn_mgr->GetUndoLogOptional(*link);
      if (!log.has_value()) {
        break;
      }
      fmt::println(stderr, "  txn={} log={} ts={} deleted={}", link->prev_txn_ ^ TXN_START_ID, link->prev_log_idx_,
                   log->ts_, log->is_deleted_);
      link = log->prev_version_;
    }
  }
}

}  // namespace bustub
