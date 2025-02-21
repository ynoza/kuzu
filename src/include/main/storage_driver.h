#pragma once

#include <queue>

#include "common/types/types_include.h"
#include "database.h"

namespace kuzu {
namespace storage {
class Column;
}

namespace main {

class StorageDriver {
public:
    explicit StorageDriver(Database* database);

    ~StorageDriver();

    void scan(const std::string& nodeName, const std::string& propertyName,
        common::offset_t* offsets, size_t size, uint8_t* result, size_t numThreads);

    uint64_t getNumNodes(const std::string& nodeName);
    uint64_t getNumRels(const std::string& relName);

private:
    void scanColumn(
        storage::Column* column, common::offset_t* offsets, size_t size, uint8_t* result);

private:
    catalog::Catalog* catalog;
    storage::StorageManager* storageManager;
};

} // namespace main
} // namespace kuzu
