#include "cfg_ir.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {

constexpr const char* kForIterableBindingName = "@for_iterable";
constexpr const char* kForCounterBindingName = "@for_counter";

struct VisibleBinding {
    std::string name;
    TypeInfo type{TypeKind::Unit, ""};
    CfgValueId value = 0;
};

using ScopeFrame = std::unordered_map<std::string, VisibleBinding>;

class LoweringScopeStack {
  public:
    void Push() {
        scopes_.emplace_back();
    }

    void Pop() {
        if (!scopes_.empty()) {
            scopes_.pop_back();
        }
    }

    ScopeFrame PopFrame() {
        if (scopes_.empty()) {
            return {};
        }
        ScopeFrame frame = std::move(scopes_.back());
        scopes_.pop_back();
        return frame;
    }

    void Declare(const std::string& name, const TypeInfo& type, CfgValueId value) {
        if (scopes_.empty()) {
            Push();
        }
        scopes_.back()[name] = VisibleBinding{name, type, value};
    }

    VisibleBinding Lookup(const std::string& name, const SourceLocation& location) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            const auto found = it->find(name);
            if (found != it->end()) {
                return found->second;
            }
        }
        throw BuildLocationError(location, "CFG lowering could not resolve `" + name + "`");
    }

    void Assign(const std::string& name, CfgValueId value, const SourceLocation& location) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            const auto found = it->find(name);
            if (found != it->end()) {
                found->second.value = value;
                return;
            }
        }
        throw BuildLocationError(location, "CFG lowering could not assign `" + name + "`");
    }

    std::vector<VisibleBinding> SnapshotVisible() const {
        std::unordered_set<std::string> seen;
        std::vector<std::string> names;

        for (auto scope_it = scopes_.rbegin(); scope_it != scopes_.rend(); ++scope_it) {
            for (const auto& [name, _] : *scope_it) {
                if (seen.insert(name).second) {
                    names.push_back(name);
                }
            }
        }

        std::sort(names.begin(), names.end());

        std::vector<VisibleBinding> snapshot;
        snapshot.reserve(names.size());
        for (const std::string& name : names) {
            for (auto scope_it = scopes_.rbegin(); scope_it != scopes_.rend(); ++scope_it) {
                const auto found = scope_it->find(name);
                if (found != scope_it->end()) {
                    snapshot.push_back(found->second);
                    break;
                }
            }
        }
        return snapshot;
    }

  private:
    std::vector<ScopeFrame> scopes_;
};

struct BlockBindingBundle {
    CfgBlockId block = 0;
    std::vector<VisibleBinding> bindings;
};

std::string ValueName(CfgValueId value) {
    return "%" + std::to_string(value);
}

std::string BlockName(CfgBlockId block) {
    return "block" + std::to_string(block);
}

std::string Indent(int depth) {
    return std::string(static_cast<std::size_t>(depth) * 2, ' ');
}

std::string EscapeString(const std::string& text) {
    std::ostringstream buffer;
    for (const unsigned char c : text) {
        switch (c) {
        case '\\':
            buffer << "\\\\";
            break;
        case '"':
            buffer << "\\\"";
            break;
        case '\n':
            buffer << "\\n";
            break;
        case '\r':
            buffer << "\\r";
            break;
        case '\t':
            buffer << "\\t";
            break;
        default:
            buffer << static_cast<char>(c);
            break;
        }
    }
    return buffer.str();
}

std::string FormatLiteralValue(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return "unit";
    }
    if (const auto* integer = std::get_if<long long>(&value)) {
        return std::to_string(*integer);
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean ? "true" : "false";
    }
    if (const auto* char_value = std::get_if<char>(&value)) {
        if (*char_value == '\\') {
            return "'\\\\'";
        }
        if (*char_value == '\'') {
            return "'\\''";
        }
        return std::string("'") + *char_value + "'";
    }
    if (const auto* string_value = std::get_if<StringValuePtr>(&value)) {
        return "\"" + EscapeString(StringToString(**string_value)) + "\"";
    }
    return ValueToString(value);
}

std::string FormatInstruction(const CfgInstruction& instruction) {
    std::ostringstream buffer;
    buffer << ValueName(instruction.result) << " = ";

    switch (instruction.kind) {
    case CfgInstructionKind::Literal:
        buffer << "literal " << FormatLiteralValue(instruction.literal_value);
        break;
    case CfgInstructionKind::Copy:
        buffer << "copy " << ValueName(instruction.inputs.at(0));
        break;
    case CfgInstructionKind::Drop:
        return "drop " + ValueName(instruction.inputs.at(0));
    case CfgInstructionKind::ArrayLiteral:
        buffer << "array<" << instruction.name << "> [";
        for (std::size_t i = 0; i < instruction.inputs.size(); ++i) {
            if (i > 0) {
                buffer << ", ";
            }
            buffer << ValueName(instruction.inputs[i]);
        }
        buffer << "]";
        break;
    case CfgInstructionKind::StructLiteral:
        buffer << "struct " << instruction.name << " { ";
        for (std::size_t i = 0; i < instruction.fields.size(); ++i) {
            if (i > 0) {
                buffer << ", ";
            }
            buffer << instruction.fields[i].name << ": " << ValueName(instruction.fields[i].value);
        }
        buffer << " }";
        break;
    case CfgInstructionKind::Unary:
        buffer << TokenTypeName(instruction.token) << ValueName(instruction.inputs.at(0));
        break;
    case CfgInstructionKind::Binary:
        buffer << ValueName(instruction.inputs.at(0)) << " " << TokenTypeName(instruction.token) << " "
               << ValueName(instruction.inputs.at(1));
        break;
    case CfgInstructionKind::Range:
        buffer << "range " << ValueName(instruction.inputs.at(0)) << ".." << ValueName(instruction.inputs.at(1));
        break;
    case CfgInstructionKind::Call:
        if (instruction.call_kind == IrCallKind::Builtin) {
            buffer << "builtin " << instruction.name;
        } else {
            buffer << "call " << instruction.name;
        }
        buffer << "(";
        for (std::size_t i = 0; i < instruction.inputs.size(); ++i) {
            if (i > 0) {
                buffer << ", ";
            }
            buffer << ValueName(instruction.inputs[i]);
        }
        buffer << ")";
        break;
    case CfgInstructionKind::GetField:
        buffer << "field " << ValueName(instruction.inputs.at(0)) << "." << instruction.name;
        break;
    case CfgInstructionKind::GetIndex:
        buffer << instruction.name << " index " << ValueName(instruction.inputs.at(0)) << "["
               << ValueName(instruction.inputs.at(1)) << "]";
        break;
    case CfgInstructionKind::GetSlice:
        buffer << instruction.name << " slice " << ValueName(instruction.inputs.at(0)) << "[";
        if (instruction.has_start) {
            buffer << ValueName(instruction.inputs.at(1));
        }
        buffer << ":";
        if (instruction.has_end) {
            buffer << ValueName(instruction.inputs.back());
        }
        buffer << "]";
        break;
    case CfgInstructionKind::AssignField:
        buffer << "assign-field " << ValueName(instruction.inputs.at(0)) << "." << instruction.name << " = "
               << ValueName(instruction.inputs.at(1));
        break;
    case CfgInstructionKind::AssignIndex:
        buffer << "assign-index " << ValueName(instruction.inputs.at(0)) << "[" << ValueName(instruction.inputs.at(1))
               << "] = " << ValueName(instruction.inputs.at(2));
        break;
    }

    buffer << " : " << TypeInfoName(instruction.type);
    return buffer.str();
}

std::string FormatTarget(const CfgBlockTarget& target) {
    std::ostringstream buffer;
    buffer << BlockName(target.block);
    if (!target.arguments.empty()) {
        buffer << "(";
        for (std::size_t i = 0; i < target.arguments.size(); ++i) {
            if (i > 0) {
                buffer << ", ";
            }
            buffer << ValueName(target.arguments[i]);
        }
        buffer << ")";
    }
    return buffer.str();
}

bool ExpressionProducesOwnedValue(const IrExpr* expr) {
    if (dynamic_cast<const IrVariableExpr*>(expr) != nullptr) {
        return false;
    }

    if (const auto* assign = dynamic_cast<const IrAssignExpr*>(expr)) {
        return dynamic_cast<const IrVariableExpr*>(assign->target.get()) == nullptr;
    }

    return true;
}

CfgTerminator MakeEmptyTerminator(const SourceLocation& location) {
    CfgTerminator terminator;
    terminator.location = location;
    return terminator;
}

class CfgFunctionBuilder {
  public:
    explicit CfgFunctionBuilder(const IrFunctionDecl& function) : function_(function) {}

    CfgFunctionDecl Lower() {
        cfg_function_.location = function_.location;
        cfg_function_.name = function_.name;
        cfg_function_.module_name = function_.module_name;
        cfg_function_.owner_type_name = function_.owner_type_name;
        cfg_function_.full_name = function_.full_name;
        cfg_function_.return_type = function_.return_type;

        scopes_.Push();
        current_block_ = CreateBlock("entry");
        cfg_function_.entry_block = current_block_;
        current_block_active_ = true;

        for (const auto& parameter : function_.parameters) {
            const CfgValueId value = NextValueId();
            cfg_function_.parameters.push_back(CfgFunctionParameter{parameter.name, parameter.type, value});
            scopes_.Declare(parameter.name, parameter.type, value);
        }

        for (const auto& statement : function_.body) {
            if (!current_block_active_) {
                break;
            }
            LowerStatement(statement.get());
        }

        if (current_block_active_) {
            if (cfg_function_.return_type == TypeInfo{TypeKind::Unit, ""}) {
                SetReturn(std::nullopt, function_.location);
            } else {
                throw BuildLocationError(function_.location,
                                         "CFG lowering reached the end of `" + function_.full_name +
                                             "` without a return value");
            }
        }

        cfg_function_.value_count = next_value_id_;
        scopes_.Pop();
        return cfg_function_;
    }

  private:
    const IrFunctionDecl& function_;
    CfgFunctionDecl cfg_function_;
    LoweringScopeStack scopes_;
    CfgBlockId current_block_ = 0;
    bool current_block_active_ = false;
    CfgValueId next_value_id_ = 0;

    CfgValueId NextValueId() {
        return next_value_id_++;
    }

    CfgBlock& CurrentBlock() {
        return cfg_function_.blocks.at(current_block_);
    }

    const CfgBlock& GetBlock(CfgBlockId block) const {
        return cfg_function_.blocks.at(block);
    }

    CfgBlockId CreateBlock(const std::string& label) {
        const CfgBlockId id = cfg_function_.blocks.size();
        cfg_function_.blocks.push_back(CfgBlock{id, label, {}, {}, MakeEmptyTerminator(function_.location)});
        return id;
    }

    void ActivateBlock(CfgBlockId block) {
        current_block_ = block;
        current_block_active_ = true;
    }

    CfgValueId EmitInstruction(CfgInstruction instruction) {
        instruction.result = NextValueId();
        CurrentBlock().instructions.push_back(std::move(instruction));
        return CurrentBlock().instructions.back().result;
    }

    void AppendSideEffectInstruction(CfgInstruction instruction) {
        CurrentBlock().instructions.push_back(std::move(instruction));
    }

    CfgValueId EmitLiteral(const SourceLocation& location, const TypeInfo& type, Value value) {
        CfgInstruction instruction;
        instruction.location = location;
        instruction.type = type;
        instruction.kind = CfgInstructionKind::Literal;
        instruction.literal_value = std::move(value);
        return EmitInstruction(std::move(instruction));
    }

    CfgValueId EmitCopy(const SourceLocation& location, const TypeInfo& type, CfgValueId source) {
        CfgInstruction instruction;
        instruction.location = location;
        instruction.type = type;
        instruction.kind = CfgInstructionKind::Copy;
        instruction.inputs.push_back(source);
        return EmitInstruction(std::move(instruction));
    }

    void EmitDrop(const SourceLocation& location, CfgValueId value) {
        if (value == kInvalidCfgValueId) {
            return;
        }
        CfgInstruction instruction;
        instruction.location = location;
        instruction.kind = CfgInstructionKind::Drop;
        instruction.inputs.push_back(value);
        AppendSideEffectInstruction(std::move(instruction));
    }

    CfgBlockTarget MakeTarget(CfgBlockId block, std::vector<CfgValueId> arguments = {}) const {
        return CfgBlockTarget{block, std::move(arguments)};
    }

    BlockBindingBundle CreateVisibleParameterBlock(const std::string& label,
                                                   const std::vector<VisibleBinding>& bindings) {
        std::vector<std::pair<std::string, TypeInfo>> specs;
        specs.reserve(bindings.size());
        for (const auto& binding : bindings) {
            specs.emplace_back(binding.name, binding.type);
        }
        return CreateParameterBlock(label, specs);
    }

    BlockBindingBundle CreateParameterBlock(const std::string& label,
                                            const std::vector<std::pair<std::string, TypeInfo>>& specs) {
        BlockBindingBundle bundle;
        bundle.block = CreateBlock(label);
        CfgBlock& block = cfg_function_.blocks.at(bundle.block);
        for (const auto& [name, type] : specs) {
            const CfgValueId value = NextValueId();
            block.parameters.push_back(CfgBlockParameter{value, name, type});
            bundle.bindings.push_back(VisibleBinding{name, type, value});
        }
        return bundle;
    }

    std::vector<CfgValueId> ResolveArgumentsFor(const std::vector<VisibleBinding>& bindings,
                                                const SourceLocation& location) const {
        std::vector<CfgValueId> arguments;
        arguments.reserve(bindings.size());
        for (const auto& binding : bindings) {
            arguments.push_back(scopes_.Lookup(binding.name, location).value);
        }
        return arguments;
    }

    void RebindVisible(const std::vector<VisibleBinding>& bindings, const SourceLocation& location) {
        for (const auto& binding : bindings) {
            scopes_.Assign(binding.name, binding.value, location);
        }
    }

    void SetJump(CfgBlockTarget target, const SourceLocation& location) {
        CurrentBlock().terminator = MakeEmptyTerminator(location);
        CurrentBlock().terminator.kind = CfgTerminatorKind::Jump;
        CurrentBlock().terminator.target = std::move(target);
        current_block_active_ = false;
    }

    void SetBranch(CfgValueId condition,
                   CfgBlockTarget true_target,
                   CfgBlockTarget false_target,
                   const SourceLocation& location) {
        CurrentBlock().terminator = MakeEmptyTerminator(location);
        CurrentBlock().terminator.kind = CfgTerminatorKind::Branch;
        CurrentBlock().terminator.condition = condition;
        CurrentBlock().terminator.true_target = std::move(true_target);
        CurrentBlock().terminator.false_target = std::move(false_target);
        current_block_active_ = false;
    }

    void SetReturn(std::optional<CfgValueId> return_value, const SourceLocation& location) {
        CurrentBlock().terminator = MakeEmptyTerminator(location);
        CurrentBlock().terminator.kind = CfgTerminatorKind::Return;
        CurrentBlock().terminator.return_value = return_value;
        current_block_active_ = false;
    }

    void LowerScopedStatement(const IrStmt* stmt) {
        scopes_.Push();
        LowerStatement(stmt);
        const ScopeFrame dropped = scopes_.PopFrame();
        if (!current_block_active_) {
            return;
        }
        for (const auto& [_, binding] : dropped) {
            EmitDrop(stmt->location, binding.value);
        }
    }

    void LowerStatement(const IrStmt* stmt) {
        if (const auto* block_stmt = dynamic_cast<const IrBlockStmt*>(stmt)) {
            scopes_.Push();
            for (const auto& statement : block_stmt->statements) {
                if (!current_block_active_) {
                    break;
                }
                LowerStatement(statement.get());
            }
            const ScopeFrame dropped = scopes_.PopFrame();
            if (current_block_active_) {
                for (const auto& [_, binding] : dropped) {
                    EmitDrop(block_stmt->location, binding.value);
                }
            }
            return;
        }

        if (const auto* let_stmt = dynamic_cast<const IrLetStmt*>(stmt)) {
            const CfgValueId initializer = LowerExpression(let_stmt->initializer.get());
            const CfgValueId binding = EmitCopy(let_stmt->location, let_stmt->variable_type, initializer);
            if (ExpressionProducesOwnedValue(let_stmt->initializer.get())) {
                EmitDrop(let_stmt->location, initializer);
            }
            scopes_.Declare(let_stmt->name, let_stmt->variable_type, binding);
            return;
        }

        if (const auto* return_stmt = dynamic_cast<const IrReturnStmt*>(stmt)) {
            if (return_stmt->value) {
                SetReturn(LowerExpression(return_stmt->value.get()), return_stmt->location);
            } else {
                SetReturn(std::nullopt, return_stmt->location);
            }
            return;
        }

        if (const auto* if_stmt = dynamic_cast<const IrIfStmt*>(stmt)) {
            LowerIf(*if_stmt);
            return;
        }

        if (const auto* while_stmt = dynamic_cast<const IrWhileStmt*>(stmt)) {
            LowerWhile(*while_stmt);
            return;
        }

        if (const auto* for_stmt = dynamic_cast<const IrForStmt*>(stmt)) {
            LowerFor(*for_stmt);
            return;
        }

        if (const auto* expr_stmt = dynamic_cast<const IrExprStmt*>(stmt)) {
            (void)LowerExpression(expr_stmt->expression.get());
            return;
        }

        throw BuildLocationError(stmt->location, "CFG lowering failed for an unknown statement kind");
    }

    void LowerIf(const IrIfStmt& if_stmt) {
        const CfgValueId condition = LowerExpression(if_stmt.condition.get());
        const std::vector<VisibleBinding> outer_bindings = scopes_.SnapshotVisible();
        const BlockBindingBundle join = CreateVisibleParameterBlock("if_join", outer_bindings);
        const BlockBindingBundle then_bundle = CreateVisibleParameterBlock("if_then", outer_bindings);

        bool has_fallthrough = false;
        if (if_stmt.else_branch) {
            const BlockBindingBundle else_bundle = CreateVisibleParameterBlock("if_else", outer_bindings);
            const std::vector<CfgValueId> branch_args = ResolveArgumentsFor(outer_bindings, if_stmt.location);
            SetBranch(condition,
                      MakeTarget(then_bundle.block, branch_args),
                      MakeTarget(else_bundle.block, branch_args),
                      if_stmt.location);

            ActivateBlock(then_bundle.block);
            RebindVisible(then_bundle.bindings, if_stmt.location);
            LowerScopedStatement(if_stmt.then_branch.get());
            if (current_block_active_) {
                SetJump(MakeTarget(join.block, ResolveArgumentsFor(outer_bindings, if_stmt.location)), if_stmt.location);
                has_fallthrough = true;
            }

            ActivateBlock(else_bundle.block);
            RebindVisible(else_bundle.bindings, if_stmt.location);
            LowerScopedStatement(if_stmt.else_branch.get());
            if (current_block_active_) {
                SetJump(MakeTarget(join.block, ResolveArgumentsFor(outer_bindings, if_stmt.location)), if_stmt.location);
                has_fallthrough = true;
            }
        } else {
            const std::vector<CfgValueId> branch_args = ResolveArgumentsFor(outer_bindings, if_stmt.location);
            SetBranch(condition,
                      MakeTarget(then_bundle.block, branch_args),
                      MakeTarget(join.block, branch_args),
                      if_stmt.location);
            has_fallthrough = true;

            ActivateBlock(then_bundle.block);
            RebindVisible(then_bundle.bindings, if_stmt.location);
            LowerScopedStatement(if_stmt.then_branch.get());
            if (current_block_active_) {
                SetJump(MakeTarget(join.block, ResolveArgumentsFor(outer_bindings, if_stmt.location)), if_stmt.location);
            }
        }

        if (!has_fallthrough) {
            current_block_active_ = false;
            return;
        }

        ActivateBlock(join.block);
        RebindVisible(join.bindings, if_stmt.location);
    }

    void LowerWhile(const IrWhileStmt& while_stmt) {
        const std::vector<VisibleBinding> outer_bindings = scopes_.SnapshotVisible();
        const BlockBindingBundle header = CreateVisibleParameterBlock("while_header", outer_bindings);
        const BlockBindingBundle body_bundle = CreateVisibleParameterBlock("while_body", outer_bindings);
        const BlockBindingBundle exit = CreateVisibleParameterBlock("while_exit", outer_bindings);

        SetJump(MakeTarget(header.block, ResolveArgumentsFor(outer_bindings, while_stmt.location)), while_stmt.location);

        ActivateBlock(header.block);
        RebindVisible(header.bindings, while_stmt.location);
        const CfgValueId condition = LowerExpression(while_stmt.condition.get());
        SetBranch(condition,
                  MakeTarget(body_bundle.block, ResolveArgumentsFor(outer_bindings, while_stmt.location)),
                  MakeTarget(exit.block, ResolveArgumentsFor(outer_bindings, while_stmt.location)),
                  while_stmt.location);

        ActivateBlock(body_bundle.block);
        RebindVisible(body_bundle.bindings, while_stmt.location);
        LowerScopedStatement(while_stmt.body.get());
        if (current_block_active_) {
            SetJump(MakeTarget(header.block, ResolveArgumentsFor(outer_bindings, while_stmt.location)), while_stmt.location);
        }

        ActivateBlock(exit.block);
        RebindVisible(exit.bindings, while_stmt.location);
    }

    void LowerFor(const IrForStmt& for_stmt) {
        const CfgValueId iterable = LowerExpression(for_stmt.iterable.get());
        const std::vector<VisibleBinding> outer_bindings = scopes_.SnapshotVisible();

        std::vector<std::pair<std::string, TypeInfo>> header_specs;
        header_specs.reserve(outer_bindings.size() + 2);
        for (const auto& binding : outer_bindings) {
            header_specs.emplace_back(binding.name, binding.type);
        }
        header_specs.emplace_back("__for_iterable", for_stmt.iterable->type);
        header_specs.emplace_back("__for_counter", TypeInfo{TypeKind::Int, ""});

        const BlockBindingBundle header = CreateParameterBlock("for_header", header_specs);
        const BlockBindingBundle body_bundle = CreateParameterBlock("for_body", header_specs);
        const BlockBindingBundle exit = CreateVisibleParameterBlock("for_exit", outer_bindings);

        const CfgValueId zero = EmitLiteral(for_stmt.location, {TypeKind::Int, ""}, static_cast<long long>(0));
        std::vector<CfgValueId> header_args = ResolveArgumentsFor(outer_bindings, for_stmt.location);
        header_args.push_back(iterable);
        header_args.push_back(zero);
        SetJump(MakeTarget(header.block, std::move(header_args)), for_stmt.location);

        ActivateBlock(header.block);
        for (std::size_t i = 0; i < outer_bindings.size(); ++i) {
            scopes_.Assign(outer_bindings[i].name, header.bindings[i].value, for_stmt.location);
        }
        const CfgValueId iterable_value = header.bindings[outer_bindings.size()].value;
        const CfgValueId counter = header.bindings.back().value;
        const CfgValueId length = EmitBuiltinCall(for_stmt.location,
                                                  {TypeKind::Int, ""},
                                                  IrBuiltinKind::Len,
                                                  "len",
                                                  {iterable_value});
        const CfgValueId condition =
            EmitBinary(for_stmt.location, {TypeKind::Bool, ""}, counter, TokenType::Less, length);
        std::vector<CfgValueId> body_args = ResolveArgumentsFor(outer_bindings, for_stmt.location);
        body_args.push_back(iterable_value);
        body_args.push_back(counter);
        SetBranch(condition,
                  MakeTarget(body_bundle.block, std::move(body_args)),
                  MakeTarget(exit.block, ResolveArgumentsFor(outer_bindings, for_stmt.location)),
                  for_stmt.location);

        ActivateBlock(body_bundle.block);
        for (std::size_t i = 0; i < outer_bindings.size(); ++i) {
            scopes_.Assign(outer_bindings[i].name, body_bundle.bindings[i].value, for_stmt.location);
        }
        const CfgValueId body_iterable = body_bundle.bindings[outer_bindings.size()].value;
        const CfgValueId body_counter = body_bundle.bindings.back().value;
        scopes_.Push();
        scopes_.Declare(kForIterableBindingName, for_stmt.iterable->type, body_iterable);
        scopes_.Declare(kForCounterBindingName, {TypeKind::Int, ""}, body_counter);
        if (for_stmt.index_name.has_value()) {
            const CfgValueId index_binding = EmitCopy(for_stmt.location, {TypeKind::Int, ""}, body_counter);
            scopes_.Declare(*for_stmt.index_name, {TypeKind::Int, ""}, index_binding);
        }

        const std::string access_mode = for_stmt.iterable->type == TypeInfo{TypeKind::String, ""} ? "String" : "Slice";
        const CfgValueId element = EmitIndex(for_stmt.location, for_stmt.element_type, access_mode, body_iterable, body_counter);
        const CfgValueId element_binding = EmitCopy(for_stmt.location, for_stmt.element_type, element);
        EmitDrop(for_stmt.location, element);
        scopes_.Declare(for_stmt.variable_name, for_stmt.element_type, element_binding);
        LowerStatement(for_stmt.body.get());
        std::optional<CfgValueId> current_iterable;
        std::optional<CfgValueId> current_counter;
        if (current_block_active_) {
            current_iterable = scopes_.Lookup(kForIterableBindingName, for_stmt.location).value;
            current_counter = scopes_.Lookup(kForCounterBindingName, for_stmt.location).value;
        }
        const ScopeFrame dropped = scopes_.PopFrame();
        if (current_block_active_) {
            for (const auto& [_, binding] : dropped) {
                if (binding.name == kForIterableBindingName || binding.name == kForCounterBindingName) {
                    continue;
                }
                EmitDrop(for_stmt.location, binding.value);
            }
        }

        if (current_block_active_) {
            const CfgValueId one = EmitLiteral(for_stmt.location, {TypeKind::Int, ""}, static_cast<long long>(1));
            const CfgValueId next_counter =
                EmitBinary(for_stmt.location, {TypeKind::Int, ""}, *current_counter, TokenType::Plus, one);
            std::vector<CfgValueId> next_args = ResolveArgumentsFor(outer_bindings, for_stmt.location);
            next_args.push_back(*current_iterable);
            next_args.push_back(next_counter);
            SetJump(MakeTarget(header.block, std::move(next_args)), for_stmt.location);
        }

        ActivateBlock(exit.block);
        RebindVisible(exit.bindings, for_stmt.location);
    }

    CfgValueId EmitUnary(const SourceLocation& location,
                         const TypeInfo& type,
                         TokenType token,
                         CfgValueId right) {
        CfgInstruction instruction;
        instruction.location = location;
        instruction.type = type;
        instruction.kind = CfgInstructionKind::Unary;
        instruction.token = token;
        instruction.inputs.push_back(right);
        return EmitInstruction(std::move(instruction));
    }

    CfgValueId EmitBinary(const SourceLocation& location,
                          const TypeInfo& type,
                          CfgValueId left,
                          TokenType token,
                          CfgValueId right) {
        CfgInstruction instruction;
        instruction.location = location;
        instruction.type = type;
        instruction.kind = CfgInstructionKind::Binary;
        instruction.token = token;
        instruction.inputs = {left, right};
        return EmitInstruction(std::move(instruction));
    }

    CfgValueId EmitBuiltinCall(const SourceLocation& location,
                               const TypeInfo& type,
                               IrBuiltinKind builtin_kind,
                               std::string callee_name,
                               std::vector<CfgValueId> arguments) {
        CfgInstruction instruction;
        instruction.location = location;
        instruction.type = type;
        instruction.kind = CfgInstructionKind::Call;
        instruction.name = std::move(callee_name);
        instruction.call_kind = IrCallKind::Builtin;
        instruction.builtin_kind = builtin_kind;
        instruction.inputs = std::move(arguments);
        return EmitInstruction(std::move(instruction));
    }

    CfgValueId EmitIndex(const SourceLocation& location,
                         const TypeInfo& type,
                         std::string mode,
                         CfgValueId object,
                         CfgValueId index) {
        CfgInstruction instruction;
        instruction.location = location;
        instruction.type = type;
        instruction.kind = CfgInstructionKind::GetIndex;
        instruction.name = std::move(mode);
        instruction.inputs = {object, index};
        return EmitInstruction(std::move(instruction));
    }

    CfgValueId LowerLogicalBinary(const IrBinaryExpr& binary) {
        const CfgValueId left = LowerExpression(binary.left.get());
        const std::vector<VisibleBinding> outer_bindings = scopes_.SnapshotVisible();

        std::vector<std::pair<std::string, TypeInfo>> join_specs;
        join_specs.reserve(outer_bindings.size() + 1);
        join_specs.emplace_back("__logical_result", TypeInfo{TypeKind::Bool, ""});
        for (const auto& binding : outer_bindings) {
            join_specs.emplace_back(binding.name, binding.type);
        }

        const BlockBindingBundle join =
            CreateParameterBlock(binary.op == TokenType::AndAnd ? "and_join" : "or_join", join_specs);
        const BlockBindingBundle right_bundle =
            CreateVisibleParameterBlock(binary.op == TokenType::AndAnd ? "and_rhs" : "or_rhs", outer_bindings);

        const CfgValueId constant = EmitLiteral(binary.location,
                                                {TypeKind::Bool, ""},
                                                binary.op == TokenType::AndAnd ? Value{false} : Value{true});
        const std::vector<CfgValueId> outer_args = ResolveArgumentsFor(outer_bindings, binary.location);
        std::vector<CfgValueId> short_circuit_args;
        short_circuit_args.reserve(outer_args.size() + 1);
        short_circuit_args.push_back(constant);
        short_circuit_args.insert(short_circuit_args.end(), outer_args.begin(), outer_args.end());

        if (binary.op == TokenType::AndAnd) {
            SetBranch(left,
                      MakeTarget(right_bundle.block, outer_args),
                      MakeTarget(join.block, short_circuit_args),
                      binary.location);
        } else {
            SetBranch(left,
                      MakeTarget(join.block, short_circuit_args),
                      MakeTarget(right_bundle.block, outer_args),
                      binary.location);
        }

        ActivateBlock(right_bundle.block);
        RebindVisible(right_bundle.bindings, binary.location);
        const CfgValueId right = LowerExpression(binary.right.get());
        std::vector<CfgValueId> taken_args;
        taken_args.reserve(outer_bindings.size() + 1);
        taken_args.push_back(right);
        const std::vector<CfgValueId> rebound_outer_args = ResolveArgumentsFor(outer_bindings, binary.location);
        taken_args.insert(taken_args.end(), rebound_outer_args.begin(), rebound_outer_args.end());
        SetJump(MakeTarget(join.block, std::move(taken_args)), binary.location);

        ActivateBlock(join.block);
        for (std::size_t i = 0; i < outer_bindings.size(); ++i) {
            scopes_.Assign(outer_bindings[i].name, join.bindings[i + 1].value, binary.location);
        }
        return join.bindings.front().value;
    }

    CfgValueId LowerExpression(const IrExpr* expr) {
        if (const auto* literal = dynamic_cast<const IrLiteralExpr*>(expr)) {
            return EmitLiteral(literal->location, literal->type, literal->value);
        }

        if (const auto* array_literal = dynamic_cast<const IrArrayLiteralExpr*>(expr)) {
            CfgInstruction instruction;
            instruction.location = array_literal->location;
            instruction.type = array_literal->type;
            instruction.kind = CfgInstructionKind::ArrayLiteral;
            instruction.name = array_literal->type.name;
            for (const auto& element : array_literal->elements) {
                instruction.inputs.push_back(LowerExpression(element.get()));
            }
            return EmitInstruction(std::move(instruction));
        }

        if (const auto* variable = dynamic_cast<const IrVariableExpr*>(expr)) {
            return scopes_.Lookup(variable->name, variable->location).value;
        }

        if (const auto* assign = dynamic_cast<const IrAssignExpr*>(expr)) {
            const CfgValueId value = LowerExpression(assign->value.get());

            if (const auto* variable = dynamic_cast<const IrVariableExpr*>(assign->target.get())) {
                const VisibleBinding current_binding = scopes_.Lookup(variable->name, assign->location);
                const CfgValueId assigned_value = EmitCopy(assign->location, assign->type, value);
                if (ExpressionProducesOwnedValue(assign->value.get())) {
                    EmitDrop(assign->location, value);
                }
                scopes_.Assign(variable->name, assigned_value, assign->location);
                EmitDrop(assign->location, current_binding.value);
                return assigned_value;
            }

            if (const auto* member = dynamic_cast<const IrMemberExpr*>(assign->target.get())) {
                const CfgValueId object = LowerExpression(member->object.get());
                CfgInstruction instruction;
                instruction.location = assign->location;
                instruction.type = assign->type;
                instruction.kind = CfgInstructionKind::AssignField;
                instruction.name = member->member_name;
                instruction.inputs = {object, value};
                return EmitInstruction(std::move(instruction));
            }

            if (const auto* index = dynamic_cast<const IrIndexExpr*>(assign->target.get())) {
                const CfgValueId object = LowerExpression(index->object.get());
                const CfgValueId index_value = LowerExpression(index->index.get());
                CfgInstruction instruction;
                instruction.location = assign->location;
                instruction.type = assign->type;
                instruction.kind = CfgInstructionKind::AssignIndex;
                instruction.inputs = {object, index_value, value};
                return EmitInstruction(std::move(instruction));
            }

            throw BuildLocationError(assign->location, "CFG lowering found an invalid assignment target");
        }

        if (const auto* unary = dynamic_cast<const IrUnaryExpr*>(expr)) {
            return EmitUnary(unary->location, unary->type, unary->op, LowerExpression(unary->right.get()));
        }

        if (const auto* binary = dynamic_cast<const IrBinaryExpr*>(expr)) {
            if (binary->op == TokenType::AndAnd || binary->op == TokenType::OrOr) {
                return LowerLogicalBinary(*binary);
            }
            const CfgValueId left = LowerExpression(binary->left.get());
            const CfgValueId right = LowerExpression(binary->right.get());
            return EmitBinary(binary->location, binary->type, left, binary->op, right);
        }

        if (const auto* range = dynamic_cast<const IrRangeExpr*>(expr)) {
            CfgInstruction instruction;
            instruction.location = range->location;
            instruction.type = range->type;
            instruction.kind = CfgInstructionKind::Range;
            instruction.inputs = {LowerExpression(range->start.get()), LowerExpression(range->end.get())};
            return EmitInstruction(std::move(instruction));
        }

        if (const auto* call = dynamic_cast<const IrCallExpr*>(expr)) {
            CfgInstruction instruction;
            instruction.location = call->location;
            instruction.type = call->type;
            instruction.kind = CfgInstructionKind::Call;
            instruction.name = call->callee_name;
            instruction.call_kind = call->call_kind;
            instruction.builtin_kind = call->builtin_kind;
            for (const auto& argument : call->arguments) {
                instruction.inputs.push_back(LowerExpression(argument.get()));
            }
            return EmitInstruction(std::move(instruction));
        }

        if (const auto* struct_literal = dynamic_cast<const IrStructLiteralExpr*>(expr)) {
            CfgInstruction instruction;
            instruction.location = struct_literal->location;
            instruction.type = struct_literal->type;
            instruction.kind = CfgInstructionKind::StructLiteral;
            instruction.name = struct_literal->type_name;
            for (const auto& field : struct_literal->fields) {
                instruction.fields.push_back(CfgStructFieldValue{field.name, LowerExpression(field.value.get())});
            }
            return EmitInstruction(std::move(instruction));
        }

        if (const auto* member = dynamic_cast<const IrMemberExpr*>(expr)) {
            CfgInstruction instruction;
            instruction.location = member->location;
            instruction.type = member->type;
            instruction.kind = CfgInstructionKind::GetField;
            instruction.name = member->member_name;
            instruction.inputs = {LowerExpression(member->object.get())};
            return EmitInstruction(std::move(instruction));
        }

        if (const auto* index = dynamic_cast<const IrIndexExpr*>(expr)) {
            const std::string mode = index->object->type == TypeInfo{TypeKind::String, ""} ? "String" : "Slice";
            return EmitIndex(index->location,
                             index->type,
                             mode,
                             LowerExpression(index->object.get()),
                             LowerExpression(index->index.get()));
        }

        if (const auto* slice = dynamic_cast<const IrSliceExpr*>(expr)) {
            CfgInstruction instruction;
            instruction.location = slice->location;
            instruction.type = slice->type;
            instruction.kind = CfgInstructionKind::GetSlice;
            instruction.name = slice->object->type == TypeInfo{TypeKind::String, ""} ? "String" : "Slice";
            instruction.inputs.push_back(LowerExpression(slice->object.get()));
            if (slice->start) {
                instruction.has_start = true;
                instruction.inputs.push_back(LowerExpression(slice->start.get()));
            }
            if (slice->end) {
                instruction.has_end = true;
                instruction.inputs.push_back(LowerExpression(slice->end.get()));
            }
            return EmitInstruction(std::move(instruction));
        }

        throw BuildLocationError(expr->location, "CFG lowering failed for an unknown expression kind");
    }
};

class CfgProgramBuilder {
  public:
    explicit CfgProgramBuilder(const IrProgram& program) : program_(program) {}

    CfgProgram Lower() const {
        CfgProgram cfg_program;
        cfg_program.module_name = program_.module_name;
        cfg_program.imports = program_.imports;
        cfg_program.structs = program_.structs;

        for (const auto& function : program_.functions) {
            cfg_program.functions.push_back(CfgFunctionBuilder(function).Lower());
        }

        return cfg_program;
    }

  private:
    const IrProgram& program_;
};

}  // namespace

CfgProgram LowerIrToCfg(const IrProgram& program) {
    return CfgProgramBuilder(program).Lower();
}

std::string FormatCfgProgram(const CfgProgram& program) {
    std::ostringstream buffer;

    if (program.module_name.has_value()) {
        buffer << "module " << *program.module_name << ";\n\n";
    }

    for (const auto& import_decl : program.imports) {
        buffer << "import \"" << import_decl.path << "\";\n";
    }
    if (!program.imports.empty()) {
        buffer << '\n';
    }

    for (const auto& struct_decl : program.structs) {
        buffer << "struct " << struct_decl.full_name << " {\n";
        for (const auto& field : struct_decl.fields) {
            buffer << "  " << field.name << ": " << TypeInfoName(field.type) << ";\n";
        }
        buffer << "}\n\n";
    }

    for (const auto& function : program.functions) {
        buffer << "cfg fn " << function.full_name << "(";
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i > 0) {
                buffer << ", ";
            }
            buffer << function.parameters[i].name << ": " << TypeInfoName(function.parameters[i].type) << " "
                   << ValueName(function.parameters[i].value);
        }
        buffer << ") -> " << TypeInfoName(function.return_type) << " {\n";

        for (const auto& block : function.blocks) {
            buffer << Indent(1) << BlockName(block.id);
            if (!block.parameters.empty()) {
                buffer << "(";
                for (std::size_t i = 0; i < block.parameters.size(); ++i) {
                    if (i > 0) {
                        buffer << ", ";
                    }
                    buffer << block.parameters[i].name << ": " << TypeInfoName(block.parameters[i].type) << " "
                           << ValueName(block.parameters[i].value);
                }
                buffer << ")";
            }
            buffer << ":\n";

            for (const auto& instruction : block.instructions) {
                buffer << Indent(2) << FormatInstruction(instruction) << "\n";
            }

            buffer << Indent(2);
            switch (block.terminator.kind) {
            case CfgTerminatorKind::None:
                buffer << "unreachable";
                break;
            case CfgTerminatorKind::Jump:
                buffer << "jump " << FormatTarget(block.terminator.target);
                break;
            case CfgTerminatorKind::Branch:
                buffer << "branch " << ValueName(*block.terminator.condition) << " ? "
                       << FormatTarget(block.terminator.true_target) << " : "
                       << FormatTarget(block.terminator.false_target);
                break;
            case CfgTerminatorKind::Return:
                buffer << "return";
                if (block.terminator.return_value.has_value()) {
                    buffer << " " << ValueName(*block.terminator.return_value);
                }
                break;
            }
            buffer << "\n";
        }

        buffer << "}\n\n";
    }

    return buffer.str();
}
