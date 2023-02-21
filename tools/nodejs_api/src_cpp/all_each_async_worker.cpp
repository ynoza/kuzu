#include "all_each_async_worker.h"

#include <chrono>
#include <thread>
#include <vector>

#include "node_query_result.h"

using namespace std;

AllEachAsyncWorker::AllEachAsyncWorker(Function& callback, shared_ptr<kuzu::main::QueryResult>& queryResult,
    std::string executionType)
    : AsyncWorker(callback), queryResult(queryResult), executionType(executionType) {};

void AllEachAsyncWorker::Execute() {
    unsigned int i = 0;
    while(this->queryResult->hasNext()) {
        auto row = this->queryResult->getNext();
        allResult.emplace_back(vector<unique_ptr<kuzu::common::Value>>(row->len()));
        for (unsigned int j = 0; j < row->len(); j++) {
            allResult[i][j] = make_unique<kuzu::common::Value>(kuzu::common::Value(*row->getValue(j)));
        }
        i++;
    }
};

void AllEachAsyncWorker::OnOK() {
    Napi::Array arr = Napi::Array::New(Env(), allResult.size());
    for (unsigned int i=0; i < allResult.size(); i++) {
        Napi::Array rowArray = Napi::Array::New(Env(), allResult[i].size());
        unsigned int j = 0;
        for (; j < allResult[i].size(); j++){
            rowArray.Set(j, ConvertToNapiObject(*allResult[i][j], Env()));
        }
        if (executionType == "each") {
            rowArray.Set(j, allResult.size()-i-1);
            Callback().Call({Env().Null(), rowArray});
        } else if (executionType == "all") {
            arr.Set(i, rowArray);
        }
    }
    if (executionType == "all") {
        Callback().Call({Env().Null(), arr});
    }
};


Napi::Value AllEachAsyncWorker::ConvertToNapiObject(const kuzu::common::Value& value, Napi::Env env) {
    if (value.isNull()) {
        return Napi::Value();
    }
    auto dataType = value.getDataType();
    switch (dataType.typeID) {
        case kuzu::common::BOOL: {
            return Napi::Boolean::New(env, value.getValue<bool>());
        }
        case kuzu::common::INT64: {
            return Napi::Number::New(env, value.getValue<int64_t>());
        }
        case kuzu::common::DOUBLE: {
            return Napi::Number::New(env, value.getValue<double>());
        }
        case kuzu::common::STRING: {
            return Napi::String::New(env, value.getValue<string>());
        }
        case kuzu::common::LIST: {
            auto& listVal = value.getListValReference();
            Napi::Array arr = Napi::Array::New(env, listVal.size());
            for (auto i = 0u; i < listVal.size(); ++i) {
                arr.Set(i, ConvertToNapiObject(*listVal[i], env));
            }
            return arr;
        }
        default:
            throw NotImplementedException("Unsupported type: " + kuzu::common::Types::dataTypeToString(dataType));
    }
}
