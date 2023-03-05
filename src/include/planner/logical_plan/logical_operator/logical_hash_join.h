#pragma once

#include <utility>

#include "base_logical_operator.h"
#include "binder/expression/node_expression.h"
#include "common/join_type.h"

namespace kuzu {
namespace planner {

// Probe side on left, i.e. children[0]. Build side on right, i.e. children[1].
class LogicalHashJoin : public LogicalOperator {
public:
    // Inner and left join.
    LogicalHashJoin(binder::expression_vector joinNodeIDs, common::JoinType joinType,
        bool isProbeAcc, std::shared_ptr<LogicalOperator> probeSideChild,
        std::shared_ptr<LogicalOperator> buildSideChild)
        : LogicalHashJoin{std::move(joinNodeIDs), joinType, nullptr, isProbeAcc,
              std::move(probeSideChild), std::move(buildSideChild)} {}

    // Mark join.
    LogicalHashJoin(binder::expression_vector joinNodeIDs, std::shared_ptr<binder::Expression> mark,
        bool isProbeAcc, std::shared_ptr<LogicalOperator> probeSideChild,
        std::shared_ptr<LogicalOperator> buildSideChild)
        : LogicalHashJoin{std::move(joinNodeIDs), common::JoinType::MARK, std::move(mark),
              isProbeAcc, std::move(probeSideChild), std::move(buildSideChild)} {}

    LogicalHashJoin(binder::expression_vector joinNodeIDs, common::JoinType joinType,
        std::shared_ptr<binder::Expression> mark, bool isProbeAcc,
        std::shared_ptr<LogicalOperator> probeSideChild,
        std::shared_ptr<LogicalOperator> buildSideChild)
        : LogicalOperator{LogicalOperatorType::HASH_JOIN, std::move(probeSideChild),
              std::move(buildSideChild)},
          joinNodeIDs(std::move(joinNodeIDs)), joinType{joinType}, mark{std::move(mark)},
          isProbeAcc{isProbeAcc} {}

    f_group_pos_set getGroupsPosToFlattenOnProbeSide();
    f_group_pos_set getGroupsPosToFlattenOnBuildSide();

    void computeFactorizedSchema() override;
    void computeFlatSchema() override;

    inline std::string getExpressionsForPrinting() const override {
        return binder::ExpressionUtil::toString(joinNodeIDs);
    }

    binder::expression_vector getExpressionsToMaterialize() const;
    inline binder::expression_vector getJoinNodeIDs() const { return joinNodeIDs; }
    inline common::JoinType getJoinType() const { return joinType; }

    inline std::shared_ptr<binder::Expression> getMark() const {
        assert(joinType == common::JoinType::MARK && mark);
        return mark;
    }
    inline bool getIsProbeAcc() const { return isProbeAcc; }

    inline std::unique_ptr<LogicalOperator> copy() override {
        return make_unique<LogicalHashJoin>(
            joinNodeIDs, joinType, mark, isProbeAcc, children[0]->copy(), children[1]->copy());
    }

private:
    // Flat probe side key group in either of the following two cases:
    // 1. there are multiple join nodes;
    // 2. if the build side contains more than one group or the build side has projected out data
    // chunks, which may increase the multiplicity of data chunks in the build side. The key is to
    // keep probe side key unflat only when we know that there is only 0 or 1 match for each key.
    // TODO(Guodong): when the build side has only flat payloads, we should consider getting rid of
    // flattening probe key, instead duplicating keys as in vectorized processing if necessary.
    bool requireFlatProbeKeys();

    bool isJoinKeyUniqueOnBuildSide(const binder::Expression& joinNodeID);

private:
    binder::expression_vector joinNodeIDs;
    common::JoinType joinType;
    std::shared_ptr<binder::Expression> mark; // when joinType is Mark
    bool isProbeAcc;
};

} // namespace planner
} // namespace kuzu
