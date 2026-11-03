#include "graph_test/private_graph_test.h"
#include <format>

namespace lbug {
namespace testing {

class FTSNoStopWordsTest : public EmptyDBTest {
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

    std::vector<std::string> search(const std::string& words, bool conjunctive = false) {
        auto result = conn->query(std::format("CALL QUERY_FTS_INDEX('doc', 'docIdx', '{}', "
                                              "conjunctive := {}) RETURN node.id ORDER BY node.id",
            words, conjunctive));
        EXPECT_TRUE(result->isSuccess()) << result->getErrorMessage();
        return TestHelper::convertResultToString(*result);
    }
};

// No stemming, no stopwords, and words as runs of letters, marks and digits. Default stopwords
// such as "the" and "first" are kept through every kind of write and query.
TEST_F(FTSNoStopWordsTest, EveryWordIsKept) {
    ASSERT_TRUE(
        conn->query("CREATE NODE TABLE doc(id INT64, text STRING, PRIMARY KEY(id))")->isSuccess());
    ASSERT_TRUE(conn->query("CREATE (:doc {id: 1, text: 'the first note'})")->isSuccess());
    auto created = conn->query("CALL CREATE_FTS_INDEX('doc', 'docIdx', ['text'], "
                               "stemmer := 'none', stopWords := 'none', "
                               "ignore_pattern := '[^\\\\p{L}\\\\p{M}\\\\p{N}]+')");
    ASSERT_TRUE(created->isSuccess()) << created->getErrorMessage();
    ASSERT_TRUE(conn->query("CREATE (:doc {id: 2, text: 'the second, the last'})")->isSuccess());

    EXPECT_EQ(search("the"), (std::vector<std::string>{"1", "2"}));
    EXPECT_EQ(search("the second", true /* conjunctive */), std::vector<std::string>{"2"});

    ASSERT_TRUE(conn->query("MATCH (d:doc {id: 1}) SET d.text = 'other words'")->isSuccess());
    EXPECT_EQ(search("first"), std::vector<std::string>{});
    EXPECT_EQ(search("the"), std::vector<std::string>{"2"});
    EXPECT_EQ(search("other"), std::vector<std::string>{"1"});

    ASSERT_TRUE(conn->query("MATCH (d:doc {id: 2}) DELETE d")->isSuccess());
    EXPECT_EQ(search("the"), std::vector<std::string>{});
}

} // namespace testing
} // namespace lbug
