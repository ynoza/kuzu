#include "src/processor/include/physical_plan/operator/physical_operator.h"

#include <regex>

namespace graphflow {
namespace processor {

PhysicalOperator::PhysicalOperator(unique_ptr<PhysicalOperator> child, uint32_t id)
    : PhysicalOperator{id} {
    children.push_back(move(child));
}

PhysicalOperator::PhysicalOperator(
    unique_ptr<PhysicalOperator> left, unique_ptr<PhysicalOperator> right, uint32_t id)
    : PhysicalOperator{id} {
    children.push_back(move(left));
    children.push_back(move(right));
}

PhysicalOperator::PhysicalOperator(vector<unique_ptr<PhysicalOperator>> children, uint32_t id)
    : PhysicalOperator{id} {
    for (auto& child : children) {
        this->children.push_back(move(child));
    }
}

PhysicalOperator* PhysicalOperator::getLeafOperator() {
    PhysicalOperator* op = this;
    while (op->getNumChildren()) {
        op = op->getChild(0);
    }
    return op;
}

shared_ptr<ResultSet> PhysicalOperator::init(ExecutionContext* context) {
    registerProfilingMetrics(context->profiler);
    if (!children.empty()) {
        resultSet = children[0]->init(context);
    }
    return resultSet;
}

void PhysicalOperator::registerProfilingMetrics(Profiler* profiler) {
    auto executionTime = profiler->registerTimeMetric(getTimeMetricKey());
    auto numOutputTuple = profiler->registerNumericMetric(getNumTupleMetricKey());

    metrics = make_unique<OperatorMetrics>(*executionTime, *numOutputTuple);
}

void PhysicalOperator::printMetricsToJson(nlohmann::json& json, Profiler& profiler) {
    printTimeAndNumOutputMetrics(json, profiler);
}

void PhysicalOperator::printTimeAndNumOutputMetrics(nlohmann::json& json, Profiler& profiler) {
    double prevExecutionTime = 0.0;
    if (getNumChildren()) {
        prevExecutionTime = profiler.sumAllTimeMetricsWithKey(children[0]->getTimeMetricKey());
    }
    // Time metric measures execution time of the subplan under current operator (like a CDF).
    // By subtracting prevOperator runtime, we get the runtime of current operator
    json["executionTime"] =
        to_string(profiler.sumAllTimeMetricsWithKey(getTimeMetricKey()) - prevExecutionTime);
    json["numOutputTuples"] = profiler.sumAllNumericMetricsWithKey(getNumTupleMetricKey());
}

double PhysicalOperator::getExecutionTime(Profiler& profiler) const {
    double prevExecutionTime = 0.0;
    for (auto i = 0u; i < getNumChildren(); i++) {
        prevExecutionTime += profiler.sumAllTimeMetricsWithKey(children[i]->getTimeMetricKey());
    }
    return profiler.sumAllTimeMetricsWithKey(getTimeMetricKey()) - prevExecutionTime;
}

vector<string> PhysicalOperator::getAttributes(Profiler& profiler) const {
    vector<string> metrics;
    metrics.emplace_back("ExecutionTime: " + to_string(getExecutionTime(profiler)));
    metrics.emplace_back("NumOutputTuples: " + to_string(getNumOutputTuples(profiler)));
    return metrics;
}

} // namespace processor
} // namespace graphflow
