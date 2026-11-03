#include "graph_test/private_graph_test.h"
#include <format>

namespace lbug {
namespace testing {

class FTSInterruptedCreateTest : public EmptyDBTest {
public:
    void SetUp() override {
        EmptyDBTest::SetUp();
        createDBAndConn();
#ifndef __STATIC_LINK_EXTENSION_TEST__
        ASSERT_TRUE(conn
                ->query(std::format("LOAD EXTENSION '{}'",
                    TestHelper::appendLbugRootPath("extension/fts/build/libfts.lbug_extension")))
                ->isSuccess());
#endif
    }

    std::vector<std::string> search(const std::string& words) {
        auto result = conn->query(std::format(
            "CALL QUERY_FTS_INDEX('doc', 'docIdx', '{}') RETURN node.id ORDER BY node.id", words));
        EXPECT_TRUE(result->isSuccess()) << result->getErrorMessage();
        return TestHelper::convertResultToString(*result);
    }
};

// CREATE_FTS_INDEX runs as many statements, each committed on its own, and writes the index's
// catalog entry last. A crash or failure in between leaves internal tables without an index, and
// creating the index again drops them, whichever statement it stopped after. The leftovers are
// made here by committing the statements that change the schema, which decide what's left; a
// crash leaves the same, since replay keeps tables internal.
TEST_F(FTSInterruptedCreateTest, CreateAgainDropsWhatAnInterruptedCreateLeft) {
    ASSERT_TRUE(
        conn->query("CREATE NODE TABLE doc(id INT64, text STRING, PRIMARY KEY(id))")->isSuccess());
    ASSERT_TRUE(
        conn->query("CREATE (:doc {id: 1, text: 'kept words'}), "
                    "(:doc {id: 2, text: 'more words'})")
            ->isSuccess());
    // CREATE_FTS_INDEX's statements that change the schema, in its order, for an index on table 0
    // that keeps stopwords in a table of its own.
    const std::vector<std::string> statements{
        "CREATE MACRO `0_docIdx_tokenize`(query) AS lower(query)",
        "CREATE NODE TABLE `0_docIdx_stopwords` (sw STRING, PRIMARY KEY(sw))",
        "CREATE NODE TABLE `0_docIdx_appears_info` (ID SERIAL, term STRING, docID INT64, "
        "PRIMARY KEY(ID))",
        "CREATE NODE TABLE `0_docIdx_docs` (docID INT64, len UINT64, PRIMARY KEY(docID))",
        "CREATE NODE TABLE `0_docIdx_terms` (term STRING, df UINT64, PRIMARY KEY(term))",
        "CREATE REL TABLE `0_docIdx_appears_in` (FROM `0_docIdx_terms` TO `0_docIdx_docs`, "
        "tf UINT64)",
        "DROP TABLE `0_docIdx_appears_info`",
    };
    for (auto done = 1u; done <= statements.size(); done++) {
        for (auto i = 0u; i < done; i++) {
            // Each query resets this.
            conn->getClientContext()->setUseInternalCatalogEntry(true);
            auto result = conn->query(statements[i]);
            ASSERT_TRUE(result->isSuccess()) << statements[i] << ": " << result->getErrorMessage();
        }

        auto created =
            conn->query("CALL CREATE_FTS_INDEX('doc', 'docIdx', ['text'], stopWords := 'none')");
        ASSERT_TRUE(created->isSuccess())
            << "after " << statements[done - 1] << ": " << created->getErrorMessage();
        EXPECT_EQ(search("words"), (std::vector<std::string>{"1", "2"}));
        ASSERT_TRUE(conn->query("CALL DROP_FTS_INDEX('doc', 'docIdx')")->isSuccess());
    }
}

} // namespace testing
} // namespace lbug
