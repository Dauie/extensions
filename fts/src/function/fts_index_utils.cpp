#include "function/fts_index_utils.h"

#include "binder/binder.h"
#include "catalog/catalog.h"
#include "catalog/catalog_entry/index_catalog_entry.h"
#include "catalog/catalog_entry/node_table_catalog_entry.h"
#include "common/exception/binder.h"
#include "transaction/transaction_context.h"
#include <format>

namespace lbug {
namespace fts_extension {

void FTSIndexUtils::validateIndexExistence(const main::ClientContext& context,
    const catalog::TableCatalogEntry* tableEntry, const std::string& indexName,
    IndexOperation indexOperation) {
    auto catalog = catalog::Catalog::Get(context);
    switch (indexOperation) {
    case IndexOperation::CREATE: {
        if (catalog->containsIndex(transaction::Transaction::Get(context), tableEntry->getTableID(),
                indexName)) {
            throw common::BinderException{std::format("Index {} already exists in table {}.",
                indexName, tableEntry->getName())};
        }
    } break;
    case IndexOperation::DROP:
    case IndexOperation::QUERY: {
        auto transaction = transaction::Transaction::Get(context);
        if (!catalog->containsIndex(transaction, tableEntry->getTableID(), indexName)) {
            throw common::BinderException{std::format(
                "Table {} doesn't have an index with name {}.", tableEntry->getName(), indexName)};
        }
        // With the extension loaded, an index is unloaded because the extension couldn't use it
        // at open (see initFTSEntries), or because WAL replay restored it after the extension
        // loaded. Either way, dropping it is how to recover.
        if (indexOperation == IndexOperation::QUERY &&
            !catalog->getIndex(transaction, tableEntry->getTableID(), indexName)->isLoaded()) {
            throw common::BinderException{std::format(
                "Index {} on table {} needs to be rebuilt. Drop it and create it again.", indexName,
                tableEntry->getName())};
        }
    } break;
    default: {
        UNREACHABLE_CODE;
    }
    }
}

catalog::NodeTableCatalogEntry* FTSIndexUtils::bindNodeTable(const main::ClientContext& context,
    const std::string& tableName, const std::string& indexName, IndexOperation indexOperation) {
    binder::Binder::validateTableExistence(context, tableName);
    const auto tableEntry = catalog::Catalog::Get(context)->getTableCatalogEntry(
        transaction::Transaction::Get(context), tableName);
    binder::Binder::validateNodeTableType(tableEntry);
    validateIndexExistence(context, tableEntry, indexName, indexOperation);
    return tableEntry->ptrCast<catalog::NodeTableCatalogEntry>();
}

void FTSIndexUtils::validateAutoTransaction(const main::ClientContext& context,
    const std::string& funcName) {
    if (!transaction::TransactionContext::Get(context)->isAutoTransaction()) {
        throw common::BinderException{
            std::format("{} is only supported in auto transaction mode.", funcName)};
    }
}

} // namespace fts_extension
} // namespace lbug
