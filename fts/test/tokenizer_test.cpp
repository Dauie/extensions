#include "graph_test/private_graph_test.h"
#include <format>

namespace lbug {
namespace testing {

class FTSTokenizerTest : public EmptyDBTest {
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
        ASSERT_TRUE(conn->query("CREATE NODE TABLE doc(id INT64, text STRING, PRIMARY KEY(id))")
                ->isSuccess());
    }
};

// jieba reads its dictionaries straight from files, so a build can leave it out (FTS_JIEBA).
TEST_F(FTSTokenizerTest, JiebaIsAvailableOnlyWhenBuilt) {
    auto plain = conn->query("CALL CREATE_FTS_INDEX('doc', 'plainIdx', ['text'])");
    ASSERT_TRUE(plain->isSuccess()) << plain->getErrorMessage();
    auto created = conn->query("CALL CREATE_FTS_INDEX('doc', 'docIdx', ['text'], "
                               "tokenizer := 'jieba', stemmer := 'none')");
    auto tokenized = conn->query("RETURN TOKENIZE('natural language', 'jieba', '')");
#ifdef LBUG_FTS_JIEBA
    ASSERT_TRUE(created->isSuccess()) << created->getErrorMessage();
    ASSERT_TRUE(tokenized->isSuccess()) << tokenized->getErrorMessage();
#else
    const std::string refused = "Binder exception: This build has no jieba tokenizer.";
    ASSERT_EQ(created->getErrorMessage(), refused);
    ASSERT_EQ(tokenized->getErrorMessage(), refused);
    auto withDictDir =
        conn->query("CALL CREATE_FTS_INDEX('doc', 'docIdx', ['text'], jieba_dict_dir := 'dict')");
    ASSERT_EQ(withDictDir->getErrorMessage(), refused);
    auto unsupported =
        conn->query("CALL CREATE_FTS_INDEX('doc', 'docIdx', ['text'], tokenizer := 'other')");
    ASSERT_EQ(unsupported->getErrorMessage(), "Binder exception: Unsupported tokenizer: other.\n"
                                              "Supported tokenizers: 'simple' (default)");
#endif
}

} // namespace testing
} // namespace lbug
