/**
 *    Copyright (C) 2022-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <string>
#include <utility>

#include "mongo/bson/bsonobj.h"
#include "mongo/db/exec/sbe/values/value.h"
#include "mongo/db/query/optimizer/cascades/memo_explain_interface.h"
#include "mongo/db/query/optimizer/explain_interface.h"
#include "mongo/db/query/optimizer/index_bounds.h"
#include "mongo/db/query/optimizer/metadata.h"
#include "mongo/db/query/optimizer/node_defs.h"
#include "mongo/db/query/optimizer/partial_schema_requirements.h"
#include "mongo/db/query/optimizer/props.h"
#include "mongo/db/query/optimizer/syntax/syntax.h"


namespace mongo::optimizer {

enum class ExplainVersion { V1, V2, V2Compact, V3, UserFacingExplain, Vmax };

/**
 * This structure holds any data that is required by the explain. It is self-sufficient and separate
 * because it must outlive the other optimizer state as it is used by the runtime plan executor.
 */
class ABTPrinter : public AbstractABTPrinter {
public:
    ABTPrinter(Metadata metadata, PlanAndProps planAndProps, ExplainVersion explainVersion);

    BSONObj explainBSON() const override final;
    std::string getPlanSummary() const override final;

private:
    // Metadata field used to populate index information for index scans in the planSummary field.
    Metadata _metadata;
    PlanAndProps _planAndProps;
    ExplainVersion _explainVersion;
};

class UserFacingExplain {
public:
    UserFacingExplain(const NodeToGroupPropsMap& nodeMap = {}) : _nodeMap(nodeMap) {}

    // Constants relevant to all stages.
    constexpr static StringData kStage = "stage"_sd;
    constexpr static StringData kNodeId = "planNodeId"_sd;
    constexpr static StringData kProj = "projections"_sd;
    constexpr static StringData kCE = "cardinalityEstimate"_sd;
    constexpr static StringData kInput = "inputStage"_sd;

    // Specific to PhysicalScanNode.
    constexpr static StringData kScanName = "COLLSCAN"_sd;
    constexpr static StringData kDir = "direction"_sd;
    constexpr static StringData kForward = "forward"_sd;
    constexpr static StringData kBackward = "backward"_sd;
    constexpr static StringData kRandom = "random"_sd;

    // Specific to FilterNode.
    constexpr static StringData kFilterName = "FILTER"_sd;
    constexpr static StringData kFilter = "filter"_sd;

    // Specific to EvaluationNode.
    constexpr static StringData kEvalName = "EVALUATION"_sd;

    // Specific to RootNode.
    constexpr static StringData kRootName = "ROOT"_sd;
    constexpr static StringData kCost = "costEstimate"_sd;

    // Specific to EOF.
    constexpr static StringData kEOF = "EOF"_sd;

    // The default noop case.
    template <typename T, typename... Ts>
    void walk(const T&, BSONObjBuilder* bob, Ts&&...) {
        // If we get here, that means we are trying to generate explain for an unsupported node. We
        // should never generate an unsupported node to explain to begin with.
        tasserted(8075606, "Trying to generate explain for an unsupported node.");
    }

    void walk(const RootNode& node, BSONObjBuilder* bob, const ABT& child, const ABT& /* refs */) {
        auto it = _nodeMap.find(&node);
        tassert(8075600, "Failed to find node properties", it != _nodeMap.end());
        const NodeProps& props = it->second;

        bob->append(kStage, kRootName);
        bob->append(kProj, "<todo>");
        bob->append(kCE, "<todo>");
        bob->append(kCost, props._cost.getCost());

        BSONObjBuilder inputBob(bob->subobjStart(kInput));
        generateExplain(child, &inputBob);
    }

    void walk(const FilterNode& node,
              BSONObjBuilder* bob,
              const ABT& child,
              const ABT& /* expr */) {
        auto it = _nodeMap.find(&node);
        tassert(8075601, "Failed to find node properties", it != _nodeMap.end());
        const NodeProps& props = it->second;

        bob->append(kStage, kFilterName);
        bob->append(kNodeId, props._planNodeId);
        bob->append(kFilter, "<todo>");
        bob->append(kCE, "<todo>");

        BSONObjBuilder inputBob(bob->subobjStart(kInput));
        generateExplain(child, &inputBob);
    }

    void walk(const EvaluationNode& node,
              BSONObjBuilder* bob,
              const ABT& child,
              const ABT& /* expr */) {
        auto it = _nodeMap.find(&node);
        tassert(8075602, "Failed to find node properties", it != _nodeMap.end());
        const NodeProps& props = it->second;

        bob->append(kStage, kEvalName);
        bob->append(kNodeId, props._planNodeId);
        bob->append(kProj, "<todo>");
        bob->append(kCE, "<todo>");

        BSONObjBuilder inputBob(bob->subobjStart(kInput));
        generateExplain(child, &inputBob);
    }

    void walk(const PhysicalScanNode& node, BSONObjBuilder* bob, const ABT& /* bind */) {
        auto it = _nodeMap.find(&node);
        tassert(8075603, "Failed to find node properties", it != _nodeMap.end());
        const NodeProps& props = it->second;

        bob->append(kStage, kScanName);
        bob->append(kNodeId, props._planNodeId);

        switch (node.getScanOrder()) {
            case ScanOrder::Forward:
                bob->append(kDir, kForward);
                break;
            case ScanOrder::Reverse:
                bob->append(kDir, kBackward);
                break;
            case ScanOrder::Random:
                bob->append(kDir, kRandom);
                break;
        }

        bob->append(kProj, "<todo>");
        bob->append(kCE, "<todo>");
    }

    void generateExplain(const ABT::reference_type n, BSONObjBuilder* bob) {
        algebra::walk<false>(n, *this, bob);
    }

    BSONObj generateEOFPlan(const ABT::reference_type node) {
        BSONObjBuilder bob;

        auto it = _nodeMap.find(node.cast<Node>());
        tassert(8075605, "Failed to find node properties", it != _nodeMap.end());
        const NodeProps& props = it->second;

        bob.append(kStage, kEOF);
        bob.append(kNodeId, props._planNodeId);

        return bob.obj();
    }

    bool isEOFPlan(const ABT::reference_type node) {
        // This function expects the full ABT to be the argument. So we must have a RootNode.
        auto root = node.cast<RootNode>();
        if (!root->getChild().is<EvaluationNode>()) {
            // An EOF plan will have an EvaluationNode as the child of the RootNode.
            return false;
        }

        auto eval = root->getChild().cast<EvaluationNode>();
        if (eval->getProjection() != Constant::nothing()) {
            // The EvaluationNode of an EOF plan will have Nothing as the projection.
            return false;
        }

        // This is the rest of an EOF plan.
        ABT eofChild =
            make<LimitSkipNode>(properties::LimitSkipRequirement{0, 0}, make<CoScanNode>());
        return eval->getChild() == eofChild;
    }

    BSONObj explain(const ABT::reference_type node) {
        // Short circuit to return EOF stage if the collection is empty.
        if (isEOFPlan(node)) {
            return generateEOFPlan(node);
        }

        BSONObjBuilder bob;
        generateExplain(node, &bob);

        BSONObj result = bob.obj();

        // If at this point (after the walk) the explain BSON is empty, that means the ABT had no
        // nodes (if it had any unsupported nodes, we would have hit the MONGO_UNREACHABLE in the
        // default case above).
        tassert(8075604, "The ABT has no nodes.", !result.isEmpty());

        return result;
    }

private:
    const NodeToGroupPropsMap& _nodeMap;
};

class ExplainGenerator {
public:
    // Optionally display logical and physical properties using the memo.
    // whenever memo delegators are printed.
    static std::string explain(ABT::reference_type node,
                               bool displayProperties = false,
                               const cascades::MemoExplainInterface* memoInterface = nullptr,
                               const NodeToGroupPropsMap& nodeMap = {});

    // Optionally display logical and physical properties using the memo.
    // whenever memo delegators are printed.
    static std::string explainV2(ABT::reference_type node,
                                 bool displayProperties = false,
                                 const cascades::MemoExplainInterface* memoInterface = nullptr,
                                 const NodeToGroupPropsMap& nodeMap = {});

    // Optionally display logical and physical properties using the memo.
    // whenever memo delegators are printed.
    static std::string explainV2Compact(
        ABT::reference_type node,
        bool displayProperties = false,
        const cascades::MemoExplainInterface* memoInterface = nullptr,
        const NodeToGroupPropsMap& nodeMap = {});

    static std::string explainNode(ABT::reference_type node);

    static std::pair<sbe::value::TypeTags, sbe::value::Value> explainBSON(
        ABT::reference_type node,
        bool displayProperties = false,
        const cascades::MemoExplainInterface* memoInterface = nullptr,
        const NodeToGroupPropsMap& nodeMap = {});

    static BSONObj explainBSONObj(ABT::reference_type node,
                                  bool displayProperties = false,
                                  const cascades::MemoExplainInterface* memoInterface = nullptr,
                                  const NodeToGroupPropsMap& nodeMap = {});

    static std::string explainBSONStr(ABT::reference_type node,
                                      bool displayProperties = false,
                                      const cascades::MemoExplainInterface* memoInterface = nullptr,
                                      const NodeToGroupPropsMap& nodeMap = {});

    static std::string explainLogicalProps(const std::string& description,
                                           const properties::LogicalProps& props);
    static std::string explainPhysProps(const std::string& description,
                                        const properties::PhysProps& props);

    static std::string explainMemo(const cascades::MemoExplainInterface& memoInterface);

    static std::pair<sbe::value::TypeTags, sbe::value::Value> explainMemoBSON(
        const cascades::MemoExplainInterface& memoInterface);

    static BSONObj explainMemoBSONObj(const cascades::MemoExplainInterface& memoInterface);

    static std::string explainPartialSchemaReqExpr(const PSRExpr::Node& reqs);

    static std::string explainResidualRequirements(const ResidualRequirements::Node& resReqs);

    static std::string explainInterval(const IntervalRequirement& interval);

    static std::string explainCompoundInterval(const CompoundIntervalRequirement& interval);

    static std::string explainIntervalExpr(const IntervalReqExpr::Node& intervalExpr);

    static std::string explainCompoundIntervalExpr(
        const CompoundIntervalReqExpr::Node& intervalExpr);

    static std::string explainCandidateIndex(const CandidateIndexEntry& indexEntry);
};

}  // namespace mongo::optimizer
