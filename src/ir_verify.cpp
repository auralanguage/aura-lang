#include "ir_verify.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {

struct FunctionInfo {
    std::vector<TypeInfo> parameter_types;
    TypeInfo return_type{TypeKind::Unit, ""};
    bool is_builtin = false;
};

struct StructInfo {
    std::unordered_map<std::string, TypeInfo> fields;
};

bool IsStringLikeType(const TypeInfo& type) {
    return type == TypeInfo{TypeKind::String, ""} || type == TypeInfo{TypeKind::Char, ""};
}

bool IsStringCollectionType(const TypeInfo& type) {
    return type == TypeInfo{TypeKind::Slice, "String"} || type == TypeInfo{TypeKind::Slice, "Char"};
}

std::string FormatValueId(CfgValueId value) {
    return "%" + std::to_string(value);
}

class StructTable {
  public:
    void Add(const SourceLocation& location, const std::string& name, const std::vector<IrStructFieldDecl>& fields) {
        if (structs_.find(name) != structs_.end()) {
            throw BuildLocationError(location, "IR verifier found duplicate struct `" + name + "`");
        }

        StructInfo info;
        for (const auto& field : fields) {
            if (info.fields.find(field.name) != info.fields.end()) {
                throw BuildLocationError(location,
                                         "IR verifier found duplicate field `" + field.name + "` in struct `" + name +
                                             "`");
            }
            info.fields[field.name] = field.type;
        }
        structs_[name] = std::move(info);
    }

    bool Contains(const std::string& name) const {
        return structs_.find(name) != structs_.end();
    }

    const StructInfo& Get(const std::string& name, const SourceLocation& location) const {
        const auto it = structs_.find(name);
        if (it == structs_.end()) {
            throw BuildLocationError(location, "IR verifier could not resolve struct `" + name + "`");
        }
        return it->second;
    }

  private:
    std::unordered_map<std::string, StructInfo> structs_;
};

class FunctionTable {
  public:
    FunctionTable() {
        AddBuiltin("print", {}, {TypeKind::Unit, ""});
        AddBuiltin("len", {}, {TypeKind::Int, ""});
        AddBuiltin("push", {}, {TypeKind::Unit, ""});
        AddBuiltin("pop", {}, {TypeKind::Unit, ""});
        AddBuiltin("insert", {}, {TypeKind::Unit, ""});
        AddBuiltin("remove_at", {}, {TypeKind::Unit, ""});
        AddBuiltin("clear", {}, {TypeKind::Unit, ""});
        AddBuiltin("contains", {}, {TypeKind::Bool, ""});
        AddBuiltin("starts_with", {}, {TypeKind::Bool, ""});
        AddBuiltin("ends_with", {}, {TypeKind::Bool, ""});
        AddBuiltin("join", {}, {TypeKind::String, ""});
        AddBuiltin("file_exists", {}, {TypeKind::Bool, ""});
        AddBuiltin("read_text", {}, {TypeKind::String, ""});
        AddBuiltin("write_text", {}, {TypeKind::Unit, ""});
        AddBuiltin("append_text", {}, {TypeKind::Unit, ""});
        AddBuiltin("remove_file", {}, {TypeKind::Bool, ""});
        AddBuiltin("create_dir", {}, {TypeKind::Bool, ""});
        AddBuiltin("list_dir", {}, {TypeKind::Slice, "String"});
        AddBuiltin("abs", {}, {TypeKind::Int, ""});
        AddBuiltin("min", {}, {TypeKind::Int, ""});
        AddBuiltin("max", {}, {TypeKind::Int, ""});
        AddBuiltin("pow", {}, {TypeKind::Int, ""});
    }

    void AddUser(const SourceLocation& location,
                 const std::string& name,
                 const std::vector<TypeInfo>& parameter_types,
                 const TypeInfo& return_type) {
        if (functions_.find(name) != functions_.end()) {
            throw BuildLocationError(location, "IR verifier found duplicate function `" + name + "`");
        }
        functions_[name] = FunctionInfo{parameter_types, return_type, false};
    }

    const FunctionInfo& Get(const std::string& name, const SourceLocation& location) const {
        const auto it = functions_.find(name);
        if (it == functions_.end()) {
            throw BuildLocationError(location, "IR verifier could not resolve function `" + name + "`");
        }
        return it->second;
    }

  private:
    void AddBuiltin(std::string name, std::vector<TypeInfo> parameter_types, TypeInfo return_type) {
        functions_.emplace(std::move(name), FunctionInfo{std::move(parameter_types), std::move(return_type), true});
    }

    std::unordered_map<std::string, FunctionInfo> functions_;
};

void VerifyTypeExists(const TypeInfo& type, const StructTable& structs, const SourceLocation& location) {
    if (type.kind == TypeKind::Slice) {
        VerifyTypeExists(TypeInfoFromAnnotation(type.name), structs, location);
        return;
    }
    if (type.kind == TypeKind::Named && !structs.Contains(type.name)) {
        throw BuildLocationError(location, "IR verifier found unknown named type `" + type.name + "`");
    }
}

void VerifyBuiltinCall(const SourceLocation& location,
                       const TypeInfo& result_type,
                       IrBuiltinKind builtin_kind,
                       const std::string& callee_name,
                       const std::vector<TypeInfo>& argument_types) {
    switch (builtin_kind) {
    case IrBuiltinKind::Print:
        if (result_type != TypeInfo{TypeKind::Unit, ""}) {
            throw BuildLocationError(location, "Builtin `print` must return `Unit`");
        }
        return;
    case IrBuiltinKind::Len:
        if (argument_types.size() != 1 || !(argument_types[0].kind == TypeKind::Slice || argument_types[0] == TypeInfo{TypeKind::String, ""}) ||
            result_type != TypeInfo{TypeKind::Int, ""}) {
            throw BuildLocationError(location, "Builtin `len` verifier check failed");
        }
        return;
    case IrBuiltinKind::Push:
        if (argument_types.size() != 2 || argument_types[0].kind != TypeKind::Slice ||
            TypeInfoFromAnnotation(argument_types[0].name) != argument_types[1] ||
            result_type != TypeInfo{TypeKind::Unit, ""}) {
            throw BuildLocationError(location, "Builtin `push` verifier check failed");
        }
        return;
    case IrBuiltinKind::Pop:
        if (argument_types.size() != 1 || argument_types[0].kind != TypeKind::Slice ||
            result_type != TypeInfoFromAnnotation(argument_types[0].name)) {
            throw BuildLocationError(location, "Builtin `pop` verifier check failed");
        }
        return;
    case IrBuiltinKind::Insert:
        if (argument_types.size() != 3 || argument_types[0].kind != TypeKind::Slice ||
            argument_types[1] != TypeInfo{TypeKind::Int, ""} ||
            TypeInfoFromAnnotation(argument_types[0].name) != argument_types[2] ||
            result_type != TypeInfo{TypeKind::Unit, ""}) {
            throw BuildLocationError(location, "Builtin `insert` verifier check failed");
        }
        return;
    case IrBuiltinKind::RemoveAt:
        if (argument_types.size() != 2 || argument_types[0].kind != TypeKind::Slice ||
            argument_types[1] != TypeInfo{TypeKind::Int, ""} ||
            result_type != TypeInfoFromAnnotation(argument_types[0].name)) {
            throw BuildLocationError(location, "Builtin `remove_at` verifier check failed");
        }
        return;
    case IrBuiltinKind::Clear:
        if (argument_types.size() != 1 || argument_types[0].kind != TypeKind::Slice ||
            result_type != TypeInfo{TypeKind::Unit, ""}) {
            throw BuildLocationError(location, "Builtin `clear` verifier check failed");
        }
        return;
    case IrBuiltinKind::Contains:
        if (argument_types.size() != 2 || result_type != TypeInfo{TypeKind::Bool, ""}) {
            throw BuildLocationError(location, "Builtin `contains` verifier check failed");
        }
        if (argument_types[0] == TypeInfo{TypeKind::String, ""}) {
            if (!IsStringLikeType(argument_types[1])) {
                throw BuildLocationError(location, "Builtin `contains` verifier check failed");
            }
            return;
        }
        if (argument_types[0] == TypeInfo{TypeKind::Slice, "String"} && argument_types[1] == TypeInfo{TypeKind::String, ""}) {
            return;
        }
        if (argument_types[0] == TypeInfo{TypeKind::Slice, "Char"} && argument_types[1] == TypeInfo{TypeKind::Char, ""}) {
            return;
        }
        throw BuildLocationError(location, "Builtin `contains` verifier check failed");
    case IrBuiltinKind::StartsWith:
    case IrBuiltinKind::EndsWith:
        if (argument_types.size() != 2 || argument_types[0] != TypeInfo{TypeKind::String, ""} ||
            !IsStringLikeType(argument_types[1]) || result_type != TypeInfo{TypeKind::Bool, ""}) {
            throw BuildLocationError(location, "Builtin `" + callee_name + "` verifier check failed");
        }
        return;
    case IrBuiltinKind::Join:
        if (argument_types.size() != 2 || !IsStringCollectionType(argument_types[0]) ||
            argument_types[1] != TypeInfo{TypeKind::String, ""} || result_type != TypeInfo{TypeKind::String, ""}) {
            throw BuildLocationError(location, "Builtin `join` verifier check failed");
        }
        return;
    case IrBuiltinKind::FileExists:
        if (argument_types.size() != 1 || argument_types[0] != TypeInfo{TypeKind::String, ""} ||
            result_type != TypeInfo{TypeKind::Bool, ""}) {
            throw BuildLocationError(location, "Builtin `file_exists` verifier check failed");
        }
        return;
    case IrBuiltinKind::ReadText:
        if (argument_types.size() != 1 || argument_types[0] != TypeInfo{TypeKind::String, ""} ||
            result_type != TypeInfo{TypeKind::String, ""}) {
            throw BuildLocationError(location, "Builtin `read_text` verifier check failed");
        }
        return;
    case IrBuiltinKind::WriteText:
    case IrBuiltinKind::AppendText:
        if (argument_types.size() != 2 || argument_types[0] != TypeInfo{TypeKind::String, ""} ||
            argument_types[1] != TypeInfo{TypeKind::String, ""} ||
            result_type != TypeInfo{TypeKind::Unit, ""}) {
            throw BuildLocationError(location, "Builtin `" + callee_name + "` verifier check failed");
        }
        return;
    case IrBuiltinKind::RemoveFile:
    case IrBuiltinKind::CreateDir:
        if (argument_types.size() != 1 || argument_types[0] != TypeInfo{TypeKind::String, ""} ||
            result_type != TypeInfo{TypeKind::Bool, ""}) {
            throw BuildLocationError(location, "Builtin `" + callee_name + "` verifier check failed");
        }
        return;
    case IrBuiltinKind::ListDir:
        if (argument_types.size() != 1 || argument_types[0] != TypeInfo{TypeKind::String, ""} ||
            result_type != TypeInfo{TypeKind::Slice, "String"}) {
            throw BuildLocationError(location, "Builtin `list_dir` verifier check failed");
        }
        return;
    case IrBuiltinKind::Abs:
        if (argument_types.size() != 1 || argument_types[0] != TypeInfo{TypeKind::Int, ""} ||
            result_type != TypeInfo{TypeKind::Int, ""}) {
            throw BuildLocationError(location, "Builtin `abs` verifier check failed");
        }
        return;
    case IrBuiltinKind::Min:
        if (argument_types.size() != 2 || argument_types[0] != TypeInfo{TypeKind::Int, ""} ||
            argument_types[1] != TypeInfo{TypeKind::Int, ""} || result_type != TypeInfo{TypeKind::Int, ""}) {
            throw BuildLocationError(location, "Builtin `min` verifier check failed");
        }
        return;
    case IrBuiltinKind::Max:
        if (argument_types.size() != 2 || argument_types[0] != TypeInfo{TypeKind::Int, ""} ||
            argument_types[1] != TypeInfo{TypeKind::Int, ""} || result_type != TypeInfo{TypeKind::Int, ""}) {
            throw BuildLocationError(location, "Builtin `max` verifier check failed");
        }
        return;
    case IrBuiltinKind::Pow:
        if (argument_types.size() != 2 || argument_types[0] != TypeInfo{TypeKind::Int, ""} ||
            argument_types[1] != TypeInfo{TypeKind::Int, ""} || result_type != TypeInfo{TypeKind::Int, ""}) {
            throw BuildLocationError(location, "Builtin `pow` verifier check failed");
        }
        return;
    case IrBuiltinKind::None:
        throw BuildLocationError(location, "Builtin call is missing a builtin kind");
    }
}

class IrVerifierScope {
  public:
    void Push() {
        scopes_.emplace_back();
    }

    void Pop() {
        scopes_.pop_back();
    }

    void Declare(const std::string& name, const TypeInfo& type, const SourceLocation& location) {
        if (scopes_.empty()) {
            Push();
        }
        auto& scope = scopes_.back();
        if (scope.find(name) != scope.end()) {
            throw BuildLocationError(location, "IR verifier found duplicate local `" + name + "`");
        }
        scope[name] = type;
    }

    TypeInfo Lookup(const std::string& name, const SourceLocation& location) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            const auto found = it->find(name);
            if (found != it->end()) {
                return found->second;
            }
        }
        throw BuildLocationError(location, "IR verifier could not resolve local `" + name + "`");
    }

  private:
    std::vector<std::unordered_map<std::string, TypeInfo>> scopes_;
};

class IrProgramVerifier {
  public:
    explicit IrProgramVerifier(const IrProgram& program) : program_(program) {}

    void Verify() {
        for (const auto& struct_decl : program_.structs) {
            structs_.Add(struct_decl.location, struct_decl.full_name, struct_decl.fields);
        }
        for (const auto& struct_decl : program_.structs) {
            for (const auto& field : struct_decl.fields) {
                VerifyTypeExists(field.type, structs_, struct_decl.location);
            }
        }
        for (const auto& function : program_.functions) {
            std::vector<TypeInfo> parameter_types;
            parameter_types.reserve(function.parameters.size());
            for (const auto& parameter : function.parameters) {
                VerifyTypeExists(parameter.type, structs_, function.location);
                parameter_types.push_back(parameter.type);
            }
            VerifyTypeExists(function.return_type, structs_, function.location);
            functions_.AddUser(function.location, function.full_name, parameter_types, function.return_type);
        }
        for (const auto& function : program_.functions) {
            VerifyFunction(function);
        }
    }

  private:
    const IrProgram& program_;
    StructTable structs_;
    FunctionTable functions_;

    void VerifyFunction(const IrFunctionDecl& function) {
        IrVerifierScope scope;
        scope.Push();
        std::unordered_set<std::string> parameter_names;
        for (const auto& parameter : function.parameters) {
            if (!parameter_names.insert(parameter.name).second) {
                throw BuildLocationError(function.location,
                                         "IR verifier found duplicate parameter `" + parameter.name + "` in `" +
                                             function.full_name + "`");
            }
            scope.Declare(parameter.name, parameter.type, function.location);
        }

        for (const auto& statement : function.body) {
            VerifyStatement(statement.get(), scope, function.return_type);
        }
    }

    void VerifyStatement(const IrStmt* stmt, IrVerifierScope& scope, const TypeInfo& return_type) {
        if (const auto* block = dynamic_cast<const IrBlockStmt*>(stmt)) {
            scope.Push();
            for (const auto& statement : block->statements) {
                VerifyStatement(statement.get(), scope, return_type);
            }
            scope.Pop();
            return;
        }

        if (const auto* let_stmt = dynamic_cast<const IrLetStmt*>(stmt)) {
            VerifyExpression(let_stmt->initializer.get(), scope);
            if (let_stmt->initializer->type != let_stmt->variable_type) {
                throw BuildLocationError(let_stmt->location,
                                         "IR verifier found mismatched let initializer type for `" + let_stmt->name + "`");
            }
            VerifyTypeExists(let_stmt->variable_type, structs_, let_stmt->location);
            scope.Declare(let_stmt->name, let_stmt->variable_type, let_stmt->location);
            return;
        }

        if (const auto* return_stmt = dynamic_cast<const IrReturnStmt*>(stmt)) {
            if (return_stmt->value) {
                VerifyExpression(return_stmt->value.get(), scope);
                if (return_stmt->value->type != return_type) {
                    throw BuildLocationError(return_stmt->location, "IR verifier found a return type mismatch");
                }
            } else if (return_type != TypeInfo{TypeKind::Unit, ""}) {
                throw BuildLocationError(return_stmt->location, "IR verifier found a missing return value");
            }
            return;
        }

        if (const auto* if_stmt = dynamic_cast<const IrIfStmt*>(stmt)) {
            VerifyExpression(if_stmt->condition.get(), scope);
            if (if_stmt->condition->type != TypeInfo{TypeKind::Bool, ""}) {
                throw BuildLocationError(if_stmt->location, "IR verifier requires `if` conditions to be `Bool`");
            }
            scope.Push();
            VerifyStatement(if_stmt->then_branch.get(), scope, return_type);
            scope.Pop();
            if (if_stmt->else_branch) {
                scope.Push();
                VerifyStatement(if_stmt->else_branch.get(), scope, return_type);
                scope.Pop();
            }
            return;
        }

        if (const auto* while_stmt = dynamic_cast<const IrWhileStmt*>(stmt)) {
            VerifyExpression(while_stmt->condition.get(), scope);
            if (while_stmt->condition->type != TypeInfo{TypeKind::Bool, ""}) {
                throw BuildLocationError(while_stmt->location, "IR verifier requires `while` conditions to be `Bool`");
            }
            scope.Push();
            VerifyStatement(while_stmt->body.get(), scope, return_type);
            scope.Pop();
            return;
        }

        if (const auto* for_stmt = dynamic_cast<const IrForStmt*>(stmt)) {
            VerifyExpression(for_stmt->iterable.get(), scope);
            const bool is_string = for_stmt->iterable->type == TypeInfo{TypeKind::String, ""};
            const bool is_slice = for_stmt->iterable->type.kind == TypeKind::Slice;
            if (!is_string && !is_slice) {
                throw BuildLocationError(for_stmt->location, "IR verifier requires `for` iterables to be String or slice");
            }
            const TypeInfo expected_element = is_string ? TypeInfo{TypeKind::Char, ""} : TypeInfoFromAnnotation(for_stmt->iterable->type.name);
            if (expected_element != for_stmt->element_type) {
                throw BuildLocationError(for_stmt->location, "IR verifier found a mismatched `for` element type");
            }
            scope.Push();
            if (for_stmt->index_name.has_value()) {
                scope.Declare(*for_stmt->index_name, {TypeKind::Int, ""}, for_stmt->location);
            }
            scope.Declare(for_stmt->variable_name, for_stmt->element_type, for_stmt->location);
            VerifyStatement(for_stmt->body.get(), scope, return_type);
            scope.Pop();
            return;
        }

        if (const auto* expr_stmt = dynamic_cast<const IrExprStmt*>(stmt)) {
            VerifyExpression(expr_stmt->expression.get(), scope);
            return;
        }

        throw BuildLocationError(stmt->location, "IR verifier encountered an unknown statement kind");
    }

    void VerifyExpression(const IrExpr* expr, IrVerifierScope& scope) {
        VerifyTypeExists(expr->type, structs_, expr->location);

        if (const auto* literal = dynamic_cast<const IrLiteralExpr*>(expr)) {
            if (TypeInfoFromValue(literal->value) != literal->type) {
                throw BuildLocationError(literal->location, "IR verifier found a literal with the wrong type");
            }
            return;
        }

        if (const auto* array_literal = dynamic_cast<const IrArrayLiteralExpr*>(expr)) {
            if (array_literal->type.kind != TypeKind::Slice) {
                throw BuildLocationError(array_literal->location, "IR verifier found an invalid array literal");
            }
            const TypeInfo element_type = TypeInfoFromAnnotation(array_literal->type.name);
            for (const auto& element : array_literal->elements) {
                VerifyExpression(element.get(), scope);
                if (element->type != element_type) {
                    throw BuildLocationError(array_literal->location, "IR verifier found a mixed-type array literal");
                }
            }
            return;
        }

        if (const auto* variable = dynamic_cast<const IrVariableExpr*>(expr)) {
            if (scope.Lookup(variable->name, variable->location) != variable->type) {
                throw BuildLocationError(variable->location, "IR verifier found a variable type mismatch");
            }
            return;
        }

        if (const auto* assign = dynamic_cast<const IrAssignExpr*>(expr)) {
            VerifyExpression(assign->target.get(), scope);
            VerifyExpression(assign->value.get(), scope);
            if (assign->value->type != assign->type) {
                throw BuildLocationError(assign->location, "IR verifier found an assignment value type mismatch");
            }
            if (const auto* variable = dynamic_cast<const IrVariableExpr*>(assign->target.get())) {
                if (scope.Lookup(variable->name, assign->location) != assign->type) {
                    throw BuildLocationError(assign->location, "IR verifier found an invalid variable assignment target");
                }
                return;
            }
            if (const auto* member = dynamic_cast<const IrMemberExpr*>(assign->target.get())) {
                if (member->type != assign->type) {
                    throw BuildLocationError(assign->location, "IR verifier found an invalid field assignment target");
                }
                return;
            }
            if (const auto* index = dynamic_cast<const IrIndexExpr*>(assign->target.get())) {
                if (index->type != assign->type) {
                    throw BuildLocationError(assign->location, "IR verifier found an invalid index assignment target");
                }
                return;
            }
            throw BuildLocationError(assign->location, "IR verifier found an unsupported assignment target");
        }

        if (const auto* unary = dynamic_cast<const IrUnaryExpr*>(expr)) {
            VerifyExpression(unary->right.get(), scope);
            if (unary->op == TokenType::Bang &&
                (unary->right->type != TypeInfo{TypeKind::Bool, ""} || unary->type != TypeInfo{TypeKind::Bool, ""})) {
                throw BuildLocationError(unary->location, "IR verifier found an invalid `!` expression");
            }
            if (unary->op == TokenType::Minus &&
                (unary->right->type != TypeInfo{TypeKind::Int, ""} || unary->type != TypeInfo{TypeKind::Int, ""})) {
                throw BuildLocationError(unary->location, "IR verifier found an invalid unary `-` expression");
            }
            return;
        }

        if (const auto* binary = dynamic_cast<const IrBinaryExpr*>(expr)) {
            VerifyExpression(binary->left.get(), scope);
            VerifyExpression(binary->right.get(), scope);
            switch (binary->op) {
            case TokenType::AndAnd:
            case TokenType::OrOr:
                if (binary->left->type != TypeInfo{TypeKind::Bool, ""} ||
                    binary->right->type != TypeInfo{TypeKind::Bool, ""} ||
                    binary->type != TypeInfo{TypeKind::Bool, ""}) {
                    throw BuildLocationError(binary->location, "IR verifier found an invalid logical expression");
                }
                return;
            case TokenType::EqualEqual:
            case TokenType::BangEqual:
                if (binary->type != TypeInfo{TypeKind::Bool, ""} ||
                    (binary->left->type != binary->right->type &&
                     !(IsStringLikeType(binary->left->type) && IsStringLikeType(binary->right->type)))) {
                    throw BuildLocationError(binary->location, "IR verifier found an invalid equality expression");
                }
                return;
            case TokenType::Greater:
            case TokenType::GreaterEqual:
            case TokenType::Less:
            case TokenType::LessEqual:
                if (binary->left->type != TypeInfo{TypeKind::Int, ""} ||
                    binary->right->type != TypeInfo{TypeKind::Int, ""} ||
                    binary->type != TypeInfo{TypeKind::Bool, ""}) {
                    throw BuildLocationError(binary->location, "IR verifier found an invalid comparison expression");
                }
                return;
            case TokenType::Plus:
                if (binary->left->type == TypeInfo{TypeKind::Int, ""} &&
                    binary->right->type == TypeInfo{TypeKind::Int, ""} &&
                    binary->type == TypeInfo{TypeKind::Int, ""}) {
                    return;
                }
                if (IsStringLikeType(binary->left->type) && IsStringLikeType(binary->right->type) &&
                    !(binary->left->type == TypeInfo{TypeKind::Char, ""} &&
                      binary->right->type == TypeInfo{TypeKind::Char, ""}) &&
                    binary->type == TypeInfo{TypeKind::String, ""}) {
                    return;
                }
                throw BuildLocationError(binary->location, "IR verifier found an invalid `+` expression");
            case TokenType::Minus:
            case TokenType::Star:
            case TokenType::Slash:
                if (binary->left->type != TypeInfo{TypeKind::Int, ""} ||
                    binary->right->type != TypeInfo{TypeKind::Int, ""} ||
                    binary->type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(binary->location, "IR verifier found an invalid arithmetic expression");
                }
                return;
            default:
                throw BuildLocationError(binary->location, "IR verifier encountered an unknown binary operator");
            }
        }

        if (const auto* range = dynamic_cast<const IrRangeExpr*>(expr)) {
            VerifyExpression(range->start.get(), scope);
            VerifyExpression(range->end.get(), scope);
            if (range->start->type != TypeInfo{TypeKind::Int, ""} || range->end->type != TypeInfo{TypeKind::Int, ""} ||
                range->type != TypeInfo{TypeKind::Slice, "Int"}) {
                throw BuildLocationError(range->location, "IR verifier found an invalid range expression");
            }
            return;
        }

        if (const auto* call = dynamic_cast<const IrCallExpr*>(expr)) {
            std::vector<TypeInfo> argument_types;
            argument_types.reserve(call->arguments.size());
            for (const auto& argument : call->arguments) {
                VerifyExpression(argument.get(), scope);
                argument_types.push_back(argument->type);
            }

            if (call->call_kind == IrCallKind::Builtin) {
                VerifyBuiltinCall(call->location, call->type, call->builtin_kind, call->callee_name, argument_types);
                return;
            }

            if (call->builtin_kind != IrBuiltinKind::None) {
                throw BuildLocationError(call->location, "IR verifier found a user call carrying a builtin tag");
            }

            const FunctionInfo& function_info = functions_.Get(call->callee_name, call->location);
            if (function_info.is_builtin) {
                throw BuildLocationError(call->location, "IR verifier found a builtin call tagged as user-defined");
            }
            if (function_info.parameter_types != argument_types || function_info.return_type != call->type) {
                throw BuildLocationError(call->location, "IR verifier found a malformed user function call");
            }
            return;
        }

        if (const auto* struct_literal = dynamic_cast<const IrStructLiteralExpr*>(expr)) {
            if (struct_literal->type.kind != TypeKind::Named || struct_literal->type.name != struct_literal->type_name) {
                throw BuildLocationError(struct_literal->location, "IR verifier found an invalid struct literal type");
            }
            const StructInfo& struct_info = structs_.Get(struct_literal->type_name, struct_literal->location);
            if (struct_info.fields.size() != struct_literal->fields.size()) {
                throw BuildLocationError(struct_literal->location, "IR verifier found an incomplete struct literal");
            }
            std::unordered_set<std::string> seen_fields;
            for (const auto& field : struct_literal->fields) {
                VerifyExpression(field.value.get(), scope);
                const auto field_it = struct_info.fields.find(field.name);
                if (field_it == struct_info.fields.end() || !seen_fields.insert(field.name).second ||
                    field_it->second != field.value->type) {
                    throw BuildLocationError(struct_literal->location, "IR verifier found an invalid struct literal field");
                }
            }
            return;
        }

        if (const auto* member = dynamic_cast<const IrMemberExpr*>(expr)) {
            VerifyExpression(member->object.get(), scope);
            if (member->object->type.kind != TypeKind::Named) {
                throw BuildLocationError(member->location, "IR verifier found a member access on a non-struct type");
            }
            const StructInfo& struct_info = structs_.Get(member->object->type.name, member->location);
            const auto field_it = struct_info.fields.find(member->member_name);
            if (field_it == struct_info.fields.end() || field_it->second != member->type) {
                throw BuildLocationError(member->location, "IR verifier found an invalid struct field access");
            }
            return;
        }

        if (const auto* index = dynamic_cast<const IrIndexExpr*>(expr)) {
            VerifyExpression(index->object.get(), scope);
            VerifyExpression(index->index.get(), scope);
            if (index->index->type != TypeInfo{TypeKind::Int, ""}) {
                throw BuildLocationError(index->location, "IR verifier requires index operands to be `Int`");
            }
            if (index->object->type.kind == TypeKind::Slice &&
                index->type == TypeInfoFromAnnotation(index->object->type.name)) {
                return;
            }
            if (index->object->type == TypeInfo{TypeKind::String, ""} && index->type == TypeInfo{TypeKind::Char, ""}) {
                return;
            }
            throw BuildLocationError(index->location, "IR verifier found an invalid index expression");
        }

        if (const auto* slice = dynamic_cast<const IrSliceExpr*>(expr)) {
            VerifyExpression(slice->object.get(), scope);
            if (slice->start) {
                VerifyExpression(slice->start.get(), scope);
                if (slice->start->type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(slice->location, "IR verifier requires slice start indices to be `Int`");
                }
            }
            if (slice->end) {
                VerifyExpression(slice->end.get(), scope);
                if (slice->end->type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(slice->location, "IR verifier requires slice end indices to be `Int`");
                }
            }
            if (slice->object->type != slice->type || (slice->type.kind != TypeKind::Slice && slice->type != TypeInfo{TypeKind::String, ""})) {
                throw BuildLocationError(slice->location, "IR verifier found an invalid slice expression");
            }
            return;
        }

        throw BuildLocationError(expr->location, "IR verifier encountered an unknown expression kind");
    }
};

class CfgProgramVerifier {
  public:
    explicit CfgProgramVerifier(const CfgProgram& program) : program_(program) {}

    void Verify() {
        for (const auto& struct_decl : program_.structs) {
            structs_.Add(struct_decl.location, struct_decl.full_name, struct_decl.fields);
        }
        for (const auto& struct_decl : program_.structs) {
            for (const auto& field : struct_decl.fields) {
                VerifyTypeExists(field.type, structs_, struct_decl.location);
            }
        }
        for (const auto& function : program_.functions) {
            std::vector<TypeInfo> parameter_types;
            parameter_types.reserve(function.parameters.size());
            for (const auto& parameter : function.parameters) {
                VerifyTypeExists(parameter.type, structs_, function.location);
                parameter_types.push_back(parameter.type);
            }
            VerifyTypeExists(function.return_type, structs_, function.location);
            functions_.AddUser(function.location, function.full_name, parameter_types, function.return_type);
        }
        for (const auto& function : program_.functions) {
            VerifyFunction(function);
        }
    }

  private:
    const CfgProgram& program_;
    StructTable structs_;
    FunctionTable functions_;

    void VerifyFunction(const CfgFunctionDecl& function) {
        if (function.blocks.empty()) {
            throw BuildLocationError(function.location, "CFG verifier found a function with no basic blocks");
        }
        if (function.entry_block >= function.blocks.size()) {
            throw BuildLocationError(function.location, "CFG verifier found an invalid entry block");
        }

        std::unordered_map<CfgBlockId, const CfgBlock*> blocks;
        std::unordered_map<CfgBlockId, std::size_t> predecessor_counts;
        std::unordered_set<CfgValueId> function_value_ids;
        std::unordered_set<std::string> parameter_names;

        for (const auto& parameter : function.parameters) {
            if (!parameter_names.insert(parameter.name).second) {
                throw BuildLocationError(function.location, "CFG verifier found duplicate parameter names");
            }
            if (parameter.value >= function.value_count || !function_value_ids.insert(parameter.value).second) {
                throw BuildLocationError(function.location, "CFG verifier found invalid function parameter value ids");
            }
        }

        for (std::size_t i = 0; i < function.blocks.size(); ++i) {
            const CfgBlock& block = function.blocks[i];
            if (block.id != i) {
                throw BuildLocationError(function.location, "CFG verifier requires contiguous block ids");
            }
            blocks.emplace(block.id, &block);
            predecessor_counts[block.id] = 0;

            std::unordered_set<CfgValueId> block_parameter_values;
            for (const auto& parameter : block.parameters) {
                VerifyTypeExists(parameter.type, structs_, block.terminator.location);
                if (parameter.value >= function.value_count || !function_value_ids.insert(parameter.value).second ||
                    !block_parameter_values.insert(parameter.value).second) {
                    throw BuildLocationError(function.location, "CFG verifier found invalid block parameter value ids");
                }
            }
        }

        for (const auto& block : function.blocks) {
            CountPredecessors(block.terminator, predecessor_counts, blocks, block.terminator.location);
        }
        for (const auto& [block_id, count] : predecessor_counts) {
            if (block_id != function.entry_block && count == 0) {
                throw BuildLocationError(function.location, "CFG verifier found an unreachable block");
            }
        }

        std::unordered_set<CfgValueId> all_results = function_value_ids;
        for (const auto& block : function.blocks) {
            std::unordered_map<CfgValueId, TypeInfo> available_types;
            for (const auto& parameter : function.parameters) {
                available_types.emplace(parameter.value, parameter.type);
            }
            for (const auto& parameter : block.parameters) {
                available_types.emplace(parameter.value, parameter.type);
            }

            for (const auto& instruction : block.instructions) {
                VerifyInstruction(function, instruction, available_types);
                if (instruction.kind == CfgInstructionKind::Drop) {
                    if (instruction.result != kInvalidCfgValueId) {
                        throw BuildLocationError(instruction.location, "CFG verifier requires `drop` to have no result");
                    }
                    continue;
                }
                if (instruction.result >= function.value_count || !all_results.insert(instruction.result).second) {
                    throw BuildLocationError(instruction.location, "CFG verifier found duplicate instruction value ids");
                }
                available_types[instruction.result] = instruction.type;
            }

            VerifyTerminator(function, block.terminator, blocks, available_types);
        }
    }

    void CountPredecessors(const CfgTerminator& terminator,
                           std::unordered_map<CfgBlockId, std::size_t>& predecessor_counts,
                           const std::unordered_map<CfgBlockId, const CfgBlock*>& blocks,
                           const SourceLocation& location) {
        const auto bump = [&](const CfgBlockTarget& target) {
            if (blocks.find(target.block) == blocks.end()) {
                throw BuildLocationError(location, "CFG verifier found a jump to an unknown block");
            }
            ++predecessor_counts[target.block];
        };

        switch (terminator.kind) {
        case CfgTerminatorKind::Jump:
            bump(terminator.target);
            return;
        case CfgTerminatorKind::Branch:
            bump(terminator.true_target);
            bump(terminator.false_target);
            return;
        case CfgTerminatorKind::Return:
            return;
        case CfgTerminatorKind::None:
            throw BuildLocationError(location, "CFG verifier found a block without a terminator");
        }
    }

    TypeInfo RequireAvailable(const std::unordered_map<CfgValueId, TypeInfo>& available_types,
                              CfgValueId value,
                              const SourceLocation& location) const {
        const auto it = available_types.find(value);
        if (it == available_types.end()) {
            throw BuildLocationError(location, "CFG verifier found a use of undefined value " + FormatValueId(value));
        }
        return it->second;
    }

    void VerifyInstruction(const CfgFunctionDecl& function,
                           const CfgInstruction& instruction,
                           const std::unordered_map<CfgValueId, TypeInfo>& available_types) {
        VerifyTypeExists(instruction.type, structs_, instruction.location);
        std::vector<TypeInfo> input_types;
        input_types.reserve(instruction.inputs.size());
        for (const CfgValueId input : instruction.inputs) {
            input_types.push_back(RequireAvailable(available_types, input, instruction.location));
        }

        switch (instruction.kind) {
        case CfgInstructionKind::Literal:
            if (TypeInfoFromValue(instruction.literal_value) != instruction.type) {
                throw BuildLocationError(instruction.location, "CFG verifier found a literal with the wrong type");
            }
            return;
        case CfgInstructionKind::Copy:
            if (input_types.size() != 1 || input_types[0] != instruction.type) {
                throw BuildLocationError(instruction.location, "CFG verifier found an invalid `copy`");
            }
            return;
        case CfgInstructionKind::Drop:
            if (input_types.size() != 1) {
                throw BuildLocationError(instruction.location, "CFG verifier found an invalid `drop`");
            }
            return;
        case CfgInstructionKind::ArrayLiteral:
            if (instruction.type.kind != TypeKind::Slice || TypeInfoFromAnnotation(instruction.type.name).name != TypeInfoFromAnnotation(instruction.name).name ||
                TypeInfoFromAnnotation(instruction.type.name).kind != TypeInfoFromAnnotation(instruction.name).kind) {
                if (instruction.type.kind != TypeKind::Slice || instruction.type.name != instruction.name) {
                    throw BuildLocationError(instruction.location, "CFG verifier found an invalid array literal type");
                }
            }
            for (const TypeInfo& input_type : input_types) {
                if (input_type != TypeInfoFromAnnotation(instruction.name)) {
                    throw BuildLocationError(instruction.location, "CFG verifier found a mixed-type array literal");
                }
            }
            return;
        case CfgInstructionKind::StructLiteral: {
            if (instruction.type.kind != TypeKind::Named || instruction.type.name != instruction.name) {
                throw BuildLocationError(instruction.location, "CFG verifier found an invalid struct literal type");
            }
            const StructInfo& struct_info = structs_.Get(instruction.name, instruction.location);
            if (instruction.fields.size() != struct_info.fields.size()) {
                throw BuildLocationError(instruction.location, "CFG verifier found an incomplete struct literal");
            }
            std::unordered_set<std::string> seen_fields;
            for (const auto& field : instruction.fields) {
                const auto field_it = struct_info.fields.find(field.name);
                if (field_it == struct_info.fields.end() || !seen_fields.insert(field.name).second) {
                    throw BuildLocationError(instruction.location, "CFG verifier found an invalid struct literal field");
                }
                if (RequireAvailable(available_types, field.value, instruction.location) != field_it->second) {
                    throw BuildLocationError(instruction.location, "CFG verifier found a malformed struct literal value");
                }
            }
            return;
        }
        case CfgInstructionKind::Unary:
            if (input_types.size() != 1) {
                throw BuildLocationError(instruction.location, "CFG verifier found an invalid unary instruction");
            }
            if (instruction.token == TokenType::Bang &&
                (input_types[0] != TypeInfo{TypeKind::Bool, ""} || instruction.type != TypeInfo{TypeKind::Bool, ""})) {
                throw BuildLocationError(instruction.location, "CFG verifier found an invalid unary `!`");
            }
            if (instruction.token == TokenType::Minus &&
                (input_types[0] != TypeInfo{TypeKind::Int, ""} || instruction.type != TypeInfo{TypeKind::Int, ""})) {
                throw BuildLocationError(instruction.location, "CFG verifier found an invalid unary `-`");
            }
            return;
        case CfgInstructionKind::Binary: {
            if (input_types.size() != 2) {
                throw BuildLocationError(instruction.location, "CFG verifier found an invalid binary instruction");
            }
            const IrBinaryExpr pseudo_binary(instruction.location,
                                             instruction.type,
                                             std::make_unique<IrLiteralExpr>(instruction.location, input_types[0], std::monostate{}),
                                             instruction.token,
                                             std::make_unique<IrLiteralExpr>(instruction.location, input_types[1], std::monostate{}));
            IrVerifierScope unused_scope;
            IrProgram empty_program;
            IrProgramVerifier unused_verifier(empty_program);
            (void)pseudo_binary;
            switch (instruction.token) {
            case TokenType::AndAnd:
            case TokenType::OrOr:
                if (input_types[0] != TypeInfo{TypeKind::Bool, ""} || input_types[1] != TypeInfo{TypeKind::Bool, ""} ||
                    instruction.type != TypeInfo{TypeKind::Bool, ""}) {
                    throw BuildLocationError(instruction.location, "CFG verifier found an invalid logical instruction");
                }
                return;
            case TokenType::EqualEqual:
            case TokenType::BangEqual:
                if (instruction.type != TypeInfo{TypeKind::Bool, ""} &&
                    !(input_types[0] == input_types[1] || (IsStringLikeType(input_types[0]) && IsStringLikeType(input_types[1])))) {
                    throw BuildLocationError(instruction.location, "CFG verifier found an invalid equality instruction");
                }
                if (!(input_types[0] == input_types[1] || (IsStringLikeType(input_types[0]) && IsStringLikeType(input_types[1])))) {
                    throw BuildLocationError(instruction.location, "CFG verifier found an invalid equality instruction");
                }
                return;
            case TokenType::Greater:
            case TokenType::GreaterEqual:
            case TokenType::Less:
            case TokenType::LessEqual:
                if (input_types[0] != TypeInfo{TypeKind::Int, ""} || input_types[1] != TypeInfo{TypeKind::Int, ""} ||
                    instruction.type != TypeInfo{TypeKind::Bool, ""}) {
                    throw BuildLocationError(instruction.location, "CFG verifier found an invalid comparison instruction");
                }
                return;
            case TokenType::Plus:
                if (input_types[0] == TypeInfo{TypeKind::Int, ""} && input_types[1] == TypeInfo{TypeKind::Int, ""} &&
                    instruction.type == TypeInfo{TypeKind::Int, ""}) {
                    return;
                }
                if (IsStringLikeType(input_types[0]) && IsStringLikeType(input_types[1]) &&
                    !(input_types[0] == TypeInfo{TypeKind::Char, ""} && input_types[1] == TypeInfo{TypeKind::Char, ""}) &&
                    instruction.type == TypeInfo{TypeKind::String, ""}) {
                    return;
                }
                throw BuildLocationError(instruction.location, "CFG verifier found an invalid `+` instruction");
            case TokenType::Minus:
            case TokenType::Star:
            case TokenType::Slash:
                if (input_types[0] != TypeInfo{TypeKind::Int, ""} || input_types[1] != TypeInfo{TypeKind::Int, ""} ||
                    instruction.type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(instruction.location, "CFG verifier found an invalid arithmetic instruction");
                }
                return;
            default:
                throw BuildLocationError(instruction.location, "CFG verifier encountered an unknown binary operator");
            }
        }
        case CfgInstructionKind::Range:
            if (input_types.size() != 2 || input_types[0] != TypeInfo{TypeKind::Int, ""} ||
                input_types[1] != TypeInfo{TypeKind::Int, ""} || instruction.type != TypeInfo{TypeKind::Slice, "Int"}) {
                throw BuildLocationError(instruction.location, "CFG verifier found an invalid range instruction");
            }
            return;
        case CfgInstructionKind::Call: {
            if (instruction.call_kind == IrCallKind::Builtin) {
                VerifyBuiltinCall(instruction.location, instruction.type, instruction.builtin_kind, instruction.name, input_types);
                return;
            }
            if (instruction.builtin_kind != IrBuiltinKind::None) {
                throw BuildLocationError(instruction.location, "CFG verifier found a user call with a builtin tag");
            }
            const FunctionInfo& function_info = functions_.Get(instruction.name, instruction.location);
            if (function_info.parameter_types != input_types || function_info.return_type != instruction.type) {
                throw BuildLocationError(instruction.location, "CFG verifier found a malformed user call");
            }
            return;
        }
        case CfgInstructionKind::GetField: {
            if (input_types.size() != 1 || input_types[0].kind != TypeKind::Named) {
                throw BuildLocationError(instruction.location, "CFG verifier found an invalid field access");
            }
            const StructInfo& struct_info = structs_.Get(input_types[0].name, instruction.location);
            const auto field_it = struct_info.fields.find(instruction.name);
            if (field_it == struct_info.fields.end() || field_it->second != instruction.type) {
                throw BuildLocationError(instruction.location, "CFG verifier found an invalid field access");
            }
            return;
        }
        case CfgInstructionKind::GetIndex:
            if (input_types.size() != 2 || input_types[1] != TypeInfo{TypeKind::Int, ""}) {
                throw BuildLocationError(instruction.location, "CFG verifier found an invalid index instruction");
            }
            if (instruction.name == "Slice" && input_types[0].kind == TypeKind::Slice &&
                instruction.type == TypeInfoFromAnnotation(input_types[0].name)) {
                return;
            }
            if (instruction.name == "String" && input_types[0] == TypeInfo{TypeKind::String, ""} &&
                instruction.type == TypeInfo{TypeKind::Char, ""}) {
                return;
            }
            throw BuildLocationError(instruction.location, "CFG verifier found an invalid index instruction");
        case CfgInstructionKind::GetSlice:
            if (input_types.empty()) {
                throw BuildLocationError(instruction.location, "CFG verifier found an invalid slice instruction");
            }
            if ((instruction.name == "Slice" && input_types[0] == instruction.type && instruction.type.kind == TypeKind::Slice) ||
                (instruction.name == "String" && input_types[0] == TypeInfo{TypeKind::String, ""} &&
                 instruction.type == TypeInfo{TypeKind::String, ""})) {
                for (std::size_t i = 1; i < input_types.size(); ++i) {
                    if (input_types[i] != TypeInfo{TypeKind::Int, ""}) {
                        throw BuildLocationError(instruction.location, "CFG verifier found a non-int slice bound");
                    }
                }
                return;
            }
            throw BuildLocationError(instruction.location, "CFG verifier found an invalid slice instruction");
        case CfgInstructionKind::AssignField: {
            if (input_types.size() != 2 || input_types[0].kind != TypeKind::Named) {
                throw BuildLocationError(instruction.location, "CFG verifier found an invalid field assignment");
            }
            const StructInfo& struct_info = structs_.Get(input_types[0].name, instruction.location);
            const auto field_it = struct_info.fields.find(instruction.name);
            if (field_it == struct_info.fields.end() || field_it->second != input_types[1] || instruction.type != input_types[1]) {
                throw BuildLocationError(instruction.location, "CFG verifier found an invalid field assignment");
            }
            return;
        }
        case CfgInstructionKind::AssignIndex:
            if (input_types.size() != 3 || input_types[1] != TypeInfo{TypeKind::Int, ""} || instruction.type != input_types[2]) {
                throw BuildLocationError(instruction.location, "CFG verifier found an invalid index assignment");
            }
            if (input_types[0].kind == TypeKind::Slice && TypeInfoFromAnnotation(input_types[0].name) == input_types[2]) {
                return;
            }
            if (input_types[0] == TypeInfo{TypeKind::String, ""}) {
                return;
            }
            throw BuildLocationError(instruction.location, "CFG verifier found an invalid index assignment");
        }
        (void)function;
    }

    void VerifyTarget(const CfgBlockTarget& target,
                      const std::unordered_map<CfgBlockId, const CfgBlock*>& blocks,
                      const std::unordered_map<CfgValueId, TypeInfo>& available_types,
                      const SourceLocation& location) {
        const auto block_it = blocks.find(target.block);
        if (block_it == blocks.end()) {
            throw BuildLocationError(location, "CFG verifier found a jump to an unknown block");
        }
        const CfgBlock& target_block = *block_it->second;
        if (target_block.parameters.size() != target.arguments.size()) {
            throw BuildLocationError(location, "CFG verifier found a jump with the wrong block-argument count");
        }
        for (std::size_t i = 0; i < target.arguments.size(); ++i) {
            if (RequireAvailable(available_types, target.arguments[i], location) != target_block.parameters[i].type) {
                throw BuildLocationError(location, "CFG verifier found a jump with the wrong block-argument types");
            }
        }
    }

    void VerifyTerminator(const CfgFunctionDecl& function,
                          const CfgTerminator& terminator,
                          const std::unordered_map<CfgBlockId, const CfgBlock*>& blocks,
                          const std::unordered_map<CfgValueId, TypeInfo>& available_types) {
        switch (terminator.kind) {
        case CfgTerminatorKind::Jump:
            VerifyTarget(terminator.target, blocks, available_types, terminator.location);
            return;
        case CfgTerminatorKind::Branch:
            if (!terminator.condition.has_value() ||
                RequireAvailable(available_types, *terminator.condition, terminator.location) != TypeInfo{TypeKind::Bool, ""}) {
                throw BuildLocationError(terminator.location, "CFG verifier requires branch conditions to be `Bool`");
            }
            VerifyTarget(terminator.true_target, blocks, available_types, terminator.location);
            VerifyTarget(terminator.false_target, blocks, available_types, terminator.location);
            return;
        case CfgTerminatorKind::Return:
            if (terminator.return_value.has_value()) {
                if (RequireAvailable(available_types, *terminator.return_value, terminator.location) != function.return_type) {
                    throw BuildLocationError(terminator.location, "CFG verifier found a return type mismatch");
                }
            } else if (function.return_type != TypeInfo{TypeKind::Unit, ""}) {
                throw BuildLocationError(terminator.location, "CFG verifier found a missing return value");
            }
            return;
        case CfgTerminatorKind::None:
            throw BuildLocationError(terminator.location, "CFG verifier found a block without a terminator");
        }
    }
};

}  // namespace

void VerifyIrProgram(const IrProgram& program) {
    IrProgramVerifier(program).Verify();
}

void VerifyCfgProgram(const CfgProgram& program) {
    CfgProgramVerifier(program).Verify();
}
