#pragma once

#include <cassert>

#include "common/exception.h"
#include "common/in_mem_overflow_buffer_utils.h"
#include "common/type_utils.h"
#include "common/vector/value_vector.h"

namespace kuzu {
namespace function {
namespace operation {

struct CastStringToDate {
    static inline void operation(common::ku_string_t& input, common::date_t& result) {
        result = common::Date::FromCString((const char*)input.getData(), input.len);
    }
};

struct CastStringToTimestamp {
    static inline void operation(common::ku_string_t& input, common::timestamp_t& result) {
        result = common::Timestamp::FromCString((const char*)input.getData(), input.len);
    }
};

struct CastStringToInterval {
    static inline void operation(common::ku_string_t& input, common::interval_t& result) {
        result = common::Interval::FromCString((const char*)input.getData(), input.len);
    }
};

struct CastToString {
    template<typename T>
    static inline std::string castToStringWithDataType(T& input, const common::DataType& dataType) {
        return common::TypeUtils::toString(input);
    }

    template<typename T>
    static inline void operation(T& input, common::ku_string_t& result,
        common::ValueVector& resultVector, const common::DataType& dataType) {
        std::string resultStr = castToStringWithDataType(input, dataType);
        if (resultStr.length() > common::ku_string_t::SHORT_STR_LENGTH) {
            result.overflowPtr = reinterpret_cast<uint64_t>(
                resultVector.getOverflowBuffer().allocateSpace(resultStr.length()));
        }
        result.set(resultStr);
    }
};

template<>
inline std::string CastToString::castToStringWithDataType(
    common::ku_list_t& input, const common::DataType& dataType) {
    return common::TypeUtils::toString(input, dataType);
}

template<typename SRC, typename DST>
static inline void numericDownCast(SRC& input, DST& result, const std::string& dstTypeStr) {
    if (input < std::numeric_limits<DST>::min() || input > std::numeric_limits<DST>::max()) {
        throw common::RuntimeException(
            "Cast failed. " + std::to_string(input) + " is not in " + dstTypeStr + " range.");
    }
    result = (DST)input;
}

struct CastToDouble {
    template<typename T>
    static inline void operation(T& input, double_t& result) {
        result = static_cast<double_t>(input);
    }
};

struct CastToFloat {
    template<typename T>
    static inline void operation(T& input, float_t& result) {
        result = static_cast<float_t>(input);
    }
};

template<>
inline void CastToFloat::operation(double_t& input, float_t& result) {
    numericDownCast<double_t, float_t>(input, result, "FLOAT");
}

struct CastToInt64 {
    template<typename T>
    static inline void operation(T& input, int64_t& result) {
        result = static_cast<int64_t>(input);
    }
};

template<>
inline void CastToInt64::operation(double_t& input, int64_t& result) {
    numericDownCast<double_t, int64_t>(input, result, "INT64");
}

template<>
inline void CastToInt64::operation(float_t& input, int64_t& result) {
    numericDownCast<float_t, int64_t>(input, result, "INT64");
}

struct CastToInt32 {
    template<typename T>
    static inline void operation(T& input, int32_t& result) {
        result = static_cast<int32_t>(input);
    }
};

template<>
inline void CastToInt32::operation(double_t& input, int32_t& result) {
    numericDownCast<double_t, int32_t>(input, result, "INT32");
}

template<>
inline void CastToInt32::operation(float_t& input, int32_t& result) {
    numericDownCast<float_t, int32_t>(input, result, "INT32");
}

template<>
inline void CastToInt32::operation(int64_t& input, int32_t& result) {
    numericDownCast<int64_t, int32_t>(input, result, "INT32");
}

struct CastToInt16 {
    template<typename T>
    static inline void operation(T& input, int16_t& result) {
        result = static_cast<int16_t>(input);
    }
};

template<>
inline void CastToInt16::operation(double_t& input, int16_t& result) {
    numericDownCast<double_t, int16_t>(input, result, "INT16");
}

template<>
inline void CastToInt16::operation(float_t& input, int16_t& result) {
    numericDownCast<float_t, int16_t>(input, result, "INT16");
}

template<>
inline void CastToInt16::operation(int64_t& input, int16_t& result) {
    numericDownCast<int64_t, int16_t>(input, result, "INT16");
}

template<>
inline void CastToInt16::operation(int32_t& input, int16_t& result) {
    numericDownCast<int32_t, int16_t>(input, result, "INT16");
}

} // namespace operation
} // namespace function
} // namespace kuzu
