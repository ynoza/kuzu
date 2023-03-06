#pragma once

#include <ctime>
#include <iostream>
#include <chrono>

#include <napi.h>
#include "main/kuzu.h"

class Util {
    public:
        static Napi::Object GetObjectFromProperties(
            const vector<pair<std::string, unique_ptr<kuzu::common::Value>>>& properties,
            Napi::Env env);
        static Napi::Value ConvertToNapiObject(const kuzu::common::Value& value, Napi::Env env);
        static unsigned long GetEpochFromDate(int32_t year, int32_t month, int32_t day);
        static unordered_map<string, shared_ptr<kuzu::common::Value>> transformParameters(Napi::Array params);
        static kuzu::common::Value transformNapiValue(Napi::Value val);
};
