#pragma once

#include <string>

#include "common/types/types.h"
#include "function/table/bind_input.h"
#include <format>

namespace lbug {
namespace fts_extension {

struct Stemmer {
    static constexpr const char* NAME = "stemmer";
    static constexpr common::LogicalTypeID TYPE = common::LogicalTypeID::STRING;
    static constexpr const char* DEFAULT_VALUE = "english";

    static void validate(const std::string& stemmer);
};

enum class StopWordsSource : uint8_t {
    FILE = 0,
    TABLE = 1,
    DEFAULT = 2,
    NONE = 3,
};

struct StopWordsTableInfo {
    std::string stopWords;
    std::string tableName;
    StopWordsSource source;

    StopWordsTableInfo();
    StopWordsTableInfo(std::string stopWords, std::string tableName, StopWordsSource source)
        : stopWords{std::move(stopWords)}, tableName{std::move(tableName)}, source{source} {}
};

struct StopWords {
    static constexpr const char* NAME = "stopwords";
    static constexpr common::LogicalTypeID TYPE = common::LogicalTypeID::STRING;
    static constexpr const char* DEFAULT_VALUE = "default";
    // Keeps every word. It takes precedence over a node table of the same name.
    static constexpr const char* NONE_VALUE = "none";
    static const std::unordered_set<std::string>& getDefaultStopWords();
    static constexpr uint64_t NUM_DEFAULT_STOP_WORDS = 570;

    static StopWordsTableInfo bind(main::ClientContext& context, common::table_id_t tableID,
        const std::string& indexName, const std::string& stopWords);
};

struct IgnorePattern {
    static constexpr const char* NAME = "ignore_pattern";
    static constexpr common::LogicalTypeID TYPE = common::LogicalTypeID::STRING;
    static constexpr const char* DEFAULT_VALUE = "[0-9!@#$%^&*()_+={}\\[\\]:;<>,.?~\\/\\|'\"`-]+";
    // We store a second ignore pattern for queries to not replace on wildcard characters
    static constexpr const char* DEFAULT_VALUE_QUERY =
        "[0-9!@#$%^&()_+={}\\[\\]:;<>,.~\\/\\|'\"`-]+";

    static void validate(const std::string& ignorePattern);
};

struct Tokenizer {
    static constexpr const char* NAME = "tokenizer";
    static constexpr common::LogicalTypeID TYPE = common::LogicalTypeID::STRING;
    static constexpr const char* DEFAULT_VALUE = "simple";
#ifdef LBUG_FTS_JIEBA
    static constexpr const char* SUPPORTED = "'simple' (default), 'jieba' (advanced Chinese)";
#else
    static constexpr const char* SUPPORTED = "'simple' (default)";
#endif
    static constexpr const char* NO_JIEBA = "This build has no jieba tokenizer.";

    static void validate(const std::string& tokenizer);
    // Throws if this build has no jieba tokenizer (see FTS_JIEBA).
    static void requireJieba();
    // Whether this build can tokenize as an index built with `tokenizer` does.
    static bool isBuilt(const std::string& tokenizer);
};

struct TokenizerInfo {
    std::string tokenizer = Tokenizer::DEFAULT_VALUE;
#ifdef LBUG_FTS_JIEBA
    std::string jiebaDictDir = std::format("{}/extension/fts/build/dict", LBUG_ROOT_DIRECTORY);
#else
    std::string jiebaDictDir;
#endif

    TokenizerInfo() = default;
};

struct FTSConfig;

struct CreateFTSConfig {
    std::string stemmer = Stemmer::DEFAULT_VALUE;
    StopWordsTableInfo stopWordsTableInfo;
    std::string ignorePattern = IgnorePattern::DEFAULT_VALUE;
    std::string ignorePatternQuery = IgnorePattern::DEFAULT_VALUE_QUERY;
    TokenizerInfo tokenizerInfo;

    CreateFTSConfig() = default;
    CreateFTSConfig(main::ClientContext& context, common::table_id_t tableID,
        const std::string& indexName, const function::optional_params_t& optionalParams);

    FTSConfig getFTSConfig() const;
};

struct FTSConfig {
    // The version of the analysis that turns text into terms: tokenizing, applying the ignore
    // pattern, lower-casing, removing stopwords and stemming. Change it whenever any of those
    // changes, including an engine update that changes case mapping. An index keeps the terms
    // the old analysis produced, so its documents could no longer be found to delete or update;
    // the extension leaves an index of another version unloaded until it's created again.
    static constexpr uint64_t ANALYSIS_VERSION = 1;

    std::string stemmer = "";
    std::string stopWordsTableName = "";
    // The original stopwords that the user used when creating the index. This field is only
    // used by show_index.
    std::string stopWordsSource = "";
    std::string ignorePattern = "";
    std::string ignorePatternQuery = "";
    std::string tokenizer = "";
    std::string jiebaDictDir = "";
    // The analysis version the index was built with. Indexes built before it was stored read
    // as 0.
    uint64_t analysisVersion = 0;

    FTSConfig() = default;
    FTSConfig(std::string stemmer, std::string stopWordsTableName, std::string stopWordsSource,
        std::string ignorePattern, std::string ignorePatternQuery, std::string tokenizer,
        std::string jiebaDictDir, uint64_t analysisVersion)
        : stemmer{std::move(stemmer)}, stopWordsTableName{std::move(stopWordsTableName)},
          stopWordsSource{std::move(stopWordsSource)}, ignorePattern{std::move(ignorePattern)},
          ignorePatternQuery{std::move(ignorePatternQuery)}, tokenizer{std::move(tokenizer)},
          jiebaDictDir{std::move(jiebaDictDir)}, analysisVersion{analysisVersion} {}

    void serialize(common::Serializer& serializer) const;

    static FTSConfig deserialize(common::Deserializer& deserializer);
};

struct K {
    static constexpr const char* NAME = "k";
    static constexpr common::LogicalTypeID TYPE = common::LogicalTypeID::DOUBLE;
    static constexpr double DEFAULT_VALUE = 1.2;
    static void validate(double value);
};

struct B {
    static constexpr const char* NAME = "b";
    static constexpr common::LogicalTypeID TYPE = common::LogicalTypeID::DOUBLE;
    static constexpr double DEFAULT_VALUE = 0.75;
    static void validate(double value);
};

struct Conjunctive {
    static constexpr const char* NAME = "conjunctive";
    static constexpr common::LogicalTypeID TYPE = common::LogicalTypeID::BOOL;
    static constexpr bool DEFAULT_VALUE = false;
};

struct TopK {
    static constexpr const char* NAME = "top";
    static constexpr common::LogicalTypeID TYPE = common::LogicalTypeID::UINT64;
    static constexpr uint64_t DEFAULT_VALUE = UINT64_MAX;
    static void validate(uint64_t value);
};

} // namespace fts_extension
} // namespace lbug
