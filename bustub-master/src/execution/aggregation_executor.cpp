//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// aggregation_executor.cpp
//
// Identification: src/execution/aggregation_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include "common/macros.h"

#include "execution/executors/aggregation_executor.h"

namespace bustub {

/**
 * Construct a new AggregationExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The insert plan to be executed
 * @param child_executor The child executor from which inserted tuples are pulled (may be `nullptr`)
 */
AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                         std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx) {
  // HINT: store `plan_`, move in `child_executor_`, and construct `aht_` from
  // `plan->GetAggregates()` / `plan->GetAggregateTypes()`. `aht_iterator_` needs a valid initial
  // value too (e.g. `aht_.Begin()`, called after `aht_` is constructed).
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/** Initialize the aggregation */
void AggregationExecutor::Init() {
  // HINT: this is the pipeline-breaker's build phase (see hint in aggregation_executor.h) —
  // `child_executor_->Init()`, `aht_.Clear()`, then loop pulling batches via `child_executor_->Next()`
  // and call `aht_.InsertCombine(MakeAggregateKey(&tuple), MakeAggregateValue(&tuple))` for every
  // tuple. Handle the "no GROUP BY + empty input" edge case, then reset `aht_iterator_ = aht_.Begin()`.
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/**
 * Yield the next tuple batch from the aggregation.
 * @param[out] tuple_batch The next batch of tuples produced by the aggregation
 * @param[out] rid_batch The next batch of tuple RIDs produced by the aggregation
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if any tuples were produced, `false` if there are no more tuples
 */

auto AggregationExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                               size_t batch_size) -> bool {
  // HINT: since the build phase already happened in Init(), Next() just walks `aht_iterator_`
  // until `aht_.End()` (or `batch_size` is reached), building each output tuple from
  // `aht_iterator_.Key().group_bys_` followed by `aht_iterator_.Val().aggregates_`, then
  // `++aht_iterator_`. Remember to `tuple_batch->clear()` / `rid_batch->clear()` first, and push a
  // dummy `RID{}` per output row (rid_batch is otherwise unused for this executor).
  UNIMPLEMENTED("TODO(P3): Add implementation.");
}

/** Do not use or remove this function; otherwise, you will get zero points. */
auto AggregationExecutor::GetChildExecutor() const -> const AbstractExecutor * { return child_executor_.get(); }

}  // namespace bustub
