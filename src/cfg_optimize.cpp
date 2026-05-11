#include "cfg_optimize.hpp"

#include "runtime_support.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using ConstantMap = std::unordered_map<CfgValueId, Value>;
struct DefinitionSite {
    CfgBlockId block = 0;
    std::size_t instruction_index = 0;
    const CfgInstruction* instruction = nullptr;
};
using DefinitionMap = std::unordered_map<CfgValueId, DefinitionSite>;

struct MaterializedFreshArrayValue {
    Value value;
    CfgValueId root = kInvalidCfgValueId;
    std::size_t root_instruction_index = 0;
};

enum class CollectionShapeKind {
    Unknown,
    Array,
    String
};

struct CollectionShapeInfo {
    bool initialized = false;
    CollectionShapeKind kind = CollectionShapeKind::Unknown;
    bool whole_array = false;
    std::optional<long long> known_length;
};

struct LocalCollectionShape {
    CollectionShapeKind kind = CollectionShapeKind::Unknown;
    CfgValueId root = kInvalidCfgValueId;
    bool whole_array = false;
    std::optional<long long> known_length;
};

using BlockEntryCollectionFacts = std::vector<std::vector<CollectionShapeInfo>>;
using LocalCollectionFacts = std::unordered_map<CfgValueId, LocalCollectionShape>;
using TargetCollectionFacts = std::unordered_map<const CfgBlockTarget*, std::vector<CollectionShapeInfo>>;

bool IsFoldableLiteralValue(const Value& value) {
    return std::holds_alternative<std::monostate>(value) || std::holds_alternative<long long>(value) ||
           std::holds_alternative<bool>(value) || std::holds_alternative<char>(value) ||
           std::holds_alternative<StringValuePtr>(value);
}

bool IsImmutableScalarLikeType(const TypeInfo& type) {
    return type == TypeInfo{TypeKind::Unit, ""} || type == TypeInfo{TypeKind::Int, ""} ||
           type == TypeInfo{TypeKind::Bool, ""} || type == TypeInfo{TypeKind::Char, ""} ||
           type == TypeInfo{TypeKind::String, ""};
}

bool IsCollectionLikeType(const TypeInfo& type) {
    return type == TypeInfo{TypeKind::String, ""} || type.kind == TypeKind::Slice;
}

CollectionShapeInfo UnknownShapeInfoForType(const TypeInfo& type) {
    if (type == TypeInfo{TypeKind::String, ""}) {
        return {true, CollectionShapeKind::String, false, std::nullopt};
    }
    if (type.kind == TypeKind::Slice) {
        return {true, CollectionShapeKind::Array, false, std::nullopt};
    }
    return {};
}

LocalCollectionShape MakeLocalShape(CfgValueId root, const CollectionShapeInfo& info) {
    return {info.kind, root, info.whole_array, info.known_length};
}

CollectionShapeInfo ToShapeInfo(const LocalCollectionShape& shape) {
    return {true, shape.kind, shape.whole_array, shape.known_length};
}

bool ShapeInfoEquals(const CollectionShapeInfo& left, const CollectionShapeInfo& right) {
    return left.initialized == right.initialized && left.kind == right.kind && left.whole_array == right.whole_array &&
           left.known_length == right.known_length;
}

bool ShapeInfoVectorEquals(const std::vector<CollectionShapeInfo>& left, const std::vector<CollectionShapeInfo>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!ShapeInfoEquals(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

bool TargetCollectionFactsEqual(const TargetCollectionFacts& left, const TargetCollectionFacts& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (const auto& [target, facts] : left) {
        const auto it = right.find(target);
        if (it == right.end() || !ShapeInfoVectorEquals(facts, it->second)) {
            return false;
        }
    }
    return true;
}

bool ConstantValuesEqual(const Value& left, const Value& right) {
    if (left.index() != right.index()) {
        return false;
    }

    if (std::holds_alternative<std::monostate>(left)) {
        return true;
    }
    if (const auto* left_integer = std::get_if<long long>(&left)) {
        return *left_integer == std::get<long long>(right);
    }
    if (const auto* left_bool = std::get_if<bool>(&left)) {
        return *left_bool == std::get<bool>(right);
    }
    if (const auto* left_char = std::get_if<char>(&left)) {
        return *left_char == std::get<char>(right);
    }

    return StringToString(*std::get<StringValuePtr>(left)) == StringToString(*std::get<StringValuePtr>(right));
}

bool TryGetConstantValue(const ConstantMap& constants, CfgValueId value, Value& output) {
    const auto it = constants.find(value);
    if (it == constants.end()) {
        return false;
    }

    output = it->second;
    return true;
}

std::optional<long long> TryGetConstantInteger(const ConstantMap& constants, CfgValueId value) {
    Value constant;
    if (!TryGetConstantValue(constants, value, constant)) {
        return std::nullopt;
    }

    const auto* integer = std::get_if<long long>(&constant);
    if (integer == nullptr) {
        return std::nullopt;
    }

    return *integer;
}

std::optional<long long> TryComputeRangeLength(const ConstantMap& constants,
                                               CfgValueId start_value,
                                               CfgValueId end_value) {
    const auto start = TryGetConstantInteger(constants, start_value);
    const auto end = TryGetConstantInteger(constants, end_value);
    if (!start.has_value() || !end.has_value()) {
        return std::nullopt;
    }

    return *start < *end ? *end - *start : 0LL;
}

std::optional<long long> TryComputeSliceLength(std::optional<long long> base_length,
                                               bool has_start,
                                               std::optional<long long> start,
                                               bool has_end,
                                               std::optional<long long> end) {
    if (!base_length.has_value()) {
        return std::nullopt;
    }

    const long long slice_start = has_start ? (start.has_value() ? *start : -1) : 0;
    const long long slice_end = has_end ? (end.has_value() ? *end : -1) : *base_length;
    if (slice_start < 0 || slice_end < 0 || slice_start > slice_end || slice_end > *base_length) {
        return std::nullopt;
    }

    return slice_end - slice_start;
}

bool IsBuiltinCollectionMutator(IrBuiltinKind builtin_kind) {
    switch (builtin_kind) {
    case IrBuiltinKind::Push:
    case IrBuiltinKind::Pop:
    case IrBuiltinKind::Insert:
    case IrBuiltinKind::RemoveAt:
    case IrBuiltinKind::Clear:
        return true;
    case IrBuiltinKind::Len:
    case IrBuiltinKind::Contains:
    case IrBuiltinKind::StartsWith:
    case IrBuiltinKind::EndsWith:
    case IrBuiltinKind::Join:
    case IrBuiltinKind::Print:
    case IrBuiltinKind::FileExists:
    case IrBuiltinKind::ReadText:
    case IrBuiltinKind::Abs:
    case IrBuiltinKind::Min:
    case IrBuiltinKind::Max:
    case IrBuiltinKind::Pow:
    case IrBuiltinKind::None:
        return false;
    }

    return false;
}

DefinitionMap BuildDefinitionMap(const CfgFunctionDecl& function) {
    DefinitionMap definitions;

    for (const auto& block : function.blocks) {
        for (std::size_t instruction_index = 0; instruction_index < block.instructions.size(); ++instruction_index) {
            const auto& instruction = block.instructions[instruction_index];
            if (instruction.result == kInvalidCfgValueId) {
                continue;
            }

            definitions.emplace(instruction.result, DefinitionSite{block.id, instruction_index, &instruction});
        }
    }

    return definitions;
}

bool TryMaterializeFreshLocalArrayValueRecursive(const CfgFunctionDecl& function,
                                                 const DefinitionMap& definitions,
                                                 CfgBlockId block_id,
                                                 CfgValueId value,
                                                 const ConstantMap& constants,
                                                 MaterializedFreshArrayValue& output) {
    (void)function;
    const auto definition_it = definitions.find(value);
    if (definition_it == definitions.end()) {
        return false;
    }

    const DefinitionSite& definition = definition_it->second;
    if (definition.block != block_id || definition.instruction == nullptr) {
        return false;
    }

    const CfgInstruction& instruction = *definition.instruction;
    try {
        switch (instruction.kind) {
        case CfgInstructionKind::Copy:
            return instruction.inputs.size() == 1 &&
                   TryMaterializeFreshLocalArrayValueRecursive(function, definitions, block_id, instruction.inputs[0],
                                                               constants, output);
        case CfgInstructionKind::ArrayLiteral: {
            std::vector<Value> elements;
            elements.reserve(instruction.inputs.size());
            for (const CfgValueId input : instruction.inputs) {
                Value constant;
                if (!TryGetConstantValue(constants, input, constant)) {
                    return false;
                }
                elements.push_back(std::move(constant));
            }

            output.value = AuraMakeArrayLiteral(instruction.name, elements);
            output.root = instruction.result;
            output.root_instruction_index = definition.instruction_index;
            return true;
        }
        case CfgInstructionKind::Range: {
            if (instruction.inputs.size() != 2) {
                return false;
            }
            Value start;
            Value end;
            if (!TryGetConstantValue(constants, instruction.inputs[0], start) ||
                !TryGetConstantValue(constants, instruction.inputs[1], end)) {
                return false;
            }

            output.value = AuraMakeIntRange(start, end);
            output.root = instruction.result;
            output.root_instruction_index = definition.instruction_index;
            return true;
        }
        case CfgInstructionKind::GetSlice: {
            if (instruction.name == "String" || instruction.inputs.empty()) {
                return false;
            }

            MaterializedFreshArrayValue materialized_object;
            if (!TryMaterializeFreshLocalArrayValueRecursive(function, definitions, block_id, instruction.inputs[0],
                                                             constants, materialized_object)) {
                return false;
            }

            const auto start = instruction.has_start ? TryGetConstantInteger(constants, instruction.inputs[1])
                                                     : std::optional<long long>{};
            const auto end = instruction.has_end ? TryGetConstantInteger(constants, instruction.inputs.back())
                                                 : std::optional<long long>{};
            if ((instruction.has_start && !start.has_value()) || (instruction.has_end && !end.has_value())) {
                return false;
            }

            const Value start_value = instruction.has_start ? Value{*start} : Value{std::monostate{}};
            const Value end_value = instruction.has_end ? Value{*end} : Value{std::monostate{}};
            output.value = AuraSliceArray(materialized_object.value, instruction.has_start, start_value, instruction.has_end,
                                          end_value);
            output.root = materialized_object.root;
            output.root_instruction_index = materialized_object.root_instruction_index;
            return true;
        }
        case CfgInstructionKind::Literal:
        case CfgInstructionKind::Drop:
        case CfgInstructionKind::StructLiteral:
        case CfgInstructionKind::Unary:
        case CfgInstructionKind::Binary:
        case CfgInstructionKind::Call:
        case CfgInstructionKind::GetField:
        case CfgInstructionKind::GetIndex:
        case CfgInstructionKind::AssignField:
        case CfgInstructionKind::AssignIndex:
            return false;
        }
    } catch (const AuraError&) {
        return false;
    }

    return false;
}

bool IsFreshLocalArrayStable(const CfgFunctionDecl& function,
                             CfgBlockId block_id,
                             std::size_t use_instruction_index,
                             const MaterializedFreshArrayValue& materialized) {
    if (materialized.root == kInvalidCfgValueId) {
        return false;
    }

    const CfgBlock& block = function.blocks[block_id];
    std::unordered_set<CfgValueId> aliases = {materialized.root};

    for (std::size_t scan_index = materialized.root_instruction_index + 1; scan_index < use_instruction_index; ++scan_index) {
        const CfgInstruction& scan = block.instructions[scan_index];

        if (scan.kind == CfgInstructionKind::Call) {
            const bool touches_alias =
                std::any_of(scan.inputs.begin(), scan.inputs.end(),
                            [&](CfgValueId input) { return aliases.find(input) != aliases.end(); });
            if (touches_alias &&
                (scan.call_kind != IrCallKind::Builtin || IsBuiltinCollectionMutator(scan.builtin_kind))) {
                return false;
            }
        }

        if ((scan.kind == CfgInstructionKind::AssignIndex || scan.kind == CfgInstructionKind::AssignField) &&
            !scan.inputs.empty() && aliases.find(scan.inputs[0]) != aliases.end()) {
            return false;
        }

        if (scan.result == kInvalidCfgValueId) {
            continue;
        }

        if (scan.kind == CfgInstructionKind::Copy && scan.inputs.size() == 1 &&
            aliases.find(scan.inputs[0]) != aliases.end()) {
            aliases.insert(scan.result);
            continue;
        }

        if (scan.kind == CfgInstructionKind::GetSlice && scan.name != "String" && !scan.inputs.empty() &&
            aliases.find(scan.inputs[0]) != aliases.end()) {
            aliases.insert(scan.result);
        }
    }

    return true;
}

std::optional<Value> TryFoldBuiltinFromLocalProducer(const CfgFunctionDecl& function,
                                                     const DefinitionMap& definitions,
                                                     CfgBlockId block_id,
                                                     std::size_t instruction_index,
                                                     const CfgInstruction& instruction,
                                                     const ConstantMap& constants) {
    if (instruction.kind != CfgInstructionKind::Call || instruction.call_kind != IrCallKind::Builtin ||
        instruction.inputs.empty()) {
        return std::nullopt;
    }

    MaterializedFreshArrayValue materialized;
    const bool has_materialized_array =
        TryMaterializeFreshLocalArrayValueRecursive(function, definitions, block_id, instruction.inputs[0], constants,
                                                    materialized) &&
        IsFreshLocalArrayStable(function, block_id, instruction_index, materialized);

    try {
        switch (instruction.builtin_kind) {
        case IrBuiltinKind::Len:
            if (has_materialized_array) {
                return AuraBuiltinLen(materialized.value);
            }
            return std::nullopt;
        case IrBuiltinKind::Contains:
            if (!has_materialized_array || instruction.inputs.size() != 2) {
                return std::nullopt;
            }
            break;
        case IrBuiltinKind::Join:
            if (!has_materialized_array || instruction.inputs.size() != 2) {
                return std::nullopt;
            }
            break;
        case IrBuiltinKind::StartsWith:
        case IrBuiltinKind::EndsWith:
        case IrBuiltinKind::Print:
        case IrBuiltinKind::Push:
        case IrBuiltinKind::Pop:
        case IrBuiltinKind::Insert:
        case IrBuiltinKind::RemoveAt:
        case IrBuiltinKind::Clear:
        case IrBuiltinKind::FileExists:
        case IrBuiltinKind::ReadText:
        case IrBuiltinKind::Abs:
        case IrBuiltinKind::Min:
        case IrBuiltinKind::Max:
        case IrBuiltinKind::Pow:
        case IrBuiltinKind::None:
            return std::nullopt;
        }

        if (instruction.builtin_kind == IrBuiltinKind::Contains) {
            Value needle;
            if (!TryGetConstantValue(constants, instruction.inputs[1], needle)) {
                return std::nullopt;
            }

            const Value folded = AuraBuiltinContains(materialized.value, needle);
            if (IsFoldableLiteralValue(folded)) {
                return folded;
            }
            return std::nullopt;
        }

        if (instruction.builtin_kind == IrBuiltinKind::Join) {
            Value separator;
            if (!TryGetConstantValue(constants, instruction.inputs[1], separator)) {
                return std::nullopt;
            }

            const Value folded = AuraBuiltinJoin(materialized.value, separator);
            if (IsFoldableLiteralValue(folded)) {
                return folded;
            }
        }
    } catch (const AuraError&) {
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<Value> TryFoldBuiltin(const CfgInstruction& instruction, const std::vector<Value>& arguments) {
    if (instruction.call_kind != IrCallKind::Builtin) {
        return std::nullopt;
    }

    try {
        switch (instruction.builtin_kind) {
        case IrBuiltinKind::Len:
            if (arguments.size() == 1 && std::holds_alternative<StringValuePtr>(arguments[0])) {
                return AuraBuiltinLen(arguments[0]);
            }
            return std::nullopt;
        case IrBuiltinKind::Contains:
            if (arguments.size() == 2 && std::holds_alternative<StringValuePtr>(arguments[0]) &&
                (std::holds_alternative<StringValuePtr>(arguments[1]) || std::holds_alternative<char>(arguments[1]))) {
                return AuraBuiltinContains(arguments[0], arguments[1]);
            }
            return std::nullopt;
        case IrBuiltinKind::StartsWith:
            if (arguments.size() == 2 && std::holds_alternative<StringValuePtr>(arguments[0]) &&
                (std::holds_alternative<StringValuePtr>(arguments[1]) || std::holds_alternative<char>(arguments[1]))) {
                return AuraBuiltinStartsWith(arguments[0], arguments[1]);
            }
            return std::nullopt;
        case IrBuiltinKind::EndsWith:
            if (arguments.size() == 2 && std::holds_alternative<StringValuePtr>(arguments[0]) &&
                (std::holds_alternative<StringValuePtr>(arguments[1]) || std::holds_alternative<char>(arguments[1]))) {
                return AuraBuiltinEndsWith(arguments[0], arguments[1]);
            }
            return std::nullopt;
        case IrBuiltinKind::Join:
        case IrBuiltinKind::Print:
        case IrBuiltinKind::Push:
        case IrBuiltinKind::Pop:
        case IrBuiltinKind::Insert:
        case IrBuiltinKind::RemoveAt:
        case IrBuiltinKind::Clear:
        case IrBuiltinKind::FileExists:
        case IrBuiltinKind::ReadText:
        case IrBuiltinKind::Abs:
            if (arguments.size() == 1 && std::holds_alternative<long long>(arguments[0])) {
                return AuraBuiltinAbs(arguments[0]);
            }
            return std::nullopt;
        case IrBuiltinKind::Min:
            if (arguments.size() == 2 && std::holds_alternative<long long>(arguments[0]) &&
                std::holds_alternative<long long>(arguments[1])) {
                return AuraBuiltinMin(arguments[0], arguments[1]);
            }
            return std::nullopt;
        case IrBuiltinKind::Max:
            if (arguments.size() == 2 && std::holds_alternative<long long>(arguments[0]) &&
                std::holds_alternative<long long>(arguments[1])) {
                return AuraBuiltinMax(arguments[0], arguments[1]);
            }
            return std::nullopt;
        case IrBuiltinKind::Pow:
            if (arguments.size() == 2 && std::holds_alternative<long long>(arguments[0]) &&
                std::holds_alternative<long long>(arguments[1])) {
                return AuraBuiltinPow(arguments[0], arguments[1]);
            }
            return std::nullopt;
        case IrBuiltinKind::None:
            return std::nullopt;
        }
    } catch (const AuraError&) {
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<Value> TryEvaluateInstruction(const CfgFunctionDecl& function,
                                            const DefinitionMap& definitions,
                                            CfgBlockId block_id,
                                            std::size_t instruction_index,
                                            const CfgInstruction& instruction,
                                            const ConstantMap& constants) {
    if (instruction.result == kInvalidCfgValueId) {
        return std::nullopt;
    }

    std::vector<Value> inputs;
    inputs.reserve(instruction.inputs.size());
    bool all_inputs_constant = true;
    for (const CfgValueId input : instruction.inputs) {
        Value constant;
        if (!TryGetConstantValue(constants, input, constant)) {
            all_inputs_constant = false;
            inputs.emplace_back(std::monostate{});
            continue;
        }
        inputs.push_back(std::move(constant));
    }

    try {
        switch (instruction.kind) {
        case CfgInstructionKind::Literal:
            return IsFoldableLiteralValue(instruction.literal_value) ? std::optional<Value>(instruction.literal_value)
                                                                     : std::nullopt;
        case CfgInstructionKind::Copy:
            return std::nullopt;
        case CfgInstructionKind::Unary:
            if (all_inputs_constant && inputs.size() == 1) {
                const Value folded = AuraEvaluateUnary(instruction.token, inputs[0]);
                if (IsFoldableLiteralValue(folded)) {
                    return folded;
                }
            }
            return std::nullopt;
        case CfgInstructionKind::Binary:
            if (!all_inputs_constant || inputs.size() != 2) {
                return std::nullopt;
            }
            if (instruction.token == TokenType::AndAnd) {
                return Value{AuraExpectBool(inputs[0], "`&&`") && AuraExpectBool(inputs[1], "`&&`")};
            }
            if (instruction.token == TokenType::OrOr) {
                return Value{AuraExpectBool(inputs[0], "`||`") || AuraExpectBool(inputs[1], "`||`")};
            }
            if (instruction.token == TokenType::Slash && AuraExpectInteger(inputs[1], "`/`") == 0) {
                return std::nullopt;
            }
            {
                const Value folded = AuraEvaluateBinary(instruction.token, inputs[0], inputs[1]);
                if (IsFoldableLiteralValue(folded)) {
                    return folded;
                }
            }
            return std::nullopt;
        case CfgInstructionKind::Call:
            if (all_inputs_constant) {
                if (const auto folded = TryFoldBuiltin(instruction, inputs); folded.has_value()) {
                    return folded;
                }
            }
            if (const auto folded =
                    TryFoldBuiltinFromLocalProducer(function, definitions, block_id, instruction_index, instruction,
                                                   constants);
                folded.has_value()) {
                return folded;
            }
            return std::nullopt;
        case CfgInstructionKind::GetIndex:
            if (all_inputs_constant && instruction.name == "String" && inputs.size() == 2 &&
                std::holds_alternative<StringValuePtr>(inputs[0]) && std::holds_alternative<long long>(inputs[1])) {
                const Value folded = AuraIndexString(inputs[0], inputs[1]);
                if (IsFoldableLiteralValue(folded)) {
                    return folded;
                }
            }
            if (instruction.name != "String" && instruction.inputs.size() == 2) {
                const auto index = TryGetConstantInteger(constants, instruction.inputs[1]);
                if (index.has_value()) {
                    MaterializedFreshArrayValue materialized;
                    if (TryMaterializeFreshLocalArrayValueRecursive(function, definitions, block_id,
                                                                    instruction.inputs[0], constants, materialized) &&
                        IsFreshLocalArrayStable(function, block_id, instruction_index, materialized)) {
                        const Value folded = AuraIndexArray(materialized.value, Value{*index});
                        if (IsFoldableLiteralValue(folded)) {
                            return folded;
                        }
                    }
                }
            }
            return std::nullopt;
        case CfgInstructionKind::GetSlice:
            if (all_inputs_constant && instruction.name == "String" && !inputs.empty() &&
                std::holds_alternative<StringValuePtr>(inputs[0])) {
                const Value start = instruction.has_start ? inputs[1] : Value{};
                const Value end = instruction.has_end ? inputs.back() : Value{};
                const Value folded = AuraSliceString(inputs[0], instruction.has_start, start, instruction.has_end, end);
                if (IsFoldableLiteralValue(folded)) {
                    return folded;
                }
            }
            return std::nullopt;
        case CfgInstructionKind::Drop:
        case CfgInstructionKind::ArrayLiteral:
        case CfgInstructionKind::StructLiteral:
        case CfgInstructionKind::Range:
        case CfgInstructionKind::GetField:
        case CfgInstructionKind::AssignField:
        case CfgInstructionKind::AssignIndex:
            return std::nullopt;
        }
    } catch (const AuraError&) {
        return std::nullopt;
    }

    return std::nullopt;
}

bool AddOrConfirmConstant(ConstantMap& constants, CfgValueId value, const Value& constant) {
    const auto it = constants.find(value);
    if (it == constants.end()) {
        constants.emplace(value, constant);
        return true;
    }
    if (ConstantValuesEqual(it->second, constant)) {
        return false;
    }

    it->second = constant;
    return true;
}

std::vector<std::vector<const CfgBlockTarget*>> BuildPredecessorTargets(const CfgFunctionDecl& function) {
    std::vector<std::vector<const CfgBlockTarget*>> predecessors(function.blocks.size());

    for (const auto& block : function.blocks) {
        switch (block.terminator.kind) {
        case CfgTerminatorKind::Jump:
            predecessors[block.terminator.target.block].push_back(&block.terminator.target);
            break;
        case CfgTerminatorKind::Branch:
            predecessors[block.terminator.true_target.block].push_back(&block.terminator.true_target);
            predecessors[block.terminator.false_target.block].push_back(&block.terminator.false_target);
            break;
        case CfgTerminatorKind::Return:
        case CfgTerminatorKind::None:
            break;
        }
    }

    return predecessors;
}

ConstantMap AnalyzeConstants(const CfgFunctionDecl& function) {
    ConstantMap constants;
    const auto predecessors = BuildPredecessorTargets(function);
    const DefinitionMap definitions = BuildDefinitionMap(function);

    bool changed = true;
    while (changed) {
        changed = false;

        for (const auto& block : function.blocks) {
            const auto& block_predecessors = predecessors[block.id];
            for (std::size_t i = 0; i < block.parameters.size(); ++i) {
                if (block_predecessors.empty()) {
                    continue;
                }

                std::optional<Value> merged;
                bool all_constant = true;
                for (const CfgBlockTarget* predecessor : block_predecessors) {
                    if (i >= predecessor->arguments.size()) {
                        all_constant = false;
                        break;
                    }

                    Value candidate;
                    if (!TryGetConstantValue(constants, predecessor->arguments[i], candidate)) {
                        all_constant = false;
                        break;
                    }

                    if (!merged.has_value()) {
                        merged = std::move(candidate);
                    } else if (!ConstantValuesEqual(*merged, candidate)) {
                        all_constant = false;
                        break;
                    }
                }

                if (all_constant && merged.has_value()) {
                    if (AddOrConfirmConstant(constants, block.parameters[i].value, *merged)) {
                        changed = true;
                    }
                }
            }
        }

        for (const auto& block : function.blocks) {
            for (std::size_t instruction_index = 0; instruction_index < block.instructions.size(); ++instruction_index) {
                const auto& instruction = block.instructions[instruction_index];
                const auto folded =
                    TryEvaluateInstruction(function, definitions, block.id, instruction_index, instruction, constants);
                if (!folded.has_value()) {
                    continue;
                }
                if (AddOrConfirmConstant(constants, instruction.result, *folded)) {
                    changed = true;
                }
            }
        }
    }

    return constants;
}

bool RewriteFoldedInstructions(CfgFunctionDecl& function, const ConstantMap& constants) {
    bool changed = false;

    for (auto& block : function.blocks) {
        for (auto& instruction : block.instructions) {
            if (instruction.kind == CfgInstructionKind::Literal || instruction.kind == CfgInstructionKind::Copy ||
                instruction.result == kInvalidCfgValueId) {
                continue;
            }

            const auto it = constants.find(instruction.result);
            if (it == constants.end() || !IsFoldableLiteralValue(it->second)) {
                continue;
            }

            instruction.kind = CfgInstructionKind::Literal;
            instruction.literal_value = it->second;
            instruction.name.clear();
            instruction.token = TokenType::EndOfFile;
            instruction.has_start = false;
            instruction.has_end = false;
            instruction.call_kind = IrCallKind::UserFunction;
            instruction.builtin_kind = IrBuiltinKind::None;
            instruction.inputs.clear();
            instruction.fields.clear();
            changed = true;
        }
    }

    return changed;
}

void RewriteInstructionToLiteral(CfgInstruction& instruction, const Value& value) {
    instruction.kind = CfgInstructionKind::Literal;
    instruction.literal_value = value;
    instruction.name.clear();
    instruction.token = TokenType::EndOfFile;
    instruction.has_start = false;
    instruction.has_end = false;
    instruction.call_kind = IrCallKind::UserFunction;
    instruction.builtin_kind = IrBuiltinKind::None;
    instruction.inputs.clear();
    instruction.fields.clear();
}

CollectionShapeInfo MergeBlockParameterFact(const CfgBlockParameter& parameter,
                                           const std::vector<const CfgBlockTarget*>& predecessors,
                                           const TargetCollectionFacts& target_facts,
                                           std::size_t parameter_index) {
    const CollectionShapeInfo type_info = UnknownShapeInfoForType(parameter.type);
    if (type_info.kind == CollectionShapeKind::Unknown || predecessors.empty()) {
        return {};
    }

    CollectionShapeInfo merged = type_info;
    merged.initialized = false;
    bool all_whole = merged.kind == CollectionShapeKind::Array;
    bool have_shared_length = false;
    std::optional<long long> shared_length;
    bool same_length = true;
    bool saw_initialized_fact = false;

    for (const CfgBlockTarget* predecessor : predecessors) {
        const auto incoming_it = target_facts.find(predecessor);
        if (incoming_it == target_facts.end() || parameter_index >= incoming_it->second.size()) {
            continue;
        }

        const CollectionShapeInfo& incoming = incoming_it->second[parameter_index];
        if (!incoming.initialized || incoming.kind != merged.kind) {
            continue;
        }

        saw_initialized_fact = true;
        if (merged.kind == CollectionShapeKind::Array) {
            all_whole = all_whole && incoming.whole_array;
        }

        if (!incoming.known_length.has_value()) {
            same_length = false;
            continue;
        }

        if (!have_shared_length) {
            shared_length = incoming.known_length;
            have_shared_length = true;
        } else if (shared_length != incoming.known_length) {
            same_length = false;
        }
    }

    if (!saw_initialized_fact) {
        return {};
    }

    merged.initialized = true;
    if (merged.kind == CollectionShapeKind::Array) {
        merged.whole_array = all_whole;
    }
    if (have_shared_length && same_length) {
        merged.known_length = shared_length;
    }
    return merged;
}

void SeedLocalCollectionFacts(const CfgFunctionDecl& function,
                              const CfgBlock& block,
                              const BlockEntryCollectionFacts& entry_facts,
                              LocalCollectionFacts& local_facts) {
    if (block.id == function.entry_block) {
        for (const auto& parameter : function.parameters) {
            if (!IsCollectionLikeType(parameter.type)) {
                continue;
            }
            local_facts[parameter.value] = MakeLocalShape(parameter.value, UnknownShapeInfoForType(parameter.type));
        }
    }

    const auto& block_entry = entry_facts[block.id];
    for (std::size_t index = 0; index < block.parameters.size(); ++index) {
        const CollectionShapeInfo info = index < block_entry.size() ? block_entry[index] : CollectionShapeInfo{};
        if (!info.initialized || info.kind == CollectionShapeKind::Unknown) {
            continue;
        }
        local_facts[block.parameters[index].value] = MakeLocalShape(block.parameters[index].value, info);
    }
}

void UpdateWholeArrayRootLength(LocalCollectionFacts& local_facts,
                                CfgValueId root,
                                std::optional<long long> known_length) {
    for (auto& [_, fact] : local_facts) {
        if (fact.kind == CollectionShapeKind::Array && fact.root == root && fact.whole_array) {
            fact.known_length = known_length;
        }
    }
}

void MaybeSeedUnknownCollectionResult(const CfgInstruction& instruction, LocalCollectionFacts& local_facts) {
    if (instruction.result == kInvalidCfgValueId || !IsCollectionLikeType(instruction.type)) {
        return;
    }
    if (local_facts.find(instruction.result) != local_facts.end()) {
        return;
    }
    local_facts[instruction.result] = MakeLocalShape(instruction.result, UnknownShapeInfoForType(instruction.type));
}

void ApplyCollectionFactsForInstruction(const ConstantMap& constants,
                                        const CfgInstruction& instruction,
                                        LocalCollectionFacts& local_facts) {
    switch (instruction.kind) {
    case CfgInstructionKind::Literal:
        if (instruction.result != kInvalidCfgValueId && instruction.type == TypeInfo{TypeKind::String, ""}) {
            if (const auto* string_value = std::get_if<StringValuePtr>(&instruction.literal_value)) {
                local_facts[instruction.result] = {
                    CollectionShapeKind::String, instruction.result, false,
                    static_cast<long long>(StringLength(**string_value))};
            } else {
                local_facts[instruction.result] =
                    MakeLocalShape(instruction.result, UnknownShapeInfoForType(instruction.type));
            }
        }
        break;
    case CfgInstructionKind::Copy:
        if (instruction.result != kInvalidCfgValueId && instruction.inputs.size() == 1) {
            const auto input_it = local_facts.find(instruction.inputs[0]);
            if (input_it != local_facts.end()) {
                local_facts[instruction.result] = input_it->second;
            }
        }
        break;
    case CfgInstructionKind::ArrayLiteral:
        if (instruction.result != kInvalidCfgValueId) {
            local_facts[instruction.result] = {
                CollectionShapeKind::Array, instruction.result, true,
                static_cast<long long>(instruction.inputs.size())};
        }
        break;
    case CfgInstructionKind::Range:
        if (instruction.result != kInvalidCfgValueId && instruction.inputs.size() == 2) {
            local_facts[instruction.result] = {CollectionShapeKind::Array, instruction.result, true,
                                               TryComputeRangeLength(constants, instruction.inputs[0],
                                                                     instruction.inputs[1])};
        }
        break;
    case CfgInstructionKind::GetSlice:
        if (instruction.result != kInvalidCfgValueId && !instruction.inputs.empty()) {
            if (instruction.name == "String") {
                CollectionShapeInfo info = UnknownShapeInfoForType(instruction.type);
                const auto object_it = local_facts.find(instruction.inputs[0]);
                if (object_it != local_facts.end() && object_it->second.kind == CollectionShapeKind::String) {
                    info.known_length =
                        TryComputeSliceLength(object_it->second.known_length, instruction.has_start,
                                              instruction.has_start ? TryGetConstantInteger(constants, instruction.inputs[1])
                                                                    : std::optional<long long>{},
                                              instruction.has_end ? true : false,
                                              instruction.has_end ? TryGetConstantInteger(constants, instruction.inputs.back())
                                                                  : std::optional<long long>{});
                }
                local_facts[instruction.result] = MakeLocalShape(instruction.result, info);
            } else {
                CollectionShapeInfo info = UnknownShapeInfoForType(instruction.type);
                info.whole_array = false;
                const auto object_it = local_facts.find(instruction.inputs[0]);
                if (object_it != local_facts.end() && object_it->second.kind == CollectionShapeKind::Array) {
                    info.known_length =
                        TryComputeSliceLength(object_it->second.known_length, instruction.has_start,
                                              instruction.has_start ? TryGetConstantInteger(constants, instruction.inputs[1])
                                                                    : std::optional<long long>{},
                                              instruction.has_end ? true : false,
                                              instruction.has_end ? TryGetConstantInteger(constants, instruction.inputs.back())
                                                                  : std::optional<long long>{});
                }
                local_facts[instruction.result] = MakeLocalShape(instruction.result, info);
            }
        }
        break;
    case CfgInstructionKind::Call:
        if (instruction.call_kind == IrCallKind::Builtin && !instruction.inputs.empty()) {
            const auto array_it = local_facts.find(instruction.inputs[0]);
            if (array_it != local_facts.end() && array_it->second.kind == CollectionShapeKind::Array &&
                array_it->second.whole_array) {
                std::optional<long long> next_length = array_it->second.known_length;
                switch (instruction.builtin_kind) {
                case IrBuiltinKind::Push:
                case IrBuiltinKind::Insert:
                    if (next_length.has_value()) {
                        next_length = *next_length + 1;
                    }
                    UpdateWholeArrayRootLength(local_facts, array_it->second.root, next_length);
                    break;
                case IrBuiltinKind::Pop:
                case IrBuiltinKind::RemoveAt:
                    if (next_length.has_value() && *next_length > 0) {
                        next_length = *next_length - 1;
                    } else {
                        next_length.reset();
                    }
                    UpdateWholeArrayRootLength(local_facts, array_it->second.root, next_length);
                    break;
                case IrBuiltinKind::Clear:
                    UpdateWholeArrayRootLength(local_facts, array_it->second.root, 0LL);
                    break;
                case IrBuiltinKind::Len:
                case IrBuiltinKind::Contains:
                case IrBuiltinKind::StartsWith:
                case IrBuiltinKind::EndsWith:
                case IrBuiltinKind::Join:
                case IrBuiltinKind::Print:
                case IrBuiltinKind::FileExists:
                case IrBuiltinKind::ReadText:
                case IrBuiltinKind::Abs:
                case IrBuiltinKind::Min:
                case IrBuiltinKind::Max:
                case IrBuiltinKind::Pow:
                case IrBuiltinKind::None:
                    break;
                }
            }
        } else {
            for (const CfgValueId input : instruction.inputs) {
                const auto input_it = local_facts.find(input);
                if (input_it != local_facts.end() && input_it->second.kind == CollectionShapeKind::Array &&
                    input_it->second.whole_array) {
                    UpdateWholeArrayRootLength(local_facts, input_it->second.root, std::nullopt);
                }
            }
        }
        break;
    case CfgInstructionKind::Drop:
    case CfgInstructionKind::StructLiteral:
    case CfgInstructionKind::Unary:
    case CfgInstructionKind::Binary:
    case CfgInstructionKind::GetField:
    case CfgInstructionKind::GetIndex:
    case CfgInstructionKind::AssignField:
    case CfgInstructionKind::AssignIndex:
        break;
    }

    MaybeSeedUnknownCollectionResult(instruction, local_facts);
}

void RecordTargetCollectionFacts(const CfgBlockTarget& target,
                                 const LocalCollectionFacts& local_facts,
                                 TargetCollectionFacts& target_facts) {
    std::vector<CollectionShapeInfo> facts;
    facts.reserve(target.arguments.size());
    for (const CfgValueId argument : target.arguments) {
        const auto fact_it = local_facts.find(argument);
        facts.push_back(fact_it == local_facts.end() ? CollectionShapeInfo{} : ToShapeInfo(fact_it->second));
    }
    target_facts[&target] = std::move(facts);
}

bool MaybeRewriteKnownLengthBuiltin(CfgInstruction& instruction, const LocalCollectionFacts& local_facts) {
    if (instruction.kind != CfgInstructionKind::Call || instruction.call_kind != IrCallKind::Builtin ||
        instruction.builtin_kind != IrBuiltinKind::Len || instruction.inputs.size() != 1) {
        return false;
    }

    const auto fact_it = local_facts.find(instruction.inputs[0]);
    if (fact_it == local_facts.end() || !fact_it->second.known_length.has_value()) {
        return false;
    }

    RewriteInstructionToLiteral(instruction, Value{*fact_it->second.known_length});
    return true;
}

void SimulateBlockCollectionFacts(const ConstantMap& constants,
                                  CfgBlock& block,
                                  LocalCollectionFacts& local_facts,
                                  TargetCollectionFacts* target_facts,
                                  bool rewrite_known_lengths) {
    for (auto& instruction : block.instructions) {
        if (rewrite_known_lengths) {
            MaybeRewriteKnownLengthBuiltin(instruction, local_facts);
        }
        ApplyCollectionFactsForInstruction(constants, instruction, local_facts);
    }

    if (target_facts == nullptr) {
        return;
    }

    if (block.terminator.kind == CfgTerminatorKind::Jump) {
        RecordTargetCollectionFacts(block.terminator.target, local_facts, *target_facts);
    } else if (block.terminator.kind == CfgTerminatorKind::Branch) {
        RecordTargetCollectionFacts(block.terminator.true_target, local_facts, *target_facts);
        RecordTargetCollectionFacts(block.terminator.false_target, local_facts, *target_facts);
    }
}

BlockEntryCollectionFacts AnalyzeCollectionDataFlow(CfgFunctionDecl& function, const ConstantMap& constants) {
    const auto predecessors = BuildPredecessorTargets(function);
    BlockEntryCollectionFacts entry_facts(function.blocks.size());
    for (const auto& block : function.blocks) {
        entry_facts[block.id].resize(block.parameters.size());
    }

    TargetCollectionFacts current_target_facts;
    bool changed = true;
    while (changed) {
        changed = false;
        TargetCollectionFacts next_target_facts;

        for (auto& block : function.blocks) {
            std::vector<CollectionShapeInfo> merged_entry;
            merged_entry.reserve(block.parameters.size());
            for (std::size_t index = 0; index < block.parameters.size(); ++index) {
                merged_entry.push_back(
                    MergeBlockParameterFact(block.parameters[index], predecessors[block.id], current_target_facts, index));
            }
            if (!ShapeInfoVectorEquals(entry_facts[block.id], merged_entry)) {
                entry_facts[block.id] = std::move(merged_entry);
                changed = true;
            }

            LocalCollectionFacts local_facts;
            SeedLocalCollectionFacts(function, block, entry_facts, local_facts);
            SimulateBlockCollectionFacts(constants, block, local_facts, &next_target_facts, false);
        }

        if (!TargetCollectionFactsEqual(current_target_facts, next_target_facts)) {
            changed = true;
        }
        current_target_facts = std::move(next_target_facts);
    }

    return entry_facts;
}

bool FoldKnownCollectionLengths(CfgFunctionDecl& function, const ConstantMap& constants) {
    const BlockEntryCollectionFacts entry_facts = AnalyzeCollectionDataFlow(function, constants);
    bool changed = false;

    for (auto& block : function.blocks) {
        LocalCollectionFacts local_facts;
        SeedLocalCollectionFacts(function, block, entry_facts, local_facts);

        for (auto& instruction : block.instructions) {
            changed |= MaybeRewriteKnownLengthBuiltin(instruction, local_facts);
            ApplyCollectionFactsForInstruction(constants, instruction, local_facts);
        }
    }

    return changed;
}

CfgValueId ResolveAlias(CfgValueId value, const std::unordered_map<CfgValueId, CfgValueId>& aliases);

bool AddTrivialCopyAliases(const CfgFunctionDecl& function, std::unordered_map<CfgValueId, CfgValueId>& aliases) {
    bool changed = false;

    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            if (instruction.kind != CfgInstructionKind::Copy || instruction.inputs.size() != 1 ||
                !IsImmutableScalarLikeType(instruction.type)) {
                continue;
            }

            const CfgValueId resolved_input = ResolveAlias(instruction.inputs[0], aliases);
            const auto alias_it = aliases.find(instruction.result);
            if (alias_it != aliases.end() && alias_it->second == resolved_input) {
                continue;
            }

            aliases[instruction.result] = resolved_input;
            changed = true;
        }
    }

    return changed;
}

CfgValueId ResolveAlias(CfgValueId value, const std::unordered_map<CfgValueId, CfgValueId>& aliases) {
    CfgValueId current = value;
    while (true) {
        const auto it = aliases.find(current);
        if (it == aliases.end() || it->second == current) {
            return current;
        }
        current = it->second;
    }
}

void RewriteAliasedTarget(CfgBlockTarget& target, const std::unordered_map<CfgValueId, CfgValueId>& aliases) {
    for (CfgValueId& argument : target.arguments) {
        argument = ResolveAlias(argument, aliases);
    }
}

bool PropagateTrivialCopies(CfgFunctionDecl& function) {
    std::unordered_map<CfgValueId, CfgValueId> aliases;
    AddTrivialCopyAliases(function, aliases);

    if (aliases.empty()) {
        return false;
    }

    bool changed = false;
    for (auto& block : function.blocks) {
        for (auto& instruction : block.instructions) {
            if (instruction.kind == CfgInstructionKind::Copy && instruction.inputs.size() == 1 &&
                IsImmutableScalarLikeType(instruction.type)) {
                const CfgValueId resolved = ResolveAlias(instruction.inputs[0], aliases);
                if (instruction.inputs[0] != resolved) {
                    instruction.inputs[0] = resolved;
                    changed = true;
                }
                continue;
            }

            for (CfgValueId& input : instruction.inputs) {
                const CfgValueId resolved = ResolveAlias(input, aliases);
                if (input != resolved) {
                    input = resolved;
                    changed = true;
                }
            }
            for (auto& field : instruction.fields) {
                const CfgValueId resolved = ResolveAlias(field.value, aliases);
                if (field.value != resolved) {
                    field.value = resolved;
                    changed = true;
                }
            }
        }

        if (block.terminator.condition.has_value()) {
            const CfgValueId resolved = ResolveAlias(*block.terminator.condition, aliases);
            if (*block.terminator.condition != resolved) {
                block.terminator.condition = resolved;
                changed = true;
            }
        }
        RewriteAliasedTarget(block.terminator.target, aliases);
        RewriteAliasedTarget(block.terminator.true_target, aliases);
        RewriteAliasedTarget(block.terminator.false_target, aliases);
    }

    return changed;
}

bool SimplifyConstantBranches(CfgFunctionDecl& function, const ConstantMap& constants) {
    bool changed = false;

    for (auto& block : function.blocks) {
        if (block.terminator.kind != CfgTerminatorKind::Branch || !block.terminator.condition.has_value()) {
            continue;
        }

        const auto it = constants.find(*block.terminator.condition);
        if (it == constants.end()) {
            continue;
        }

        const auto* condition = std::get_if<bool>(&it->second);
        if (condition == nullptr) {
            continue;
        }

        const CfgBlockTarget selected_target = *condition ? block.terminator.true_target : block.terminator.false_target;
        block.terminator.kind = CfgTerminatorKind::Jump;
        block.terminator.condition.reset();
        block.terminator.target = selected_target;
        block.terminator.true_target = {};
        block.terminator.false_target = {};
        changed = true;
    }

    return changed;
}

bool RemoveUnreachableBlocks(CfgFunctionDecl& function) {
    std::vector<bool> reachable(function.blocks.size(), false);
    std::vector<CfgBlockId> stack = {function.entry_block};

    while (!stack.empty()) {
        const CfgBlockId block_id = stack.back();
        stack.pop_back();
        if (reachable[block_id]) {
            continue;
        }
        reachable[block_id] = true;

        const CfgBlock& block = function.blocks[block_id];
        switch (block.terminator.kind) {
        case CfgTerminatorKind::Jump:
            stack.push_back(block.terminator.target.block);
            break;
        case CfgTerminatorKind::Branch:
            stack.push_back(block.terminator.true_target.block);
            stack.push_back(block.terminator.false_target.block);
            break;
        case CfgTerminatorKind::Return:
        case CfgTerminatorKind::None:
            break;
        }
    }

    if (std::all_of(reachable.begin(), reachable.end(), [](bool value) { return value; })) {
        return false;
    }

    std::vector<CfgBlock> new_blocks;
    std::unordered_map<CfgBlockId, CfgBlockId> remap;
    new_blocks.reserve(function.blocks.size());

    for (const auto& block : function.blocks) {
        if (!reachable[block.id]) {
            continue;
        }
        remap[block.id] = new_blocks.size();
        new_blocks.push_back(block);
    }

    for (std::size_t i = 0; i < new_blocks.size(); ++i) {
        CfgBlock& block = new_blocks[i];
        block.id = i;
        if (block.terminator.kind == CfgTerminatorKind::Jump) {
            block.terminator.target.block = remap.at(block.terminator.target.block);
        } else if (block.terminator.kind == CfgTerminatorKind::Branch) {
            block.terminator.true_target.block = remap.at(block.terminator.true_target.block);
            block.terminator.false_target.block = remap.at(block.terminator.false_target.block);
        }
    }

    function.entry_block = remap.at(function.entry_block);
    function.blocks = std::move(new_blocks);
    return true;
}

bool RemoveTrivialDrops(CfgFunctionDecl& function) {
    bool changed = false;
    std::unordered_map<CfgValueId, TypeInfo> value_types;

    for (const auto& parameter : function.parameters) {
        value_types.emplace(parameter.value, parameter.type);
    }
    for (const auto& block : function.blocks) {
        for (const auto& parameter : block.parameters) {
            value_types.emplace(parameter.value, parameter.type);
        }
        for (const auto& instruction : block.instructions) {
            if (instruction.result != kInvalidCfgValueId) {
                value_types[instruction.result] = instruction.type;
            }
        }
    }

    for (auto& block : function.blocks) {
        std::vector<CfgInstruction> kept;
        kept.reserve(block.instructions.size());
        for (auto& instruction : block.instructions) {
            if (instruction.kind == CfgInstructionKind::Drop && instruction.inputs.size() == 1) {
                const auto type_it = value_types.find(instruction.inputs[0]);
                if (type_it != value_types.end() && IsImmutableScalarLikeType(type_it->second)) {
                    changed = true;
                    continue;
                }
            }
            kept.push_back(std::move(instruction));
        }
        block.instructions = std::move(kept);
    }

    return changed;
}

bool TryRewriteTargetThroughPassthrough(const CfgFunctionDecl& function, CfgBlockTarget& target) {
    const CfgBlock& block = function.blocks[target.block];
    if (!block.instructions.empty() || block.terminator.kind != CfgTerminatorKind::Jump) {
        return false;
    }
    if (block.parameters.size() != target.arguments.size()) {
        return false;
    }

    std::vector<CfgValueId> rewritten_arguments;
    rewritten_arguments.reserve(block.terminator.target.arguments.size());
    for (const CfgValueId argument : block.terminator.target.arguments) {
        bool matched = false;
        for (std::size_t i = 0; i < block.parameters.size(); ++i) {
            if (block.parameters[i].value == argument) {
                rewritten_arguments.push_back(target.arguments[i]);
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }

    target.block = block.terminator.target.block;
    target.arguments = std::move(rewritten_arguments);
    return true;
}

bool TryRewriteJumpToReturn(const CfgFunctionDecl& function, CfgTerminator& terminator) {
    if (terminator.kind != CfgTerminatorKind::Jump) {
        return false;
    }

    const CfgBlock& target_block = function.blocks[terminator.target.block];
    if (!target_block.instructions.empty() || target_block.terminator.kind != CfgTerminatorKind::Return) {
        return false;
    }

    std::optional<CfgValueId> return_value;
    if (target_block.terminator.return_value.has_value()) {
        bool matched = false;
        for (std::size_t i = 0; i < target_block.parameters.size(); ++i) {
            if (target_block.parameters[i].value == *target_block.terminator.return_value) {
                if (i >= terminator.target.arguments.size()) {
                    return false;
                }
                return_value = terminator.target.arguments[i];
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }

    terminator.kind = CfgTerminatorKind::Return;
    terminator.return_value = return_value;
    terminator.target = {};
    return true;
}

bool CanonicalizeTrivialControlFlow(CfgFunctionDecl& function) {
    bool changed = false;

    for (auto& block : function.blocks) {
        if (block.terminator.kind == CfgTerminatorKind::Jump) {
            while (TryRewriteTargetThroughPassthrough(function, block.terminator.target)) {
                changed = true;
            }
            if (TryRewriteJumpToReturn(function, block.terminator)) {
                changed = true;
            }
            continue;
        }

        if (block.terminator.kind == CfgTerminatorKind::Branch) {
            while (TryRewriteTargetThroughPassthrough(function, block.terminator.true_target)) {
                changed = true;
            }
            while (TryRewriteTargetThroughPassthrough(function, block.terminator.false_target)) {
                changed = true;
            }

            if (block.terminator.true_target.block == block.terminator.false_target.block &&
                block.terminator.true_target.arguments == block.terminator.false_target.arguments) {
                block.terminator.kind = CfgTerminatorKind::Jump;
                block.terminator.target = block.terminator.true_target;
                block.terminator.condition.reset();
                block.terminator.true_target = {};
                block.terminator.false_target = {};
                changed = true;
            }
        }
    }

    return changed;
}

bool MaterializeConstantBlockParameters(CfgFunctionDecl& function, const ConstantMap& constants) {
    bool changed = false;

    for (auto& block : function.blocks) {
        std::vector<std::pair<CfgValueId, CfgValueId>> rewrites;
        std::vector<CfgInstruction> lifted_literals;

        for (const auto& parameter : block.parameters) {
            const auto constant_it = constants.find(parameter.value);
            if (constant_it == constants.end() || !IsFoldableLiteralValue(constant_it->second)) {
                continue;
            }

            CfgInstruction literal;
            literal.location = block.terminator.location.file_path.empty() ? function.location : block.terminator.location;
            literal.result = function.value_count++;
            literal.type = parameter.type;
            literal.kind = CfgInstructionKind::Literal;
            literal.literal_value = constant_it->second;
            rewrites.push_back({parameter.value, literal.result});
            lifted_literals.push_back(std::move(literal));
        }

        if (rewrites.empty()) {
            continue;
        }

        const auto rewrite_value = [&](CfgValueId& value) {
            for (const auto& [from, to] : rewrites) {
                if (value == from) {
                    value = to;
                    return;
                }
            }
        };

        for (auto& instruction : block.instructions) {
            for (CfgValueId& input : instruction.inputs) {
                rewrite_value(input);
            }
            for (auto& field : instruction.fields) {
                rewrite_value(field.value);
            }
        }

        if (block.terminator.condition.has_value()) {
            rewrite_value(*block.terminator.condition);
        }
        if (block.terminator.return_value.has_value()) {
            rewrite_value(*block.terminator.return_value);
        }
        for (CfgValueId& argument : block.terminator.target.arguments) {
            rewrite_value(argument);
        }
        for (CfgValueId& argument : block.terminator.true_target.arguments) {
            rewrite_value(argument);
        }
        for (CfgValueId& argument : block.terminator.false_target.arguments) {
            rewrite_value(argument);
        }

        std::vector<CfgInstruction> rewritten_instructions;
        rewritten_instructions.reserve(lifted_literals.size() + block.instructions.size());
        for (auto& literal : lifted_literals) {
            rewritten_instructions.push_back(std::move(literal));
        }
        for (auto& instruction : block.instructions) {
            rewritten_instructions.push_back(std::move(instruction));
        }
        block.instructions = std::move(rewritten_instructions);
        changed = true;
    }

    return changed;
}

void CollectLiveInputs(const CfgInstruction& instruction, std::unordered_set<CfgValueId>& live_values) {
    for (const CfgValueId input : instruction.inputs) {
        live_values.insert(input);
    }
    for (const auto& field : instruction.fields) {
        live_values.insert(field.value);
    }
}

void CollectLiveTerminatorValues(const CfgTerminator& terminator, std::unordered_set<CfgValueId>& live_values) {
    if (terminator.condition.has_value()) {
        live_values.insert(*terminator.condition);
    }
    if (terminator.return_value.has_value()) {
        live_values.insert(*terminator.return_value);
    }
    for (const CfgValueId argument : terminator.target.arguments) {
        live_values.insert(argument);
    }
    for (const CfgValueId argument : terminator.true_target.arguments) {
        live_values.insert(argument);
    }
    for (const CfgValueId argument : terminator.false_target.arguments) {
        live_values.insert(argument);
    }
}

bool RemoveUnusedBlockParameters(CfgFunctionDecl& function) {
    std::vector<std::vector<std::size_t>> kept_parameter_indices(function.blocks.size());

    for (const auto& block : function.blocks) {
        std::unordered_set<CfgValueId> live_values;
        for (const auto& instruction : block.instructions) {
            CollectLiveInputs(instruction, live_values);
        }
        CollectLiveTerminatorValues(block.terminator, live_values);

        auto& kept = kept_parameter_indices[block.id];
        for (std::size_t index = 0; index < block.parameters.size(); ++index) {
            if (live_values.find(block.parameters[index].value) != live_values.end()) {
                kept.push_back(index);
            }
        }
    }

    bool changed = false;
    for (const auto& block : function.blocks) {
        if (kept_parameter_indices[block.id].size() != block.parameters.size()) {
            changed = true;
            break;
        }
    }
    if (!changed) {
        return false;
    }

    const auto rewrite_target = [&](CfgBlockTarget& target) {
        const auto& kept = kept_parameter_indices[target.block];
        if (kept.size() == target.arguments.size()) {
            return;
        }

        std::vector<CfgValueId> rewritten_arguments;
        rewritten_arguments.reserve(kept.size());
        for (const std::size_t kept_index : kept) {
            if (kept_index < target.arguments.size()) {
                rewritten_arguments.push_back(target.arguments[kept_index]);
            }
        }
        target.arguments = std::move(rewritten_arguments);
    };

    for (auto& block : function.blocks) {
        if (block.terminator.kind == CfgTerminatorKind::Jump) {
            rewrite_target(block.terminator.target);
        } else if (block.terminator.kind == CfgTerminatorKind::Branch) {
            rewrite_target(block.terminator.true_target);
            rewrite_target(block.terminator.false_target);
        }
    }

    for (auto& block : function.blocks) {
        const auto& kept = kept_parameter_indices[block.id];
        if (kept.size() == block.parameters.size()) {
            continue;
        }

        std::vector<CfgBlockParameter> rewritten_parameters;
        rewritten_parameters.reserve(kept.size());
        for (const std::size_t kept_index : kept) {
            rewritten_parameters.push_back(block.parameters[kept_index]);
        }
        block.parameters = std::move(rewritten_parameters);
    }

    return true;
}

void MarkTargetLive(const CfgBlockTarget& target, std::unordered_set<CfgValueId>& live_values) {
    for (const CfgValueId argument : target.arguments) {
        live_values.insert(argument);
    }
}

void MarkInstructionInputsLive(const CfgInstruction& instruction, std::unordered_set<CfgValueId>& live_values) {
    for (const CfgValueId input : instruction.inputs) {
        live_values.insert(input);
    }
    for (const auto& field : instruction.fields) {
        live_values.insert(field.value);
    }
}

bool IsInstructionRemovableIfUnused(const CfgInstruction& instruction) {
    switch (instruction.kind) {
    case CfgInstructionKind::Literal:
    case CfgInstructionKind::Copy:
    case CfgInstructionKind::ArrayLiteral:
    case CfgInstructionKind::StructLiteral:
    case CfgInstructionKind::Unary:
    case CfgInstructionKind::Range:
    case CfgInstructionKind::GetField:
        return true;
    case CfgInstructionKind::Binary:
        return instruction.token != TokenType::Slash;
    case CfgInstructionKind::Drop:
    case CfgInstructionKind::Call:
    case CfgInstructionKind::GetIndex:
    case CfgInstructionKind::GetSlice:
    case CfgInstructionKind::AssignField:
    case CfgInstructionKind::AssignIndex:
        return false;
    }

    return false;
}

bool RemoveDeadInstructions(CfgFunctionDecl& function) {
    bool changed = false;

    for (auto& block : function.blocks) {
        std::unordered_set<CfgValueId> live_values;
        switch (block.terminator.kind) {
        case CfgTerminatorKind::Jump:
            MarkTargetLive(block.terminator.target, live_values);
            break;
        case CfgTerminatorKind::Branch:
            if (block.terminator.condition.has_value()) {
                live_values.insert(*block.terminator.condition);
            }
            MarkTargetLive(block.terminator.true_target, live_values);
            MarkTargetLive(block.terminator.false_target, live_values);
            break;
        case CfgTerminatorKind::Return:
            if (block.terminator.return_value.has_value()) {
                live_values.insert(*block.terminator.return_value);
            }
            break;
        case CfgTerminatorKind::None:
            break;
        }

        std::vector<CfgInstruction> kept;
        kept.reserve(block.instructions.size());
        for (auto it = block.instructions.rbegin(); it != block.instructions.rend(); ++it) {
            const bool has_result = it->result != kInvalidCfgValueId;
            const bool result_live = has_result && live_values.find(it->result) != live_values.end();
            const bool keep = !has_result || result_live || !IsInstructionRemovableIfUnused(*it);
            if (!keep) {
                changed = true;
                continue;
            }

            if (has_result) {
                live_values.erase(it->result);
            }
            MarkInstructionInputsLive(*it, live_values);
            kept.push_back(*it);
        }

        std::reverse(kept.begin(), kept.end());
        block.instructions = std::move(kept);
    }

    return changed;
}

bool OptimizeCfgFunction(CfgFunctionDecl& function) {
    bool any_change = false;

    while (true) {
        bool changed = false;
        const ConstantMap constants = AnalyzeConstants(function);
        changed |= RewriteFoldedInstructions(function, constants);
        changed |= FoldKnownCollectionLengths(function, constants);
        changed |= SimplifyConstantBranches(function, constants);
        changed |= MaterializeConstantBlockParameters(function, constants);
        changed |= PropagateTrivialCopies(function);
        changed |= RemoveUnusedBlockParameters(function);
        changed |= CanonicalizeTrivialControlFlow(function);
        changed |= RemoveTrivialDrops(function);
        changed |= RemoveUnreachableBlocks(function);
        changed |= RemoveDeadInstructions(function);
        any_change |= changed;
        if (!changed) {
            break;
        }
    }

    return any_change;
}

}  // namespace

void OptimizeCfgProgram(CfgProgram& program) {
    for (auto& function : program.functions) {
        OptimizeCfgFunction(function);
    }
}
