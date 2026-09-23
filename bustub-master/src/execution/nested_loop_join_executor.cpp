//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_loop_join_executor.cpp
//
// Identification: src/execution/nested_loop_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_loop_join_executor.h"
#include "binder/table_ref/bound_join_ref.h"
#include "common/config.h"
#include "common/exception.h"
#include "common/macros.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Construct a new NestedLoopJoinExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The nested loop join plan to be executed
 * @param left_executor The child executor that produces tuple for the left side of join
 * @param right_executor The child executor that produces tuple for the right side of join
 */
NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx, const NestedLoopJoinPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&left_executor,
                                               std::unique_ptr<AbstractExecutor> &&right_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_executor_(std::move(left_executor)),
      right_executor_(std::move(right_executor)) {
  if (plan->GetJoinType() != JoinType::LEFT && plan->GetJoinType() != JoinType::INNER) {
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

/** Initialize the join */
void NestedLoopJoinExecutor::Init() {
  left_executor_->Init();

  left_tuples_.clear();
  left_rids_.clear();
  left_idx_ = 0;
  has_current_left_ = false;
}

auto NestedLoopJoinExecutor::FetchNextRightTuple() -> std::optional<Tuple> {
  while (right_idx_ >= right_tuples_.size()) {
    if (!right_executor_->Next(&right_tuples_, &right_rids_, BUSTUB_BATCH_SIZE)) {
      return std::nullopt;
    }
    right_idx_ = 0;
  }
  return right_tuples_[right_idx_++];
}

auto NestedLoopJoinExecutor::BuildJoinTuple(const Tuple &left_tuple, const Tuple *right_tuple) -> Tuple {
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
 * Yield the next tuple batch from the join.
 * @param[out] tuple_batch The next tuple batch produced by the join
 * @param[out] rid_batch The next tuple RID batch produced by the join
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto NestedLoopJoinExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                                  size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  const auto &predicate = plan_->Predicate();

  while (tuple_batch->size() < batch_size) {
    if (!has_current_left_) {
      if (left_idx_ >= left_tuples_.size()) {
        if (!left_executor_->Next(&left_tuples_, &left_rids_, BUSTUB_BATCH_SIZE)) {
          break;
        }
        left_idx_ = 0;
      }
      current_left_tuple_ = left_tuples_[left_idx_++];
      // Re-scan the right (inner) table from scratch for every left (outer) tuple.
      right_executor_->Init();
      right_tuples_.clear();
      right_rids_.clear();
      right_idx_ = 0;
      current_match_found_ = false;
      has_current_left_ = true;
    }

    auto right_tuple_opt = FetchNextRightTuple();
    if (!right_tuple_opt.has_value()) {
      if (!current_match_found_ && plan_->GetJoinType() == JoinType::LEFT) {
        tuple_batch->push_back(BuildJoinTuple(current_left_tuple_, nullptr));
        rid_batch->emplace_back(RID{});
      }
      has_current_left_ = false;
      continue;
    }

    const auto &right_tuple = *right_tuple_opt;
    bool matches = true;
    if (predicate != nullptr) {
      auto value = predicate->EvaluateJoin(&current_left_tuple_, left_executor_->GetOutputSchema(), &right_tuple,
                                           right_executor_->GetOutputSchema());
      matches = !value.IsNull() && value.GetAs<bool>();
    }
    if (matches) {
      tuple_batch->push_back(BuildJoinTuple(current_left_tuple_, &right_tuple));
      rid_batch->emplace_back(RID{});
      current_match_found_ = true;
    }
  }

  return !tuple_batch->empty();
}

}  // namespace bustub
