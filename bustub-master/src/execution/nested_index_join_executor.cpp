//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_index_join_executor.cpp
//
// Identification: src/execution/nested_index_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_index_join_executor.h"
#include "common/config.h"
#include "common/exception.h"
#include "common/macros.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Creates a new nested index join executor.
 * @param exec_ctx the context that the nested index join should be performed in
 * @param plan the nested index join plan to be executed
 * @param child_executor the outer table
 */
NestedIndexJoinExecutor::NestedIndexJoinExecutor(ExecutorContext *exec_ctx, const NestedIndexJoinPlanNode *plan,
                                                 std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      child_executor_(std::move(child_executor)),
      table_info_(exec_ctx->GetCatalog()->GetTable(plan->GetInnerTableOid()).get()),
      index_info_(exec_ctx->GetCatalog()->GetIndex(plan->GetIndexOid()).get()),
      tree_(dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(index_info_->index_.get())) {
  if (plan->GetJoinType() != JoinType::LEFT && plan->GetJoinType() != JoinType::INNER) {
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void NestedIndexJoinExecutor::Init() {
  child_executor_->Init();
  outer_tuples_.clear();
  outer_rids_.clear();
  outer_idx_ = 0;
}

auto NestedIndexJoinExecutor::BuildJoinTuple(const Tuple &outer_tuple, const Tuple *inner_tuple) -> Tuple {
  const auto &outer_schema = child_executor_->GetOutputSchema();
  const auto &inner_schema = plan_->InnerTableSchema();

  std::vector<Value> values;
  values.reserve(outer_schema.GetColumnCount() + inner_schema.GetColumnCount());
  for (uint32_t i = 0; i < outer_schema.GetColumnCount(); i++) {
    values.push_back(outer_tuple.GetValue(&outer_schema, i));
  }
  for (uint32_t i = 0; i < inner_schema.GetColumnCount(); i++) {
    if (inner_tuple != nullptr) {
      values.push_back(inner_tuple->GetValue(&inner_schema, i));
    } else {
      values.push_back(ValueFactory::GetNullValueByType(inner_schema.GetColumn(i).GetType()));
    }
  }
  return {values, &GetOutputSchema()};
}

auto NestedIndexJoinExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                                   size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  const auto &outer_schema = child_executor_->GetOutputSchema();

  while (tuple_batch->size() < batch_size) {
    if (outer_idx_ >= outer_tuples_.size()) {
      if (!child_executor_->Next(&outer_tuples_, &outer_rids_, BUSTUB_BATCH_SIZE)) {
        break;
      }
      outer_idx_ = 0;
    }
    const auto &outer_tuple = outer_tuples_[outer_idx_++];

    auto key_value = plan_->KeyPredicate()->Evaluate(&outer_tuple, outer_schema);
    bool matched = false;
    if (!key_value.IsNull()) {
      Tuple key_tuple(std::vector<Value>{key_value}, index_info_->index_->GetKeySchema());
      std::vector<RID> result_rids;
      tree_->ScanKey(key_tuple, &result_rids, exec_ctx_->GetTransaction());
      for (const auto &rid : result_rids) {
        auto [meta, inner_tuple] = table_info_->table_->GetTuple(rid);
        if (meta.is_deleted_) {
          continue;
        }
        tuple_batch->push_back(BuildJoinTuple(outer_tuple, &inner_tuple));
        rid_batch->emplace_back(RID{});
        matched = true;
      }
    }

    if (!matched && plan_->GetJoinType() == JoinType::LEFT) {
      tuple_batch->push_back(BuildJoinTuple(outer_tuple, nullptr));
      rid_batch->emplace_back(RID{});
    }
  }

  return !tuple_batch->empty();
}

}  // namespace bustub
