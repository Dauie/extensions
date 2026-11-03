#include "catalog/catalog.h"
#include "catalog/fts_index_catalog_entry.h"
#include "function/fts_config.h"
#include "graph_test/private_graph_test.h"
#include "main/connection.h"
#include "transaction/transaction.h"

namespace lbug {
namespace testing {

// Indexes the extension can't use when the database opens, and how they're rebuilt.
class FTSRecoveryTest : public DBTest {
public:
    void SetUp() override {
        BaseGraphTest::SetUp();
        systemConfig->autoCheckpoint = false;
        // Lets a second connection hold a write transaction open, which a checkpoint waits for.
        systemConfig->enableMultiWrites = true;
        // Short, so that the checkpoint gives up quickly.
        createDB(10000 /* checkpointWaitTimeout, in microseconds */);
        conn = std::make_unique<main::Connection>(database.get());
    }

    std::string getInputDir() override { UNREACHABLE_CODE; }

    void createDocs() {
        ASSERT_TRUE(conn->query("CREATE NODE TABLE doc(id INT64, text STRING, PRIMARY KEY(id))")
                ->isSuccess());
        ASSERT_TRUE(
            conn->query("CREATE (:doc {id: 1, text: 'alice runs'}), "
                        "(:doc {id: 2, text: 'bob walks'})")
                ->isSuccess());
    }

    // CREATE_FTS_INDEX commits and then forces a checkpoint. A crash between the two leaves the
    // index's catalog entry in the WAL, and replay restores the entry but not the index.
    void createIndexThenCrashBeforeItsCheckpoint() {
        createDocs();
        ASSERT_TRUE(conn->query("CREATE NODE TABLE other(id INT64, PRIMARY KEY(id))")->isSuccess());
        auto writer = std::make_unique<main::Connection>(database.get());
        ASSERT_TRUE(writer->query("BEGIN TRANSACTION")->isSuccess());
        ASSERT_TRUE(writer->query("CREATE (:other {id: 1})")->isSuccess());
        // The forced checkpoint times out waiting for the writer, after the commit.
        auto result = conn->query("CALL CREATE_FTS_INDEX('doc', 'docIdx', ['text'])");
        ASSERT_FALSE(result->isSuccess());
        ASSERT_TRUE(result->getErrorMessage().starts_with(
            "Transaction committed successfully, but the post-commit checkpoint failed."));
        ASSERT_TRUE(writer->query("ROLLBACK")->isSuccess());
        writer.reset();
        // Maintained by the index in memory, which is lost below.
        ASSERT_TRUE(conn->query("CREATE (:doc {id: 3, text: 'carol runs'})")->isSuccess());
        // Close without a checkpoint, as a crash would, so the WAL is replayed on open.
        ASSERT_TRUE(conn->query("CALL force_checkpoint_on_close=false")->isSuccess());
        systemConfig->enableMultiWrites = false;
        createDBAndConn();
    }

    void expectIndexNeedsRebuild() {
        auto result = conn->query("CALL SHOW_INDEXES() WHERE index_name = 'docIdx' "
                                  "RETURN extension_loaded");
        ASSERT_TRUE(result->isSuccess()) << result->getErrorMessage();
        ASSERT_EQ(TestHelper::convertResultToString(*result), std::vector<std::string>{"False"});
        result = conn->query("CALL QUERY_FTS_INDEX('doc', 'docIdx', 'runs') RETURN node.id");
        ASSERT_FALSE(result->isSuccess());
        ASSERT_EQ(result->getErrorMessage(), "Binder exception: Index docIdx on table doc needs to "
                                             "be rebuilt. Drop it and create it again.");
        result = conn->query("CREATE (:doc {id: 4, text: 'dave runs'})");
        ASSERT_FALSE(result->isSuccess());
        ASSERT_EQ(result->getErrorMessage(), "Binder exception: Trying to insert into an index on "
                                             "table doc but its extension is not loaded.");
    }

    void expectRebuildFindsEveryDocument() {
        ASSERT_TRUE(conn->query("CALL DROP_FTS_INDEX('doc', 'docIdx')")->isSuccess());
        ASSERT_TRUE(conn->query("CALL CREATE_FTS_INDEX('doc', 'docIdx', ['text'])")->isSuccess());
        ASSERT_TRUE(conn->query("CREATE (:doc {id: 4, text: 'dave runs'})")->isSuccess());
        auto result = conn->query(
            "CALL QUERY_FTS_INDEX('doc', 'docIdx', 'runs') RETURN node.id ORDER BY node.id");
        ASSERT_TRUE(result->isSuccess()) << result->getErrorMessage();
        ASSERT_EQ(TestHelper::convertResultToString(*result),
            (std::vector<std::string>{"1", "3", "4"}));
    }
};

TEST_F(FTSRecoveryTest, IndexLostByACrashCanBeRebuiltAfterReplay) {
    if (inMemMode) {
        GTEST_SKIP();
    }
    createIndexThenCrashBeforeItsCheckpoint();
    expectIndexNeedsRebuild();
    expectRebuildFindsEveryDocument();
}

// Once the replayed entry is checkpointed, opening used to fail on an assertion, so the
// database couldn't be opened at all.
TEST_F(FTSRecoveryTest, IndexLostByACrashCanBeRebuiltAfterACheckpoint) {
    if (inMemMode) {
        GTEST_SKIP();
    }
    createIndexThenCrashBeforeItsCheckpoint();
    ASSERT_TRUE(conn->query("CHECKPOINT")->isSuccess());
    createDBAndConn();
    expectIndexNeedsRebuild();
    expectRebuildFindsEveryDocument();
}

// An index built by another version of the analysis holds terms this build wouldn't produce, so
// deleting or updating its documents would fail.
TEST_F(FTSRecoveryTest, IndexBuiltByAnotherAnalysisVersionMustBeRebuilt) {
    if (inMemMode) {
        GTEST_SKIP();
    }
    createDocs();
    ASSERT_TRUE(conn->query("CREATE (:doc {id: 3, text: 'carol runs'})")->isSuccess());
    ASSERT_TRUE(conn->query("CALL CREATE_FTS_INDEX('doc', 'docIdx', ['text'])")->isSuccess());
    // Stand in for an index written by another build: change the version this one stores.
    ASSERT_TRUE(conn->query("BEGIN TRANSACTION READ ONLY")->isSuccess());
    auto catalog = database->getCatalog();
    auto transaction = getActiveTransaction(*conn);
    auto tableID = catalog->getTableCatalogEntry(transaction, "doc")->getTableID();
    catalog->getIndex(transaction, tableID, "docIdx")
        ->getAuxInfoUnsafe()
        .cast<fts_extension::FTSIndexAuxInfo>()
        .config.analysisVersion = fts_extension::FTSConfig::ANALYSIS_VERSION + 1;
    ASSERT_TRUE(conn->query("COMMIT")->isSuccess());
    // A catalog change makes the checkpoint write the catalog, that entry included.
    ASSERT_TRUE(conn->query("CREATE NODE TABLE marker(id INT64, PRIMARY KEY(id))")->isSuccess());
    ASSERT_TRUE(conn->query("CHECKPOINT")->isSuccess());
    createDBAndConn();
    expectIndexNeedsRebuild();
    expectRebuildFindsEveryDocument();
}

#ifndef LBUG_FTS_JIEBA
// A build without jieba can't maintain an index another build made with it.
TEST_F(FTSRecoveryTest, JiebaIndexMustBeRebuiltWithoutJieba) {
    if (inMemMode) {
        GTEST_SKIP();
    }
    createDocs();
    ASSERT_TRUE(conn->query("CREATE (:doc {id: 3, text: 'carol runs'})")->isSuccess());
    ASSERT_TRUE(conn->query("CALL CREATE_FTS_INDEX('doc', 'docIdx', ['text'])")->isSuccess());
    // Stand in for an index written by a build with jieba.
    ASSERT_TRUE(conn->query("BEGIN TRANSACTION READ ONLY")->isSuccess());
    auto catalog = database->getCatalog();
    auto transaction = getActiveTransaction(*conn);
    auto tableID = catalog->getTableCatalogEntry(transaction, "doc")->getTableID();
    catalog->getIndex(transaction, tableID, "docIdx")
        ->getAuxInfoUnsafe()
        .cast<fts_extension::FTSIndexAuxInfo>()
        .config.tokenizer = "jieba";
    ASSERT_TRUE(conn->query("COMMIT")->isSuccess());
    ASSERT_TRUE(conn->query("CREATE NODE TABLE marker(id INT64, PRIMARY KEY(id))")->isSuccess());
    ASSERT_TRUE(conn->query("CHECKPOINT")->isSuccess());
    createDBAndConn();
    expectIndexNeedsRebuild();
    expectRebuildFindsEveryDocument();
}
#endif

} // namespace testing
} // namespace lbug
