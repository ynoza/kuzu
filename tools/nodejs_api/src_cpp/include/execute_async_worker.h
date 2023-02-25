#pragma once
#include <napi.h>
#include "main/kuzu.h"
#include "node_query_result.h"

using namespace Napi;

class ExecuteAsyncWorker : public AsyncWorker {

public:
    ExecuteAsyncWorker(Function& callback, shared_ptr<kuzu::main::Connection>& connection,
        std::string query, NodeQueryResult * nodeQueryResult, unordered_map<std::string, shared_ptr<kuzu::common::Value>> & params);
    virtual ~ExecuteAsyncWorker() {};

    void Execute();
    void OnOK();

private:
    NodeQueryResult * nodeQueryResult;
    std::string query;
    shared_ptr<kuzu::main::Connection> connection;
    unordered_map<std::string, shared_ptr<kuzu::common::Value>> params;
};
