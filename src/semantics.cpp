#include "semantics.hpp"

#include <sstream>

namespace {

bool IsStringLikeType(const TypeInfo& type) {
    return type == TypeInfo{TypeKind::String, ""} || type == TypeInfo{TypeKind::Char, ""};
}

bool IsStringCollectionType(const TypeInfo& type) {
    return type == TypeInfo{TypeKind::Slice, "String"} || type == TypeInfo{TypeKind::Slice, "Char"};
}

}  // namespace

void TypeScopeStack::Push() {
    scopes_.emplace_back();
}

void TypeScopeStack::Pop() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

void TypeScopeStack::Declare(const std::string& name, TypeInfo type, const SourceLocation& location) {
    if (scopes_.empty()) {
        Push();
    }

    auto& scope = scopes_.back();
    if (scope.find(name) != scope.end()) {
        throw BuildLocationError(location, "Name `" + name + "` is already declared in this scope");
    }
    scope[name] = std::move(type);
}

TypeInfo TypeScopeStack::Get(const std::string& name, const SourceLocation& location) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }

    throw BuildLocationError(location, "Undefined variable: " + name);
}

TypeScopeFrame::TypeScopeFrame(TypeScopeStack& scopes) : scopes_(scopes) {
    scopes_.Push();
}

TypeScopeFrame::~TypeScopeFrame() {
    scopes_.Pop();
}

TypeChecker::TypeChecker(const Program& program) : program_(program) {}

void TypeChecker::Check() {
    RegisterBuiltins();
    CollectStructDeclarations();
    CollectFunctionSignatures();
    CheckMainSignature();

    for (const auto& function : program_.functions) {
        CheckFunction(function);
    }

    for (const auto& struct_decl : program_.structs) {
        for (const auto& method : struct_decl.methods) {
            CheckFunction(method);
        }
    }
}

void TypeChecker::RegisterBuiltins() {
    function_signatures_["print"] = FunctionSignature{{}, {TypeKind::Unit, ""}, true};
    function_signatures_["len"] = FunctionSignature{{}, {TypeKind::Int, ""}, true};
    function_signatures_["push"] = FunctionSignature{{}, {TypeKind::Unit, ""}, true};
    function_signatures_["pop"] = FunctionSignature{{}, {TypeKind::Unit, ""}, true};
    function_signatures_["insert"] = FunctionSignature{{}, {TypeKind::Unit, ""}, true};
    function_signatures_["remove_at"] = FunctionSignature{{}, {TypeKind::Unit, ""}, true};
    function_signatures_["clear"] = FunctionSignature{{}, {TypeKind::Unit, ""}, true};
    function_signatures_["contains"] = FunctionSignature{{}, {TypeKind::Bool, ""}, true};
    function_signatures_["starts_with"] = FunctionSignature{{}, {TypeKind::Bool, ""}, true};
    function_signatures_["ends_with"] = FunctionSignature{{}, {TypeKind::Bool, ""}, true};
    function_signatures_["join"] = FunctionSignature{{}, {TypeKind::String, ""}, true};
    function_signatures_["file_exists"] = FunctionSignature{{}, {TypeKind::Bool, ""}, true};
    function_signatures_["read_text"] = FunctionSignature{{}, {TypeKind::String, ""}, true};
    function_signatures_["write_text"] = FunctionSignature{{}, {TypeKind::Unit, ""}, true};
    function_signatures_["append_text"] = FunctionSignature{{}, {TypeKind::Unit, ""}, true};
}

void TypeChecker::CollectStructDeclarations() {
    for (const auto& struct_decl : program_.structs) {
        const std::string full_name = GetFullStructName(struct_decl);
        if (struct_signatures_.find(full_name) != struct_signatures_.end()) {
            throw BuildLocationError(struct_decl.location, "Struct name is already declared: " + full_name);
        }

        struct_signatures_[full_name] = StructSignature{};
    }

    for (const auto& struct_decl : program_.structs) {
        const std::string full_name = GetFullStructName(struct_decl);
        StructSignature& signature = struct_signatures_[full_name];

        for (const auto& field : struct_decl.fields) {
            if (signature.fields.find(field.name) != signature.fields.end()) {
                throw BuildLocationError(struct_decl.location, "Struct field is already declared: " + field.name);
            }

            signature.fields[field.name] = ResolveTypeName(field.type_name, struct_decl.module_name, struct_decl.location);
        }
    }
}

void TypeChecker::CollectFunctionSignatures() {
    const auto register_function = [this](const FunctionDecl& function) {
        const std::string full_name = GetFullFunctionName(function);
        if (function_signatures_.find(full_name) != function_signatures_.end()) {
            throw BuildLocationError(function.location, "Function name is already declared: " + full_name);
        }

        FunctionSignature signature;
        signature.return_type =
            function.return_type.has_value() ? ResolveTypeName(*function.return_type, function.module_name, function.location)
                                             : TypeInfo{TypeKind::Unit, ""};

        for (const auto& parameter : function.parameters) {
            signature.parameter_types.push_back(ResolveTypeName(parameter.type_name, function.module_name, function.location));
        }

        if (function.owner_type_name.has_value()) {
            const TypeInfo owner_type =
                ResolveTypeName(*function.owner_type_name, function.module_name, function.location);

            if (function.parameters.empty()) {
                throw BuildLocationError(function.location,
                                         "Method `" + full_name + "` must declare `self` as its first parameter");
            }

            if (function.parameters[0].name != "self") {
                throw BuildLocationError(function.location,
                                         "Method `" + full_name + "` must use `self` as its first parameter name");
            }

            if (signature.parameter_types[0] != owner_type) {
                throw BuildLocationError(function.location,
                                         "Method `" + full_name + "` must declare `self: " +
                                             TypeInfoName(owner_type) + "`");
            }
        }

        function_signatures_[full_name] = std::move(signature);
    };

    for (const auto& function : program_.functions) {
        register_function(function);
    }

    for (const auto& struct_decl : program_.structs) {
        for (const auto& method : struct_decl.methods) {
            register_function(method);
        }
    }
}

void TypeChecker::CheckMainSignature() const {
    const auto main_it = function_signatures_.find("main");
    if (main_it == function_signatures_.end()) {
        return;
    }

    if (!main_it->second.parameter_types.empty()) {
        throw AuraError("The main function must not take parameters");
    }
}

void TypeChecker::CheckFunction(const FunctionDecl& function) {
    current_function_name_ = GetFullFunctionName(function);
    current_module_name_ = function.module_name;
    current_return_type_ =
        function.return_type.has_value() ? ResolveTypeName(*function.return_type, function.module_name, function.location)
                                         : TypeInfo{TypeKind::Unit, ""};

    TypeScopeStack scopes;
    scopes.Push();

    const auto signature_it = function_signatures_.find(current_function_name_);
    const auto& parameter_types = signature_it->second.parameter_types;
    for (std::size_t i = 0; i < function.parameters.size(); ++i) {
        scopes.Declare(function.parameters[i].name, parameter_types[i], function.location);
    }

    const bool definitely_returns = CheckStatements(function.body, scopes);
    if (current_return_type_ != TypeInfo{TypeKind::Unit, ""} && !definitely_returns) {
        throw BuildLocationError(
            function.location,
            "Function `" + current_function_name_ + "` does not return `" + TypeInfoName(current_return_type_) +
                "` on every path");
    }
}

bool TypeChecker::CheckStatements(const std::vector<std::unique_ptr<Stmt>>& statements, TypeScopeStack& scopes) {
    bool definitely_returns = false;
    for (const auto& statement : statements) {
        if (definitely_returns) {
            break;
        }
        definitely_returns = CheckStatement(statement.get(), scopes);
    }
    return definitely_returns;
}

bool TypeChecker::CheckStatement(const Stmt* stmt, TypeScopeStack& scopes) {
    if (const auto* block_stmt = dynamic_cast<const BlockStmt*>(stmt)) {
        TypeScopeFrame frame(scopes);
        return CheckStatements(block_stmt->statements, scopes);
    }

    if (const auto* let_stmt = dynamic_cast<const LetStmt*>(stmt)) {
        const TypeInfo initializer_type = CheckExpression(let_stmt->initializer.get(), scopes);
        const TypeInfo declared_type =
            let_stmt->type_name.has_value() ? ResolveTypeName(*let_stmt->type_name, current_module_name_, let_stmt->location)
                                            : initializer_type;

        if (initializer_type != declared_type) {
            throw BuildLocationError(let_stmt->location,
                                     "Variable `" + let_stmt->name + "` expects `" + TypeInfoName(declared_type) +
                                         "`, but got `" + TypeInfoName(initializer_type) + "`");
        }

        scopes.Declare(let_stmt->name, declared_type, let_stmt->location);
        return false;
    }

    if (const auto* return_stmt = dynamic_cast<const ReturnStmt*>(stmt)) {
        const TypeInfo actual_type =
            return_stmt->value ? CheckExpression(return_stmt->value.get(), scopes) : TypeInfo{TypeKind::Unit, ""};

        if (actual_type != current_return_type_) {
            throw BuildLocationError(return_stmt->location,
                                     "Function `" + current_function_name_ + "` must return `" +
                                         TypeInfoName(current_return_type_) + "`, but got `" +
                                         TypeInfoName(actual_type) + "`");
        }
        return true;
    }

    if (const auto* if_stmt = dynamic_cast<const IfStmt*>(stmt)) {
        const TypeInfo condition_type = CheckExpression(if_stmt->condition.get(), scopes);
        if (condition_type != TypeInfo{TypeKind::Bool, ""}) {
            throw BuildLocationError(if_stmt->condition->location, "An if condition must be Bool");
        }

        const bool then_returns = CheckScopedStatement(if_stmt->then_branch.get(), scopes);
        const bool else_returns = if_stmt->else_branch ? CheckScopedStatement(if_stmt->else_branch.get(), scopes) : false;
        return then_returns && else_returns;
    }

    if (const auto* while_stmt = dynamic_cast<const WhileStmt*>(stmt)) {
        const TypeInfo condition_type = CheckExpression(while_stmt->condition.get(), scopes);
        if (condition_type != TypeInfo{TypeKind::Bool, ""}) {
            throw BuildLocationError(while_stmt->condition->location, "A while condition must be Bool");
        }

        CheckScopedStatement(while_stmt->body.get(), scopes);
        return false;
    }

    if (const auto* for_stmt = dynamic_cast<const ForStmt*>(stmt)) {
        const TypeInfo iterable_type = CheckExpression(for_stmt->iterable.get(), scopes);
        if (iterable_type.kind != TypeKind::Slice && iterable_type != TypeInfo{TypeKind::String, ""}) {
            throw BuildLocationError(for_stmt->iterable->location, "A for loop requires a slice value or String");
        }

        TypeScopeFrame frame(scopes);
        if (for_stmt->index_name.has_value()) {
            scopes.Declare(*for_stmt->index_name, {TypeKind::Int, ""}, for_stmt->location);
        }
        const TypeInfo element_type =
            iterable_type.kind == TypeKind::Slice ? TypeInfoFromAnnotation(iterable_type.name)
                                                  : TypeInfo{TypeKind::Char, ""};
        scopes.Declare(for_stmt->variable_name, element_type, for_stmt->location);
        CheckStatement(for_stmt->body.get(), scopes);
        return false;
    }

    if (const auto* expr_stmt = dynamic_cast<const ExprStmt*>(stmt)) {
        CheckExpression(expr_stmt->expression.get(), scopes);
        return false;
    }

        throw BuildLocationError(stmt->location, "Unknown statement kind");
}

bool TypeChecker::CheckScopedStatement(const Stmt* stmt, TypeScopeStack& scopes) {
    TypeScopeFrame frame(scopes);
    return CheckStatement(stmt, scopes);
}

TypeInfo TypeChecker::CheckExpression(const Expr* expr, TypeScopeStack& scopes) {
    if (const auto* literal = dynamic_cast<const LiteralExpr*>(expr)) {
        return TypeInfoFromValue(literal->value);
    }

    if (const auto* array_literal = dynamic_cast<const ArrayLiteralExpr*>(expr)) {
        if (array_literal->elements.empty()) {
            throw BuildLocationError(array_literal->location, "Empty array literals are not supported yet");
        }

        const TypeInfo element_type = CheckExpression(array_literal->elements[0].get(), scopes);
        for (std::size_t i = 1; i < array_literal->elements.size(); ++i) {
            const TypeInfo current_type = CheckExpression(array_literal->elements[i].get(), scopes);
            if (current_type != element_type) {
                throw BuildLocationError(array_literal->elements[i]->location,
                                         "Array elements must all have the same type, but got `" +
                                             TypeInfoName(element_type) + "` and `" + TypeInfoName(current_type) +
                                             "`");
            }
        }

        return {TypeKind::Slice, TypeInfoName(element_type)};
    }

    if (const auto* range = dynamic_cast<const RangeExpr*>(expr)) {
        const TypeInfo start_type = CheckExpression(range->start.get(), scopes);
        const TypeInfo end_type = CheckExpression(range->end.get(), scopes);
        if (start_type != TypeInfo{TypeKind::Int, ""} || end_type != TypeInfo{TypeKind::Int, ""}) {
            throw BuildLocationError(range->location, "Range bounds must both be Int");
        }
        return {TypeKind::Slice, "Int"};
    }

    if (const auto* struct_literal = dynamic_cast<const StructLiteralExpr*>(expr)) {
        const TypeInfo struct_type =
            ResolveTypeName(struct_literal->type_name, current_module_name_, struct_literal->location);
        const auto struct_it = struct_signatures_.find(struct_type.name);
        if (struct_it == struct_signatures_.end()) {
            throw BuildLocationError(struct_literal->location, "Unknown struct type: " + struct_literal->type_name);
        }

        const auto& fields = struct_it->second.fields;
        std::unordered_map<std::string, bool> seen_fields;

        for (const auto& field : struct_literal->fields) {
            const auto field_it = fields.find(field.name);
            if (field_it == fields.end()) {
                throw BuildLocationError(struct_literal->location, "Unknown field `" + field.name + "` for `" +
                                                                      TypeInfoName(struct_type) + "`");
            }

            if (seen_fields[field.name]) {
                throw BuildLocationError(struct_literal->location, "Field `" + field.name + "` is initialized more than once");
            }
            seen_fields[field.name] = true;

            const TypeInfo actual_type = CheckExpression(field.value.get(), scopes);
            if (actual_type != field_it->second) {
                throw BuildLocationError(field.value->location,
                                         "Field `" + field.name + "` expects `" + TypeInfoName(field_it->second) +
                                             "`, but got `" + TypeInfoName(actual_type) + "`");
            }
        }

        for (const auto& [field_name, _] : fields) {
            if (!seen_fields[field_name]) {
                throw BuildLocationError(struct_literal->location,
                                         "Missing field `" + field_name + "` in `" + TypeInfoName(struct_type) + "` literal");
            }
        }

        return struct_type;
    }

    if (const auto* variable = dynamic_cast<const VariableExpr*>(expr)) {
        if (variable->name.find("::") != std::string::npos) {
            throw BuildLocationError(variable->location, "Qualified names can only be used in function calls or type names");
        }
        return scopes.Get(variable->name, variable->location);
    }

    if (const auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        const TypeInfo object_type = CheckExpression(member->object.get(), scopes);
        if (object_type.kind != TypeKind::Named) {
            throw BuildLocationError(member->location, "Member access requires a struct value");
        }

        const auto struct_it = struct_signatures_.find(object_type.name);
        if (struct_it == struct_signatures_.end()) {
            throw BuildLocationError(member->location, "Member access requires a struct value");
        }

        const auto field_it = struct_it->second.fields.find(member->member_name);
        if (field_it == struct_it->second.fields.end()) {
            throw BuildLocationError(member->location,
                                     "Struct `" + object_type.name + "` has no field `" + member->member_name + "`");
        }

        return field_it->second;
    }

    if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        const TypeInfo object_type = CheckExpression(index->object.get(), scopes);
        const TypeInfo index_type = CheckExpression(index->index.get(), scopes);
        if (object_type.kind == TypeKind::Slice) {
            if (index_type != TypeInfo{TypeKind::Int, ""}) {
                throw BuildLocationError(index->index->location, "Slice indices must be Int");
            }

            return TypeInfoFromAnnotation(object_type.name);
        }

        if (object_type == TypeInfo{TypeKind::String, ""}) {
            if (index_type != TypeInfo{TypeKind::Int, ""}) {
                throw BuildLocationError(index->index->location, "String indices must be Int");
            }

            return {TypeKind::Char, ""};
        }

        throw BuildLocationError(index->location, "Index access requires a slice value or String");
    }

    if (const auto* slice = dynamic_cast<const SliceExpr*>(expr)) {
        const TypeInfo object_type = CheckExpression(slice->object.get(), scopes);
        if (object_type.kind == TypeKind::Slice) {
            if (slice->start) {
                const TypeInfo start_type = CheckExpression(slice->start.get(), scopes);
                if (start_type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(slice->start->location, "Slice start index must be Int");
                }
            }

            if (slice->end) {
                const TypeInfo end_type = CheckExpression(slice->end.get(), scopes);
                if (end_type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(slice->end->location, "Slice end index must be Int");
                }
            }

            return object_type;
        }

        if (object_type == TypeInfo{TypeKind::String, ""}) {
            if (slice->start) {
                const TypeInfo start_type = CheckExpression(slice->start.get(), scopes);
                if (start_type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(slice->start->location, "String slice start index must be Int");
                }
            }

            if (slice->end) {
                const TypeInfo end_type = CheckExpression(slice->end.get(), scopes);
                if (end_type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(slice->end->location, "String slice end index must be Int");
                }
            }

            return object_type;
        }

        throw BuildLocationError(slice->location, "Slice access requires a slice value or String");
    }

    if (const auto* assign = dynamic_cast<const AssignExpr*>(expr)) {
        const TypeInfo value_type = CheckExpression(assign->value.get(), scopes);

        if (const auto* variable = dynamic_cast<const VariableExpr*>(assign->target.get())) {
            const TypeInfo variable_type = scopes.Get(variable->name, variable->location);
            if (variable_type != value_type) {
                throw BuildLocationError(assign->location,
                                         "Assignment type mismatch: `" + variable->name + "` is `" +
                                             TypeInfoName(variable_type) + "`, but the right side is `" +
                                             TypeInfoName(value_type) + "`");
            }
            return variable_type;
        }

        if (const auto* member = dynamic_cast<const MemberExpr*>(assign->target.get())) {
            const TypeInfo object_type = CheckExpression(member->object.get(), scopes);
            if (object_type.kind != TypeKind::Named) {
                throw BuildLocationError(member->location, "Field assignment requires a struct value");
            }

            const auto struct_it = struct_signatures_.find(object_type.name);
            if (struct_it == struct_signatures_.end()) {
                throw BuildLocationError(member->location, "Field assignment requires a struct value");
            }

            const auto field_it = struct_it->second.fields.find(member->member_name);
            if (field_it == struct_it->second.fields.end()) {
                throw BuildLocationError(member->location,
                                         "Struct `" + object_type.name + "` has no field `" + member->member_name + "`");
            }

            if (field_it->second != value_type) {
                throw BuildLocationError(assign->location,
                                         "Field `" + member->member_name + "` expects `" +
                                             TypeInfoName(field_it->second) + "`, but got `" +
                                             TypeInfoName(value_type) + "`");
            }
            return field_it->second;
        }

        if (const auto* index = dynamic_cast<const IndexExpr*>(assign->target.get())) {
            const TypeInfo object_type = CheckExpression(index->object.get(), scopes);
            if (object_type.kind == TypeKind::Slice) {
                const TypeInfo index_type = CheckExpression(index->index.get(), scopes);
                if (index_type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(index->index->location, "Slice indices must be Int");
                }

                const TypeInfo element_type = TypeInfoFromAnnotation(object_type.name);
                if (element_type != value_type) {
                    throw BuildLocationError(assign->location,
                                             "Slice element expects `" + TypeInfoName(element_type) + "`, but got `" +
                                                 TypeInfoName(value_type) + "`");
                }
                return element_type;
            }

            if (object_type == TypeInfo{TypeKind::String, ""}) {
                throw BuildLocationError(index->location, "String index assignment is not supported");
            }

            throw BuildLocationError(index->location, "Index assignment requires a slice value");
        }

        throw BuildLocationError(assign->location, "Invalid assignment target");
    }

    if (const auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        const TypeInfo right_type = CheckExpression(unary->right.get(), scopes);
        switch (unary->op.type) {
        case TokenType::Bang:
            if (right_type != TypeInfo{TypeKind::Bool, ""}) {
                throw BuildLocationError(unary->location, "Operator `!` requires Bool");
            }
            return {TypeKind::Bool, ""};
        case TokenType::Minus:
            if (right_type != TypeInfo{TypeKind::Int, ""}) {
                throw BuildLocationError(unary->location, "Unary `-` requires Int");
            }
            return {TypeKind::Int, ""};
        default:
            break;
        }
    }

    if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        const TypeInfo left_type = CheckExpression(binary->left.get(), scopes);

        switch (binary->op.type) {
        case TokenType::AndAnd:
        case TokenType::OrOr: {
            const TypeInfo right_type = CheckExpression(binary->right.get(), scopes);
            if (left_type != TypeInfo{TypeKind::Bool, ""} || right_type != TypeInfo{TypeKind::Bool, ""}) {
                throw BuildLocationError(binary->location, "Logical operators only work with `Bool`");
            }
            return {TypeKind::Bool, ""};
        }
        case TokenType::EqualEqual:
        case TokenType::BangEqual: {
            const TypeInfo right_type = CheckExpression(binary->right.get(), scopes);
            if (left_type != right_type && !(IsStringLikeType(left_type) && IsStringLikeType(right_type))) {
                throw BuildLocationError(binary->location,
                                         "Comparison requires matching types, but got `" + TypeInfoName(left_type) +
                                             "` and `" + TypeInfoName(right_type) + "`");
            }
            return {TypeKind::Bool, ""};
        }
        case TokenType::Plus: {
            const TypeInfo right_type = CheckExpression(binary->right.get(), scopes);
            if (left_type == TypeInfo{TypeKind::Int, ""} && right_type == TypeInfo{TypeKind::Int, ""}) {
                return {TypeKind::Int, ""};
            }
            if (IsStringLikeType(left_type) && IsStringLikeType(right_type) &&
                !(left_type == TypeInfo{TypeKind::Char, ""} && right_type == TypeInfo{TypeKind::Char, ""})) {
                return {TypeKind::String, ""};
            }
            throw BuildLocationError(binary->location,
                                     "`+` only accepts `Int + Int`, `String + String`, `String + Char`, or `Char + String`");
        }
        case TokenType::Minus:
        case TokenType::Star:
        case TokenType::Slash: {
            const TypeInfo right_type = CheckExpression(binary->right.get(), scopes);
            if (left_type != TypeInfo{TypeKind::Int, ""} || right_type != TypeInfo{TypeKind::Int, ""}) {
                throw BuildLocationError(binary->location, "Arithmetic operators only work with `Int`");
            }
            return {TypeKind::Int, ""};
        }
        case TokenType::Greater:
        case TokenType::GreaterEqual:
        case TokenType::Less:
        case TokenType::LessEqual: {
            const TypeInfo right_type = CheckExpression(binary->right.get(), scopes);
            if (left_type != TypeInfo{TypeKind::Int, ""} || right_type != TypeInfo{TypeKind::Int, ""}) {
                throw BuildLocationError(binary->location, "Comparison operators only work with `Int`");
            }
            return {TypeKind::Bool, ""};
        }
        default:
            break;
        }
    }

    if (const auto* call = dynamic_cast<const CallExpr*>(expr)) {
        if (const auto* callee = dynamic_cast<const VariableExpr*>(call->callee.get())) {
            if (callee->name == "print") {
                for (const auto& argument : call->arguments) {
                    CheckExpression(argument.get(), scopes);
                }
                return {TypeKind::Unit, ""};
            }

            if (callee->name == "len") {
                if (call->arguments.size() != 1) {
                    throw BuildLocationError(callee->location, "Function `len` expects 1 argument");
                }

                const TypeInfo target_type = CheckExpression(call->arguments[0].get(), scopes);
                if (target_type.kind == TypeKind::Slice || target_type == TypeInfo{TypeKind::String, ""}) {
                    return {TypeKind::Int, ""};
                }

                throw BuildLocationError(call->arguments[0]->location,
                                         "Function `len` expects a slice or String");
            }

            if (callee->name == "push") {
                if (call->arguments.size() != 2) {
                    throw BuildLocationError(callee->location, "Function `push` expects 2 arguments");
                }

                const TypeInfo target_type = CheckExpression(call->arguments[0].get(), scopes);
                if (target_type.kind != TypeKind::Slice) {
                    throw BuildLocationError(call->arguments[0]->location,
                                             "Function `push` expects a slice as argument #1");
                }

                const TypeInfo value_type = CheckExpression(call->arguments[1].get(), scopes);
                const TypeInfo element_type = TypeInfoFromAnnotation(target_type.name);
                if (value_type != element_type) {
                    throw BuildLocationError(call->arguments[1]->location,
                                             "Function `push` expects `" + TypeInfoName(element_type) +
                                                 "` for argument #2, but got `" + TypeInfoName(value_type) + "`");
                }

                return {TypeKind::Unit, ""};
            }

            if (callee->name == "pop") {
                if (call->arguments.size() != 1) {
                    throw BuildLocationError(callee->location, "Function `pop` expects 1 argument");
                }

                const TypeInfo target_type = CheckExpression(call->arguments[0].get(), scopes);
                if (target_type.kind != TypeKind::Slice) {
                    throw BuildLocationError(call->arguments[0]->location,
                                             "Function `pop` expects a slice");
                }

                return TypeInfoFromAnnotation(target_type.name);
            }

            if (callee->name == "insert") {
                if (call->arguments.size() != 3) {
                    throw BuildLocationError(callee->location, "Function `insert` expects 3 arguments");
                }

                const TypeInfo target_type = CheckExpression(call->arguments[0].get(), scopes);
                if (target_type.kind != TypeKind::Slice) {
                    throw BuildLocationError(call->arguments[0]->location,
                                             "Function `insert` expects a slice as argument #1");
                }

                const TypeInfo index_type = CheckExpression(call->arguments[1].get(), scopes);
                if (index_type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(call->arguments[1]->location,
                                             "Function `insert` expects `Int` for argument #2");
                }

                const TypeInfo value_type = CheckExpression(call->arguments[2].get(), scopes);
                const TypeInfo element_type = TypeInfoFromAnnotation(target_type.name);
                if (value_type != element_type) {
                    throw BuildLocationError(call->arguments[2]->location,
                                             "Function `insert` expects `" + TypeInfoName(element_type) +
                                                 "` for argument #3, but got `" + TypeInfoName(value_type) + "`");
                }

                return {TypeKind::Unit, ""};
            }

            if (callee->name == "remove_at") {
                if (call->arguments.size() != 2) {
                    throw BuildLocationError(callee->location, "Function `remove_at` expects 2 arguments");
                }

                const TypeInfo target_type = CheckExpression(call->arguments[0].get(), scopes);
                if (target_type.kind != TypeKind::Slice) {
                    throw BuildLocationError(call->arguments[0]->location,
                                             "Function `remove_at` expects a slice as argument #1");
                }

                const TypeInfo index_type = CheckExpression(call->arguments[1].get(), scopes);
                if (index_type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(call->arguments[1]->location,
                                             "Function `remove_at` expects `Int` for argument #2");
                }

                return TypeInfoFromAnnotation(target_type.name);
            }

            if (callee->name == "clear") {
                if (call->arguments.size() != 1) {
                    throw BuildLocationError(callee->location, "Function `clear` expects 1 argument");
                }

                const TypeInfo target_type = CheckExpression(call->arguments[0].get(), scopes);
                if (target_type.kind != TypeKind::Slice) {
                    throw BuildLocationError(call->arguments[0]->location,
                                             "Function `clear` expects a slice");
                }

                return {TypeKind::Unit, ""};
            }

            if (callee->name == "contains") {
                if (call->arguments.size() != 2) {
                    throw BuildLocationError(callee->location, "Function `contains` expects 2 arguments");
                }

                const TypeInfo target_type = CheckExpression(call->arguments[0].get(), scopes);
                const TypeInfo needle_type = CheckExpression(call->arguments[1].get(), scopes);
                const TypeInfo string_type{TypeKind::String, ""};
                const TypeInfo char_type{TypeKind::Char, ""};

                if (target_type == string_type) {
                    if (!IsStringLikeType(needle_type)) {
                        throw BuildLocationError(call->arguments[1]->location,
                                                 "Function `contains` expects `String` or `Char` for argument #2 when argument #1 is `String`");
                    }
                    return {TypeKind::Bool, ""};
                }

                if (target_type == TypeInfo{TypeKind::Slice, "String"}) {
                    if (needle_type != string_type) {
                        throw BuildLocationError(call->arguments[1]->location,
                                                 "Function `contains` expects `String` for argument #2 when argument #1 is `[String]`");
                    }
                    return {TypeKind::Bool, ""};
                }

                if (target_type == TypeInfo{TypeKind::Slice, "Char"}) {
                    if (needle_type != char_type) {
                        throw BuildLocationError(call->arguments[1]->location,
                                                 "Function `contains` expects `Char` for argument #2 when argument #1 is `[Char]`");
                    }
                    return {TypeKind::Bool, ""};
                }

                throw BuildLocationError(call->arguments[0]->location,
                                         "Function `contains` expects `String`, `[String]`, or `[Char]` as argument #1");
            }

            if (callee->name == "starts_with") {
                if (call->arguments.size() != 2) {
                    throw BuildLocationError(callee->location, "Function `starts_with` expects 2 arguments");
                }

                const TypeInfo target_type = CheckExpression(call->arguments[0].get(), scopes);
                if (target_type != TypeInfo{TypeKind::String, ""}) {
                    throw BuildLocationError(call->arguments[0]->location,
                                             "Function `starts_with` expects `String` as argument #1");
                }

                const TypeInfo prefix_type = CheckExpression(call->arguments[1].get(), scopes);
                if (!IsStringLikeType(prefix_type)) {
                    throw BuildLocationError(call->arguments[1]->location,
                                             "Function `starts_with` expects `String` or `Char` for argument #2");
                }

                return {TypeKind::Bool, ""};
            }

            if (callee->name == "ends_with") {
                if (call->arguments.size() != 2) {
                    throw BuildLocationError(callee->location, "Function `ends_with` expects 2 arguments");
                }

                const TypeInfo target_type = CheckExpression(call->arguments[0].get(), scopes);
                if (target_type != TypeInfo{TypeKind::String, ""}) {
                    throw BuildLocationError(call->arguments[0]->location,
                                             "Function `ends_with` expects `String` as argument #1");
                }

                const TypeInfo suffix_type = CheckExpression(call->arguments[1].get(), scopes);
                if (!IsStringLikeType(suffix_type)) {
                    throw BuildLocationError(call->arguments[1]->location,
                                             "Function `ends_with` expects `String` or `Char` for argument #2");
                }

                return {TypeKind::Bool, ""};
            }

            if (callee->name == "join") {
                if (call->arguments.size() != 2) {
                    throw BuildLocationError(callee->location, "Function `join` expects 2 arguments");
                }

                const TypeInfo values_type = CheckExpression(call->arguments[0].get(), scopes);
                if (!IsStringCollectionType(values_type)) {
                    throw BuildLocationError(call->arguments[0]->location,
                                             "Function `join` expects `[String]` or `[Char]` as argument #1");
                }

                const TypeInfo separator_type = CheckExpression(call->arguments[1].get(), scopes);
                if (separator_type != TypeInfo{TypeKind::String, ""}) {
                    throw BuildLocationError(call->arguments[1]->location,
                                             "Function `join` expects `String` for argument #2");
                }

                return {TypeKind::String, ""};
            }

            if (callee->name == "file_exists") {
                if (call->arguments.size() != 1) {
                    throw BuildLocationError(callee->location, "Function `file_exists` expects 1 argument");
                }

                const TypeInfo path_type = CheckExpression(call->arguments[0].get(), scopes);
                if (path_type != TypeInfo{TypeKind::String, ""}) {
                    throw BuildLocationError(call->arguments[0]->location,
                                             "Function `file_exists` expects `String` as argument #1");
                }

                return {TypeKind::Bool, ""};
            }

            if (callee->name == "read_text") {
                if (call->arguments.size() != 1) {
                    throw BuildLocationError(callee->location, "Function `read_text` expects 1 argument");
                }

                const TypeInfo path_type = CheckExpression(call->arguments[0].get(), scopes);
                if (path_type != TypeInfo{TypeKind::String, ""}) {
                    throw BuildLocationError(call->arguments[0]->location,
                                             "Function `read_text` expects `String` as argument #1");
                }

                return {TypeKind::String, ""};
            }

            if (callee->name == "write_text" || callee->name == "append_text") {
                if (call->arguments.size() != 2) {
                    throw BuildLocationError(callee->location,
                                             "Function `" + callee->name + "` expects 2 arguments");
                }

                const TypeInfo path_type = CheckExpression(call->arguments[0].get(), scopes);
                if (path_type != TypeInfo{TypeKind::String, ""}) {
                    throw BuildLocationError(call->arguments[0]->location,
                                             "Function `" + callee->name + "` expects `String` as argument #1");
                }

                const TypeInfo text_type = CheckExpression(call->arguments[1].get(), scopes);
                if (text_type != TypeInfo{TypeKind::String, ""}) {
                    throw BuildLocationError(call->arguments[1]->location,
                                             "Function `" + callee->name + "` expects `String` as argument #2");
                }

                return {TypeKind::Unit, ""};
            }

            if (callee->name == "abs") {
                if (call->arguments.size() != 1) {
                    throw BuildLocationError(callee->location, "Function `abs` expects 1 argument");
                }

                const TypeInfo value_type = CheckExpression(call->arguments[0].get(), scopes);
                if (value_type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(call->arguments[0]->location,
                                             "Function `abs` expects `Int` as argument #1");
                }

                return {TypeKind::Int, ""};
            }

            if (callee->name == "min") {
                if (call->arguments.size() != 2) {
                    throw BuildLocationError(callee->location, "Function `min` expects 2 arguments");
                }

                const TypeInfo left_type = CheckExpression(call->arguments[0].get(), scopes);
                if (left_type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(call->arguments[0]->location,
                                             "Function `min` expects `Int` as argument #1");
                }

                const TypeInfo right_type = CheckExpression(call->arguments[1].get(), scopes);
                if (right_type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(call->arguments[1]->location,
                                             "Function `min` expects `Int` as argument #2");
                }

                return {TypeKind::Int, ""};
            }

            if (callee->name == "max") {
                if (call->arguments.size() != 2) {
                    throw BuildLocationError(callee->location, "Function `max` expects 2 arguments");
                }

                const TypeInfo left_type = CheckExpression(call->arguments[0].get(), scopes);
                if (left_type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(call->arguments[0]->location,
                                             "Function `max` expects `Int` as argument #1");
                }

                const TypeInfo right_type = CheckExpression(call->arguments[1].get(), scopes);
                if (right_type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(call->arguments[1]->location,
                                             "Function `max` expects `Int` as argument #2");
                }

                return {TypeKind::Int, ""};
            }

            if (callee->name == "pow") {
                if (call->arguments.size() != 2) {
                    throw BuildLocationError(callee->location, "Function `pow` expects 2 arguments");
                }

                const TypeInfo base_type = CheckExpression(call->arguments[0].get(), scopes);
                if (base_type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(call->arguments[0]->location,
                                             "Function `pow` expects `Int` as argument #1 (base)");
                }

                const TypeInfo exp_type = CheckExpression(call->arguments[1].get(), scopes);
                if (exp_type != TypeInfo{TypeKind::Int, ""}) {
                    throw BuildLocationError(call->arguments[1]->location,
                                             "Function `pow` expects `Int` as argument #2 (exponent)");
                }

                return {TypeKind::Int, ""};
            }

            const std::string resolved_name = ResolveFunctionName(callee->name);
            const auto signature_it = function_signatures_.find(resolved_name);
            if (signature_it == function_signatures_.end()) {
                throw BuildLocationError(callee->location, "Unknown function: " + callee->name);
            }

            const auto& signature = signature_it->second;
            if (call->arguments.size() != signature.parameter_types.size()) {
                std::ostringstream buffer;
                buffer << "Function `" << callee->name << "` expects " << signature.parameter_types.size()
                       << " arguments, but got " << call->arguments.size();
                throw BuildLocationError(callee->location, buffer.str());
            }

            for (std::size_t i = 0; i < call->arguments.size(); ++i) {
                const TypeInfo actual_type = CheckExpression(call->arguments[i].get(), scopes);
                const TypeInfo expected_type = signature.parameter_types[i];
                if (actual_type != expected_type) {
                    throw BuildLocationError(call->arguments[i]->location,
                                             "Function `" + callee->name + "` expects `" +
                                                 TypeInfoName(expected_type) + "` for argument #" +
                                                 std::to_string(i + 1) + ", but got `" + TypeInfoName(actual_type) +
                                                 "`");
                }
            }

            return signature.return_type;
        }

        if (const auto* callee = dynamic_cast<const MemberExpr*>(call->callee.get())) {
            const TypeInfo receiver_type = CheckExpression(callee->object.get(), scopes);
            const std::string resolved_name =
                ResolveMethodName(receiver_type, callee->member_name, callee->location);
            const auto& signature = function_signatures_.at(resolved_name);

            if (call->arguments.size() + 1 != signature.parameter_types.size()) {
                std::ostringstream buffer;
                buffer << "Method `" << callee->member_name << "` expects "
                       << (signature.parameter_types.size() - 1) << " arguments, but got "
                       << call->arguments.size();
                throw BuildLocationError(callee->location, buffer.str());
            }

            for (std::size_t i = 0; i < call->arguments.size(); ++i) {
                const TypeInfo actual_type = CheckExpression(call->arguments[i].get(), scopes);
                const TypeInfo expected_type = signature.parameter_types[i + 1];
                if (actual_type != expected_type) {
                    throw BuildLocationError(call->arguments[i]->location,
                                             "Method `" + callee->member_name + "` expects `" +
                                                 TypeInfoName(expected_type) + "` for argument #" +
                                                 std::to_string(i + 1) + ", but got `" + TypeInfoName(actual_type) +
                                                 "`");
                }
            }

            return signature.return_type;
        }

        throw BuildLocationError(expr->location, "Only named function calls are supported right now");
    }

    throw BuildLocationError(expr->location, "Unknown expression kind");
}

std::string TypeChecker::GetFullFunctionName(const FunctionDecl& function) {
    if (function.owner_type_name.has_value()) {
        if (function.module_name.has_value()) {
            return *function.module_name + "::" + *function.owner_type_name + "::" + function.name;
        }
        return *function.owner_type_name + "::" + function.name;
    }

    if (function.module_name.has_value()) {
        return *function.module_name + "::" + function.name;
    }
    return function.name;
}

std::string TypeChecker::GetFullStructName(const StructDecl& struct_decl) {
    if (struct_decl.module_name.has_value()) {
        return *struct_decl.module_name + "::" + struct_decl.name;
    }
    return struct_decl.name;
}

std::string TypeChecker::ResolveFunctionName(const std::string& raw_name) const {
    if (raw_name == "print" || raw_name.find("::") != std::string::npos) {
        return raw_name;
    }

    if (current_module_name_.has_value()) {
        const std::string scoped_name = *current_module_name_ + "::" + raw_name;
        if (function_signatures_.find(scoped_name) != function_signatures_.end()) {
            return scoped_name;
        }
    }

    return raw_name;
}

std::string TypeChecker::ResolveMethodName(const TypeInfo& receiver_type,
                                           const std::string& method_name,
                                           const SourceLocation& location) const {
    if (receiver_type.kind != TypeKind::Named) {
        throw BuildLocationError(location, "Method calls require a struct value");
    }

    const std::string full_name = receiver_type.name + "::" + method_name;
    if (function_signatures_.find(full_name) == function_signatures_.end()) {
        throw BuildLocationError(location,
                                 "Struct `" + receiver_type.name + "` has no method `" + method_name + "`");
    }

    return full_name;
}

TypeInfo TypeChecker::ResolveTypeName(const std::string& raw_name,
                                      const std::optional<std::string>& module_name,
                                      const SourceLocation& location) const {
    TypeInfo type = TypeInfoFromAnnotation(raw_name);
    if (type.kind == TypeKind::Slice) {
        const TypeInfo inner_type = ResolveTypeName(type.name, module_name, location);
        return {TypeKind::Slice, TypeInfoName(inner_type)};
    }

    if (type.kind != TypeKind::Named) {
        return type;
    }

    if (type.name.find("::") == std::string::npos && module_name.has_value()) {
        const std::string scoped_name = *module_name + "::" + type.name;
        if (struct_signatures_.find(scoped_name) != struct_signatures_.end()) {
            return {TypeKind::Named, scoped_name};
        }
    }

    if (struct_signatures_.find(type.name) != struct_signatures_.end()) {
        return type;
    }

    throw BuildLocationError(location, "Unknown type: " + raw_name);
}
