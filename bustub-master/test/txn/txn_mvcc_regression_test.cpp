// Additional MVCC regressions covering OCC phantom validation and version history.
#include <memory>
#include <string>
#include <vector>

#include "common/bustub_instance.h"
#include "concurrency/transaction_manager.h"
#include "gtest/gtest.h"
#include "txn_common.h"  // NOLINT

namespace bustub {

TEST(TxnMvccRegressionTest, SerializableVersionTransitions) {
  struct Scenario {
    std::vector<std::string> writes;
    bool succeeds;
  };
  const std::vector<Scenario> scenarios{{{"INSERT INTO t VALUES (3, 1)"}, false},
                                        {{"DELETE FROM t WHERE k = 1"}, false},
                                        {{"UPDATE t SET v = 2 WHERE k = 1"}, false},
                                        {{"UPDATE t SET v = 1 WHERE k = 2"}, false},
                                        {{"UPDATE t SET v = 2 WHERE k = 1", "UPDATE t SET v = 1 WHERE k = 1"}, false},
                                        {{"INSERT INTO t VALUES (3, 1)", "DELETE FROM t WHERE k = 3"}, true},
                                        {{"UPDATE t SET v = 2 WHERE k = 2"}, true}};
  for (const auto &scenario : scenarios) {
    auto db = std::make_unique<BusTubInstance>();
    Execute(*db, "CREATE TABLE t(k int primary key, v int)");
    Execute(*db, "INSERT INTO t VALUES (1, 1), (2, 0)");
    auto *reader = db->txn_manager_->Begin(IsolationLevel::SERIALIZABLE);
    NoopWriter writer;
    ASSERT_TRUE(db->ExecuteSqlTxn("SELECT * FROM t WHERE v = 1", writer, reader));
    ASSERT_TRUE(db->ExecuteSqlTxn("INSERT INTO t VALUES (99, 0)", writer, reader));
    auto *other = db->txn_manager_->Begin(IsolationLevel::SERIALIZABLE);
    for (const auto &sql : scenario.writes) {
      ASSERT_TRUE(db->ExecuteSqlTxn(sql, writer, other));
    }
    ASSERT_TRUE(db->txn_manager_->Commit(other));
    EXPECT_EQ(db->txn_manager_->Commit(reader), scenario.succeeds);
    EXPECT_EQ(reader->GetTransactionState(),
              scenario.succeeds ? TransactionState::COMMITTED : TransactionState::ABORTED);
  }
}

TEST(TxnMvccRegressionTest, SerializableIntermediateCommittedVersion) {
  auto db = std::make_unique<BusTubInstance>();
  Execute(*db, "CREATE TABLE t(k int primary key, v int)");
  Execute(*db, "INSERT INTO t VALUES (1, 1)");
  auto *reader = db->txn_manager_->Begin(IsolationLevel::SERIALIZABLE);
  NoopWriter writer;
  ASSERT_TRUE(db->ExecuteSqlTxn("SELECT * FROM t WHERE v = 2", writer, reader));
  ASSERT_TRUE(db->ExecuteSqlTxn("INSERT INTO t VALUES (99, 0)", writer, reader));
  Execute(*db, "UPDATE t SET v = 2 WHERE k = 1");
  Execute(*db, "UPDATE t SET v = 1 WHERE k = 1");
  EXPECT_FALSE(db->txn_manager_->Commit(reader));
}

TEST(TxnMvccRegressionTest, SerializableIndexPhantomSurvivesGc) {
  auto db = std::make_unique<BusTubInstance>();
  Execute(*db, "CREATE TABLE t(k int primary key, v int)");
  auto *reader = db->txn_manager_->Begin(IsolationLevel::SERIALIZABLE);
  NoopWriter writer;
  ASSERT_TRUE(db->ExecuteSqlTxn("SELECT * FROM t WHERE k = 3", writer, reader));
  ASSERT_TRUE(db->ExecuteSqlTxn("INSERT INTO t VALUES (99, 0)", writer, reader));
  Execute(*db, "INSERT INTO t VALUES (3, 1)");
  db->txn_manager_->GarbageCollection();
  EXPECT_FALSE(db->txn_manager_->Commit(reader));
  EXPECT_EQ(reader->GetTransactionState(), TransactionState::ABORTED);
}

TEST(TxnMvccRegressionTest, SerializableSkipsUncommittedVersion) {
  auto db = std::make_unique<BusTubInstance>();
  Execute(*db, "CREATE TABLE t(k int primary key, v int)");
  Execute(*db, "INSERT INTO t VALUES (1, 0)");
  auto *reader = db->txn_manager_->Begin(IsolationLevel::SERIALIZABLE);
  NoopWriter writer;
  ASSERT_TRUE(db->ExecuteSqlTxn("SELECT * FROM t WHERE v = 1", writer, reader));
  ASSERT_TRUE(db->ExecuteSqlTxn("INSERT INTO t VALUES (99, 0)", writer, reader));
  Execute(*db, "UPDATE t SET v = 2 WHERE k = 1");
  auto *active = db->txn_manager_->Begin(IsolationLevel::SERIALIZABLE);
  ASSERT_TRUE(db->ExecuteSqlTxn("UPDATE t SET v = 1 WHERE k = 1", writer, active));
  EXPECT_TRUE(db->txn_manager_->Commit(reader));
  db->txn_manager_->Abort(active);
}

TEST(TxnMvccRegressionTest, PrimaryKeyMoveAbortPreservesOldSnapshot) {
  auto db = std::make_unique<BusTubInstance>();
  Execute(*db, "CREATE TABLE t(k int primary key, v int)");
  Execute(*db, "INSERT INTO t VALUES (1, 10), (2, 20), (3, 30)");
  auto *reader = db->txn_manager_->Begin();
  auto *mover = db->txn_manager_->Begin();
  NoopWriter writer;
  ASSERT_TRUE(db->ExecuteSqlTxn("UPDATE t SET k = k + 1", writer, mover));
  db->txn_manager_->Abort(mover);
  QueryShowResult(*db, "reader", reader, "SELECT * FROM t", IntResult{{1, 10}, {2, 20}, {3, 30}});
  ASSERT_TRUE(db->txn_manager_->Commit(reader));
  db->txn_manager_->GarbageCollection();
  auto *fresh = db->txn_manager_->Begin();
  QueryShowResult(*db, "fresh", fresh, "SELECT * FROM t WHERE k = 2", IntResult{{2, 20}});
  ASSERT_TRUE(db->txn_manager_->Commit(fresh));
}

TEST(TxnMvccRegressionTest, LimitAcrossBatchBoundaries) {
  auto db = std::make_unique<BusTubInstance>();
  Execute(*db, "CREATE TABLE t(k int)");
  std::string sql = "INSERT INTO t VALUES ";
  IntResult expected;
  for (int i = 0; i < 130; i++) {
    if (i != 0) {
      sql += ", ";
    }
    sql += "(" + std::to_string(i) + ")";
    expected.push_back({i});
  }
  Execute(*db, sql);
  auto *txn = db->txn_manager_->Begin();
  for (size_t limit : {0U, 1U, 65U, 130U, 200U}) {
    auto count = std::min(limit, expected.size());
    QueryShowResult(*db, "reader", txn, "SELECT k FROM t LIMIT " + std::to_string(limit),
                    IntResult(expected.begin(), expected.begin() + count));
  }
  ASSERT_TRUE(db->txn_manager_->Commit(txn));
}

}  // namespace bustub
