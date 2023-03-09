#include "include/node_database.h"

#include "main/kuzu.h"

using namespace kuzu::main;

Napi::FunctionReference NodeDatabase::constructor;

Napi::Object NodeDatabase::Init(Napi::Env env, Napi::Object exports) {
    Napi::HandleScope scope(env);

    Napi::Function t = DefineClass(env, "NodeDatabase", {
          InstanceMethod("resizeBufferManager", &NodeDatabase::ResizeBufferManager),
      });

    exports.Set("NodeDatabase", t);
    return exports;
}


NodeDatabase::NodeDatabase(const Napi::CallbackInfo& info) : Napi::ObjectWrap<NodeDatabase>(info)  {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length()!=2) {
        Napi::TypeError::New(env, "Need database path and buffer manager size").ThrowAsJavaScriptException();
        return;
    } else if (!info[0].IsString()) {
        Napi::TypeError::New(env, "Database path must be a string").ThrowAsJavaScriptException();
        return;
    } else if (!info[1].IsNumber()) {
        Napi::TypeError::New(env, "Database buffer manager size must be an int_64").ThrowAsJavaScriptException();
        return;
    }

    std::string databasePath = info[0].ToString();
    std::int64_t bufferPoolSize = info[1].As<Napi::Number>().DoubleValue();

    auto systemConfig = kuzu::main::SystemConfig();
    if (bufferPoolSize > 0) {
        systemConfig.defaultPageBufferPoolSize =
            bufferPoolSize * kuzu::common::StorageConstants::DEFAULT_PAGES_BUFFER_RATIO;
        systemConfig.largePageBufferPoolSize =
            bufferPoolSize * kuzu::common::StorageConstants::LARGE_PAGES_BUFFER_RATIO;
    }

    try {
        this->database = make_unique<kuzu::main::Database>(databasePath, systemConfig);
    } catch(const std::exception &exc) {
        Napi::TypeError::New(env, "Unsuccessful Database Initialization: " + std::string(exc.what())).ThrowAsJavaScriptException();
    }
}

NodeDatabase::~NodeDatabase() {}

void NodeDatabase::ResizeBufferManager(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    std::int64_t bufferSize = 0;
    if (info.Length()!=1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Database buffer manager size must be an int_64").ThrowAsJavaScriptException();
        return;
    }
    bufferSize = info[0].As<Napi::Number>().DoubleValue();

    try {
        this->database->resizeBufferManager(bufferSize);
    } catch(const std::exception &exc) {
        Napi::TypeError::New(env, "Unsuccessful resizeBufferManager: " + std::string(exc.what())).ThrowAsJavaScriptException();
    }
    return;
}
