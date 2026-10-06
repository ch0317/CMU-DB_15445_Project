//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.cpp
//
// Identification: src/execution/hash_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/hash_join_executor.h"
#include "common/config.h"
#include "common/macros.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Construct a new HashJoinExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The HashJoin join plan to be executed
 * @param left_child The child executor that produces tuples for the left side of join
 * @param right_child The child executor that produces tuples for the right side of join
 */
HashJoinExecutor::HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                                   std::unique_ptr<AbstractExecutor> &&left_child,
                                   std::unique_ptr<AbstractExecutor> &&right_child)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_executor_(std::move(left_child)),
      right_executor_(std::move(right_child)) {
  if (plan->GetJoinType() != JoinType::LEFT && plan->GetJoinType() != JoinType::INNER) {
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

/** Initialize the join */
void HashJoinExecutor::Init() {
  left_executor_->Init();
  right_executor_->Init();

  // Build phase: hash every right (build side) tuple by its join key.
  hash_table_.clear();
  std::vector<Tuple> right_tuples;
  std::vector<RID> right_rids;
  while (right_executor_->Next(&right_tuples, &right_rids, BUSTUB_BATCH_SIZE)) {
    for (auto &tuple : right_tuples) {
      auto key = MakeJoinKey(&tuple, right_executor_->GetOutputSchema(), plan_->RightJoinKeyExpressions());
      hash_table_[key].push_back(tuple);
    }
  }

  left_tuples_.clear();
  left_rids_.clear();
  left_idx_ = 0;
  has_current_left_ = false;
}

auto HashJoinExecutor::MakeJoinKey(const Tuple *tuple, const Schema &schema,
                                   const std::vector<AbstractExpressionRef> &key_exprs) -> HashJoinKey {
  std::vector<Value> values;
  values.reserve(key_exprs.size());
  for (const auto &expr : key_exprs) {
    values.push_back(expr->Evaluate(tuple, schema));
  }
  return {values};
}

auto HashJoinExecutor::BuildJoinTuple(const Tuple &left_tuple, const Tuple *right_tuple) -> Tuple {
  const auto &left_schema = left_executor_->GetOutputSchema();
  const auto &right_schema = right_executor_->GetOutputSchema();

  std::vector<Value> values;
  values.reserve(left_schema.GetColumnCount() + right_schema.GetColumnCount());
  for (uint32_t i = 0; i < left_schema.GetColumnCount(); i++) {
    values.push_back(left_tuple.GetValue(&left_schema, i));
  }
  for (uint32_t i = 0; i < right_schema.GetColumnCount(); i++) {
    if (right_tuple != nullptr) {
      values.push_back(right_tuple->GetValue(&right_schema, i));
    } else {
      values.push_back(ValueFactory::GetNullValueByType(right_schema.GetColumn(i).GetType()));
    }
  }
  return {values, &GetOutputSchema()};
}

/**
 * Yield the next tuple batch from the hash join.
 * @param[out] tuple_batch The next tuple batch produced by the hash join
 * @param[out] rid_batch The next tuple RID batch produced by the hash join
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto HashJoinExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                            size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  while (tuple_batch->size() < batch_size) {
    if (!has_current_left_) {
      if (left_idx_ >= left_tuples_.size()) {
        if (!left_executor_->Next(&left_tuples_, &left_rids_, BUSTUB_BATCH_SIZE)) {
          break;
        }
        left_idx_ = 0;
      }
      current_left_tuple_ = left_tuples_[left_idx_++];

      auto key = MakeJoinKey(&current_left_tuple_, left_executor_->GetOutputSchema(), plan_->LeftJoinKeyExpressions());
      auto it = hash_table_.find(key);
      current_matches_ = (it != hash_table_.end()) ? &it->second : nullptr;
      match_idx_ = 0;
      current_match_found_ = false;
      has_current_left_ = true;
    }

    if (current_matches_ != nullptr && match_idx_ < current_matches_->size()) {
      const auto &right_tuple = (*current_matches_)[match_idx_++];
      tuple_batch->push_back(BuildJoinTuple(current_left_tuple_, &right_tuple));
      rid_batch->emplace_back(RID{});
      current_match_found_ = true;
      continue;
    }

    if (!current_match_found_ && plan_->GetJoinType() == JoinType::LEFT) {
      tuple_batch->push_back(BuildJoinTuple(current_left_tuple_, nullptr));
      rid_batch->emplace_back(RID{});
    }
    has_current_left_ = false;
  }

  return !tuple_batch->empty();
}

}  // namespace bustub
