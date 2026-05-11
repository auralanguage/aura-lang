#include "ir.hpp"

#include "semantics.hpp"

#include <sstream>
#include <unordered_map>

namespace {

IrBuiltinKind GetBuiltinKind(const std::string& name) {
    if (name == "print") {
        return IrBuiltinKind::Print;
    }
    if (name == "len") {
        return IrBuiltinKind::Len;
    }
    if (name == "push") {
        return IrBuiltinKind::Push;
    }
    if (name == "pop") {
        return IrBuiltinKind::Pop;
    }
    if (name == "insert") {
        return IrBuiltinKind::Insert;
    }
    if (name == "remove_at") {
        return IrBuiltinKind::RemoveAt;
    }
    if (name == "clear") {
        return IrBuiltinKind::Clear;
    }
    if (name == "contains") {
        return IrBuiltinKind::Contains;
    }
    if (name == "starts_with") {
        return IrBuiltinKind::StartsWith;
    }
    if (name == "ends_with") {
        return IrBuiltinKind::EndsWith;
    }
    if (name == "join") {
        return IrBuiltinKind::Join;
    }
    if (name == "file_exists") {
        return IrBuiltinKind::FileExists;
    }
    if (name == "read_text") {
        return IrBuiltinKind::ReadText;
    }
    if (name == "abs") {
        return IrBuiltinKind::Abs;
    }
    if (name == "min") {
        return IrBuiltinKind::Min;
    }
    if (name == "max") {
        return IrBuiltinKind::Max;
    }
    if (name == "pow") {
        return IrBuiltinKind::Pow;
    }
    return IrBuiltinKind::None;
}

class IrBuilder {
  // release
  public:
    explicit IrBuilder(const Program& program) : program_(program) {}

    IrProgram Lower() {
        RegisterBuiltins();
        CollectStructDeclarations();
        CollectFunctionSignatures();

        IrProgram ir_program;
        ir_program.module_name = program_.module_name;
        ir_program.imports = program_.imports;

        for (const auto& struct_decl : program_.structs) {
            ir_program.structs.push_back(LowerStruct(struct_decl));
        }

        for (const auto& function : program_.functions) {
            ir_program.functions.push_back(LowerFunction(function));
        }

        for (const auto& struct_decl : program_.structs) {
            for (const auto& method : struct_decl.methods) {
                ir_program.functions.push_back(LowerFunction(method));
            }
        }

        return ir_program;
    }

  private:
    const Program& program_;
    std::unordered_map<std::string, FunctionSignature> function_signatures_;
    std::unordered_map<std::string, StructSignature> struct_signatures_;
    std::string current_function_name_;
    std::optional<std::string> current_module_name_;
    TypeInfo current_return_type_{TypeKind::Unit, ""};

    void RegisterBuiltins() {
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
    }

    void CollectStructDeclarations() {
        for (const auto& struct_decl : program_.structs) {
            struct_signatures_[GetFullStructName(struct_decl)] = StructSignature{};
        }

        for (const auto& struct_decl : program_.structs) {
            StructSignature& signature = struct_signatures_[GetFullStructName(struct_decl)];
            for (const auto& field : struct_decl.fields) {
                signature.fields[field.name] = ResolveTypeName(field.type_name, struct_decl.module_name, struct_decl.location);
            }
        }
    }

    void CollectFunctionSignatures() {
        const auto register_function = [this](const FunctionDecl& function) {
            FunctionSignature signature;
            signature.return_type =
                function.return_type.has_value() ? ResolveTypeName(*function.return_type, function.module_name, function.location)
                                                 : TypeInfo{TypeKind::Unit, ""};

            for (const auto& parameter : function.parameters) {
                signature.parameter_types.push_back(ResolveTypeName(parameter.type_name, function.module_name, function.location));
            }

            function_signatures_[GetFullFunctionName(function)] = std::move(signature);
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

    IrStructDecl LowerStruct(const StructDecl& struct_decl) {
        IrStructDecl ir_struct;
        ir_struct.location = struct_decl.location;
        ir_struct.name = struct_decl.name;
        ir_struct.module_name = struct_decl.module_name;
        ir_struct.full_name = GetFullStructName(struct_decl);

        for (const auto& field : struct_decl.fields) {
            ir_struct.fields.push_back(IrStructFieldDecl{
                field.name,
                ResolveTypeName(field.type_name, struct_decl.module_name, struct_decl.location),
            });
        }

        return ir_struct;
    }

    IrFunctionDecl LowerFunction(const FunctionDecl& function) {
        current_function_name_ = GetFullFunctionName(function);
        current_module_name_ = function.module_name;
        current_return_type_ =
            function.return_type.has_value() ? ResolveTypeName(*function.return_type, function.module_name, function.location)
                                             : TypeInfo{TypeKind::Unit, ""};

        IrFunctionDecl ir_function;
        ir_function.location = function.location;
        ir_function.name = function.name;
        ir_function.module_name = function.module_name;
        ir_function.owner_type_name =
            function.owner_type_name.has_value()
                ? std::optional<std::string>{ResolveTypeName(*function.owner_type_name, function.module_name, function.location).name}
                : std::nullopt;
        ir_function.full_name = current_function_name_;
        ir_function.return_type = current_return_type_;

        TypeScopeStack scopes;
        scopes.Push();

        const auto& parameter_types = function_signatures_.at(current_function_name_).parameter_types;
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            ir_function.parameters.push_back(IrParameter{function.parameters[i].name, parameter_types[i]});
            scopes.Declare(function.parameters[i].name, parameter_types[i], function.location);
        }

        for (const auto& statement : function.body) {
            ir_function.body.push_back(LowerStatement(statement.get(), scopes));
        }

        return ir_function;
    }

    std::unique_ptr<IrStmt> LowerStatement(const Stmt* stmt, TypeScopeStack& scopes) {
        if (const auto* block_stmt = dynamic_cast<const BlockStmt*>(stmt)) {
            TypeScopeFrame frame(scopes);
            std::vector<std::unique_ptr<IrStmt>> statements;
            for (const auto& statement : block_stmt->statements) {
                statements.push_back(LowerStatement(statement.get(), scopes));
            }
            return std::make_unique<IrBlockStmt>(block_stmt->location, std::move(statements));
        }

        if (const auto* let_stmt = dynamic_cast<const LetStmt*>(stmt)) {
            std::unique_ptr<IrExpr> initializer = LowerExpression(let_stmt->initializer.get(), scopes);
            const TypeInfo variable_type =
                let_stmt->type_name.has_value() ? ResolveTypeName(*let_stmt->type_name, current_module_name_, let_stmt->location)
                                                : initializer->type;
            scopes.Declare(let_stmt->name, variable_type, let_stmt->location);
            return std::make_unique<IrLetStmt>(let_stmt->location,
                                               let_stmt->name,
                                               variable_type,
                                               let_stmt->type_name.has_value(),
                                               std::move(initializer));
        }

        if (const auto* return_stmt = dynamic_cast<const ReturnStmt*>(stmt)) {
            std::unique_ptr<IrExpr> value;
            if (return_stmt->value) {
                value = LowerExpression(return_stmt->value.get(), scopes);
            }
            return std::make_unique<IrReturnStmt>(return_stmt->location, std::move(value));
        }

        if (const auto* if_stmt = dynamic_cast<const IfStmt*>(stmt)) {
            std::unique_ptr<IrExpr> condition = LowerExpression(if_stmt->condition.get(), scopes);
            std::unique_ptr<IrStmt> then_branch = LowerStatement(if_stmt->then_branch.get(), scopes);
            std::unique_ptr<IrStmt> else_branch =
                if_stmt->else_branch ? LowerStatement(if_stmt->else_branch.get(), scopes) : nullptr;
            return std::make_unique<IrIfStmt>(if_stmt->location,
                                              std::move(condition),
                                              std::move(then_branch),
                                              std::move(else_branch));
        }

        if (const auto* while_stmt = dynamic_cast<const WhileStmt*>(stmt)) {
            return std::make_unique<IrWhileStmt>(while_stmt->location,
                                                 LowerExpression(while_stmt->condition.get(), scopes),
                                                 LowerStatement(while_stmt->body.get(), scopes));
        }

        if (const auto* for_stmt = dynamic_cast<const ForStmt*>(stmt)) {
            std::unique_ptr<IrExpr> iterable = LowerExpression(for_stmt->iterable.get(), scopes);
            const TypeInfo element_type =
                iterable->type.kind == TypeKind::Slice ? TypeInfoFromAnnotation(iterable->type.name)
                                                       : TypeInfo{TypeKind::Char, ""};

            TypeScopeFrame frame(scopes);
            if (for_stmt->index_name.has_value()) {
                scopes.Declare(*for_stmt->index_name, {TypeKind::Int, ""}, for_stmt->location);
            }
            scopes.Declare(for_stmt->variable_name, element_type, for_stmt->location);

            return std::make_unique<IrForStmt>(for_stmt->location,
                                               for_stmt->index_name,
                                               for_stmt->variable_name,
                                               element_type,
                                               std::move(iterable),
                                               LowerStatement(for_stmt->body.get(), scopes));
        }

        if (const auto* expr_stmt = dynamic_cast<const ExprStmt*>(stmt)) {
            return std::make_unique<IrExprStmt>(expr_stmt->location, LowerExpression(expr_stmt->expression.get(), scopes));
        }

        throw BuildLocationError(stmt->location, "IR lowering failed for an unknown statement kind");
    }

    std::unique_ptr<IrExpr> LowerExpression(const Expr* expr, TypeScopeStack& scopes) {
        if (const auto* literal = dynamic_cast<const LiteralExpr*>(expr)) {
            const TypeInfo type = TypeInfoFromValue(literal->value);
            return std::make_unique<IrLiteralExpr>(literal->location, type, literal->value);
        }

        if (const auto* array_literal = dynamic_cast<const ArrayLiteralExpr*>(expr)) {
            std::vector<std::unique_ptr<IrExpr>> elements;
            for (const auto& element : array_literal->elements) {
                elements.push_back(LowerExpression(element.get(), scopes));
            }
            const TypeInfo type{TypeKind::Slice, TypeInfoName(elements.front()->type)};
            return std::make_unique<IrArrayLiteralExpr>(array_literal->location, type, std::move(elements));
        }

        if (const auto* variable = dynamic_cast<const VariableExpr*>(expr)) {
            return std::make_unique<IrVariableExpr>(variable->location,
                                                    scopes.Get(variable->name, variable->location),
                                                    variable->name);
        }

        if (const auto* assign = dynamic_cast<const AssignExpr*>(expr)) {
            std::unique_ptr<IrExpr> target = LowerExpression(assign->target.get(), scopes);
            std::unique_ptr<IrExpr> value = LowerExpression(assign->value.get(), scopes);
            return std::make_unique<IrAssignExpr>(assign->location, target->type, std::move(target), std::move(value));
        }

        if (const auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
            std::unique_ptr<IrExpr> right = LowerExpression(unary->right.get(), scopes);
            const TypeInfo type =
                unary->op.type == TokenType::Bang ? TypeInfo{TypeKind::Bool, ""} : TypeInfo{TypeKind::Int, ""};
            return std::make_unique<IrUnaryExpr>(unary->location, type, unary->op.type, std::move(right));
        }

        if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
            std::unique_ptr<IrExpr> left = LowerExpression(binary->left.get(), scopes);
            std::unique_ptr<IrExpr> right = LowerExpression(binary->right.get(), scopes);
            TypeInfo type{TypeKind::Unit, ""};

            switch (binary->op.type) {
            case TokenType::AndAnd:
            case TokenType::OrOr:
            case TokenType::EqualEqual:
            case TokenType::BangEqual:
            case TokenType::Greater:
            case TokenType::GreaterEqual:
            case TokenType::Less:
            case TokenType::LessEqual:
                type = {TypeKind::Bool, ""};
                break;
            case TokenType::Plus:
                if (left->type == TypeInfo{TypeKind::Int, ""} && right->type == TypeInfo{TypeKind::Int, ""}) {
                    type = {TypeKind::Int, ""};
                } else {
                    type = {TypeKind::String, ""};
                }
                break;
            case TokenType::Minus:
            case TokenType::Star:
            case TokenType::Slash:
                type = {TypeKind::Int, ""};
                break;
            default:
                throw BuildLocationError(binary->location, "IR lowering failed for an unknown binary operator");
            }

            return std::make_unique<IrBinaryExpr>(binary->location,
                                                  type,
                                                  std::move(left),
                                                  binary->op.type,
                                                  std::move(right));
        }

        if (const auto* range = dynamic_cast<const RangeExpr*>(expr)) {
            return std::make_unique<IrRangeExpr>(range->location,
                                                 TypeInfo{TypeKind::Slice, "Int"},
                                                 LowerExpression(range->start.get(), scopes),
                                                 LowerExpression(range->end.get(), scopes));
        }

        if (const auto* struct_literal = dynamic_cast<const StructLiteralExpr*>(expr)) {
            const TypeInfo struct_type =
                ResolveTypeName(struct_literal->type_name, current_module_name_, struct_literal->location);
            std::vector<IrStructLiteralField> fields;
            for (const auto& field : struct_literal->fields) {
                fields.push_back(IrStructLiteralField{field.name, LowerExpression(field.value.get(), scopes)});
            }
            return std::make_unique<IrStructLiteralExpr>(struct_literal->location,
                                                         struct_type,
                                                         struct_type.name,
                                                         std::move(fields));
        }

        if (const auto* member = dynamic_cast<const MemberExpr*>(expr)) {
            std::unique_ptr<IrExpr> object = LowerExpression(member->object.get(), scopes);
            const TypeInfo field_type = struct_signatures_.at(object->type.name).fields.at(member->member_name);
            return std::make_unique<IrMemberExpr>(member->location, field_type, std::move(object), member->member_name);
        }

        if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
            std::unique_ptr<IrExpr> object = LowerExpression(index->object.get(), scopes);
            std::unique_ptr<IrExpr> index_expr = LowerExpression(index->index.get(), scopes);
            const TypeInfo type =
                object->type.kind == TypeKind::Slice ? TypeInfoFromAnnotation(object->type.name) : TypeInfo{TypeKind::Char, ""};
            return std::make_unique<IrIndexExpr>(index->location, type, std::move(object), std::move(index_expr));
        }

        if (const auto* slice = dynamic_cast<const SliceExpr*>(expr)) {
            std::unique_ptr<IrExpr> object = LowerExpression(slice->object.get(), scopes);
            std::unique_ptr<IrExpr> start = slice->start ? LowerExpression(slice->start.get(), scopes) : nullptr;
            std::unique_ptr<IrExpr> end = slice->end ? LowerExpression(slice->end.get(), scopes) : nullptr;
            return std::make_unique<IrSliceExpr>(slice->location,
                                                 object->type,
                                                 std::move(object),
                                                 std::move(start),
                                                 std::move(end));
        }

        if (const auto* call = dynamic_cast<const CallExpr*>(expr)) {
            if (const auto* callee = dynamic_cast<const VariableExpr*>(call->callee.get())) {
                std::vector<std::unique_ptr<IrExpr>> arguments;
                for (const auto& argument : call->arguments) {
                    arguments.push_back(LowerExpression(argument.get(), scopes));
                }

                if (callee->name == "print") {
                    return std::make_unique<IrCallExpr>(call->location,
                                                        TypeInfo{TypeKind::Unit, ""},
                                                        IrCallKind::Builtin,
                                                        IrBuiltinKind::Print,
                                                        "print",
                                                        std::move(arguments));
                }

                if (callee->name == "len") {
                    return std::make_unique<IrCallExpr>(call->location,
                                                        TypeInfo{TypeKind::Int, ""},
                                                        IrCallKind::Builtin,
                                                        IrBuiltinKind::Len,
                                                        "len",
                                                        std::move(arguments));
                }

                if (callee->name == "push" || callee->name == "insert" || callee->name == "clear") {
                    return std::make_unique<IrCallExpr>(call->location,
                                                        TypeInfo{TypeKind::Unit, ""},
                                                        IrCallKind::Builtin,
                                                        GetBuiltinKind(callee->name),
                                                        callee->name,
                                                        std::move(arguments));
                }

                if (callee->name == "pop" || callee->name == "remove_at") {
                    return std::make_unique<IrCallExpr>(call->location,
                                                        TypeInfoFromAnnotation(arguments.front()->type.name),
                                                        IrCallKind::Builtin,
                                                        GetBuiltinKind(callee->name),
                                                        callee->name,
                                                        std::move(arguments));
                }

                if (callee->name == "contains" || callee->name == "starts_with" || callee->name == "ends_with" ||
                    callee->name == "file_exists") {
                    return std::make_unique<IrCallExpr>(call->location,
                                                        TypeInfo{TypeKind::Bool, ""},
                                                        IrCallKind::Builtin,
                                                        GetBuiltinKind(callee->name),
                                                        callee->name,
                                                        std::move(arguments));
                }

                if (callee->name == "join" || callee->name == "read_text") {
                    return std::make_unique<IrCallExpr>(call->location,
                                                        TypeInfo{TypeKind::String, ""},
                                                        IrCallKind::Builtin,
                                                        GetBuiltinKind(callee->name),
                                                        callee->name,
                                                        std::move(arguments));
                }

                const std::string resolved_name = ResolveFunctionName(callee->name);
                const TypeInfo type = function_signatures_.at(resolved_name).return_type;
                return std::make_unique<IrCallExpr>(call->location,
                                                    type,
                                                    IrCallKind::UserFunction,
                                                    IrBuiltinKind::None,
                                                    resolved_name,
                                                    std::move(arguments));
            }

            if (const auto* callee = dynamic_cast<const MemberExpr*>(call->callee.get())) {
                std::unique_ptr<IrExpr> receiver = LowerExpression(callee->object.get(), scopes);
                std::vector<std::unique_ptr<IrExpr>> arguments;
                arguments.push_back(std::move(receiver));
                for (const auto& argument : call->arguments) {
                    arguments.push_back(LowerExpression(argument.get(), scopes));
                }

                const std::string resolved_name =
                    ResolveMethodName(arguments.front()->type, callee->member_name, callee->location);
                const TypeInfo type = function_signatures_.at(resolved_name).return_type;
                return std::make_unique<IrCallExpr>(call->location,
                                                    type,
                                                    IrCallKind::UserFunction,
                                                    IrBuiltinKind::None,
                                                    resolved_name,
                                                    std::move(arguments));
            }

            throw BuildLocationError(call->location, "IR lowering only supports named function calls");
        }

        throw BuildLocationError(expr->location, "IR lowering failed for an unknown expression kind");
    }

    static std::string GetFullFunctionName(const FunctionDecl& function) {
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

    static std::string GetFullStructName(const StructDecl& struct_decl) {
        if (struct_decl.module_name.has_value()) {
            return *struct_decl.module_name + "::" + struct_decl.name;
        }
        return struct_decl.name;
    }

    std::string ResolveFunctionName(const std::string& raw_name) const {
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

    std::string ResolveMethodName(const TypeInfo& receiver_type,
                                  const std::string& method_name,
                                  const SourceLocation& location) const {
        if (receiver_type.kind != TypeKind::Named) {
            throw BuildLocationError(location, "IR lowering expected a struct receiver for a method call");
        }

        const std::string full_name = receiver_type.name + "::" + method_name;
        if (function_signatures_.find(full_name) == function_signatures_.end()) {
            throw BuildLocationError(location, "IR lowering could not resolve method `" + method_name + "`");
        }

        return full_name;
    }

    TypeInfo ResolveTypeName(const std::string& raw_name,
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
};

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

std::string FormatValueLiteral(const Value& value) {
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
        return std::string("'") + *char_value + "'";
    }
    if (const auto* string_value = std::get_if<StringValuePtr>(&value)) {
        return "\"" + EscapeString(StringToString(**string_value)) + "\"";
    }
    return ValueToString(value);
}

std::string Indent(int depth) {
    return std::string(static_cast<std::size_t>(depth) * 2, ' ');
}

std::string FormatExpr(const IrExpr* expr);
std::string FormatStmt(const IrStmt* stmt, int depth);

std::string FormatExpr(const IrExpr* expr) {
    if (const auto* literal = dynamic_cast<const IrLiteralExpr*>(expr)) {
        return FormatValueLiteral(literal->value);
    }

    if (const auto* array_literal = dynamic_cast<const IrArrayLiteralExpr*>(expr)) {
        std::ostringstream buffer;
        buffer << "[";
        for (std::size_t i = 0; i < array_literal->elements.size(); ++i) {
            if (i > 0) {
                buffer << ", ";
            }
            buffer << FormatExpr(array_literal->elements[i].get());
        }
        buffer << "]";
        return buffer.str();
    }

    if (const auto* variable = dynamic_cast<const IrVariableExpr*>(expr)) {
        return variable->name;
    }

    if (const auto* assign = dynamic_cast<const IrAssignExpr*>(expr)) {
        return FormatExpr(assign->target.get()) + " = " + FormatExpr(assign->value.get());
    }

    if (const auto* unary = dynamic_cast<const IrUnaryExpr*>(expr)) {
        return TokenTypeName(unary->op) + FormatExpr(unary->right.get());
    }

    if (const auto* binary = dynamic_cast<const IrBinaryExpr*>(expr)) {
        return "(" + FormatExpr(binary->left.get()) + " " + TokenTypeName(binary->op) + " " +
               FormatExpr(binary->right.get()) + ")";
    }

    if (const auto* range = dynamic_cast<const IrRangeExpr*>(expr)) {
        return "(" + FormatExpr(range->start.get()) + ".." + FormatExpr(range->end.get()) + ")";
    }

    if (const auto* call = dynamic_cast<const IrCallExpr*>(expr)) {
        std::ostringstream buffer;
        buffer << call->callee_name << "(";
        for (std::size_t i = 0; i < call->arguments.size(); ++i) {
            if (i > 0) {
                buffer << ", ";
            }
            buffer << FormatExpr(call->arguments[i].get());
        }
        buffer << ")";
        return buffer.str();
    }

    if (const auto* struct_literal = dynamic_cast<const IrStructLiteralExpr*>(expr)) {
        std::ostringstream buffer;
        buffer << struct_literal->type_name << " { ";
        for (std::size_t i = 0; i < struct_literal->fields.size(); ++i) {
            if (i > 0) {
                buffer << ", ";
            }
            buffer << struct_literal->fields[i].name << ": " << FormatExpr(struct_literal->fields[i].value.get());
        }
        buffer << " }";
        return buffer.str();
    }

    if (const auto* member = dynamic_cast<const IrMemberExpr*>(expr)) {
        return FormatExpr(member->object.get()) + "." + member->member_name;
    }

    if (const auto* index = dynamic_cast<const IrIndexExpr*>(expr)) {
        return FormatExpr(index->object.get()) + "[" + FormatExpr(index->index.get()) + "]";
    }

    if (const auto* slice = dynamic_cast<const IrSliceExpr*>(expr)) {
        std::ostringstream buffer;
        buffer << FormatExpr(slice->object.get()) << "[";
        if (slice->start) {
            buffer << FormatExpr(slice->start.get());
        }
        buffer << ":";
        if (slice->end) {
            buffer << FormatExpr(slice->end.get());
        }
        buffer << "]";
        return buffer.str();
    }

    return "<unknown-expr>";
}

std::string FormatStmt(const IrStmt* stmt, int depth) {
    std::ostringstream buffer;
    const std::string indent = Indent(depth);

    if (const auto* block = dynamic_cast<const IrBlockStmt*>(stmt)) {
        buffer << indent << "{\n";
        for (const auto& statement : block->statements) {
            buffer << FormatStmt(statement.get(), depth + 1);
        }
        buffer << indent << "}\n";
        return buffer.str();
    }

    if (const auto* let_stmt = dynamic_cast<const IrLetStmt*>(stmt)) {
        buffer << indent << "let " << let_stmt->name << ": " << TypeInfoName(let_stmt->variable_type);
        if (!let_stmt->has_explicit_type) {
            buffer << " /* inferred */";
        }
        buffer << " = " << FormatExpr(let_stmt->initializer.get()) << ";\n";
        return buffer.str();
    }

    if (const auto* return_stmt = dynamic_cast<const IrReturnStmt*>(stmt)) {
        buffer << indent << "return";
        if (return_stmt->value) {
            buffer << " " << FormatExpr(return_stmt->value.get());
        }
        buffer << ";\n";
        return buffer.str();
    }

    if (const auto* if_stmt = dynamic_cast<const IrIfStmt*>(stmt)) {
        buffer << indent << "if " << FormatExpr(if_stmt->condition.get()) << " ";
        buffer << FormatStmt(if_stmt->then_branch.get(), depth);
        if (if_stmt->else_branch) {
            buffer << indent << "else ";
            buffer << FormatStmt(if_stmt->else_branch.get(), depth);
        }
        return buffer.str();
    }

    if (const auto* while_stmt = dynamic_cast<const IrWhileStmt*>(stmt)) {
        buffer << indent << "while " << FormatExpr(while_stmt->condition.get()) << " ";
        buffer << FormatStmt(while_stmt->body.get(), depth);
        return buffer.str();
    }

    if (const auto* for_stmt = dynamic_cast<const IrForStmt*>(stmt)) {
        buffer << indent << "for ";
        if (for_stmt->index_name.has_value()) {
            buffer << *for_stmt->index_name << ", ";
        }
        buffer << for_stmt->variable_name << ": " << TypeInfoName(for_stmt->element_type) << " in "
               << FormatExpr(for_stmt->iterable.get()) << " ";
        buffer << FormatStmt(for_stmt->body.get(), depth);
        return buffer.str();
    }

    if (const auto* expr_stmt = dynamic_cast<const IrExprStmt*>(stmt)) {
        buffer << indent << FormatExpr(expr_stmt->expression.get()) << ";\n";
        return buffer.str();
    }

    return indent + "<unknown-stmt>\n";
}

}  // namespace

IrExpr::IrExpr(SourceLocation location, TypeInfo type) : location(std::move(location)), type(std::move(type)) {}
IrExpr::~IrExpr() = default;

IrLiteralExpr::IrLiteralExpr(SourceLocation location, TypeInfo type, Value value)
    : IrExpr(std::move(location), std::move(type)), value(std::move(value)) {}

IrArrayLiteralExpr::IrArrayLiteralExpr(SourceLocation location,
                                       TypeInfo type,
                                       std::vector<std::unique_ptr<IrExpr>> elements)
    : IrExpr(std::move(location), std::move(type)), elements(std::move(elements)) {}

IrVariableExpr::IrVariableExpr(SourceLocation location, TypeInfo type, std::string name)
    : IrExpr(std::move(location), std::move(type)), name(std::move(name)) {}

IrAssignExpr::IrAssignExpr(SourceLocation location,
                           TypeInfo type,
                           std::unique_ptr<IrExpr> target,
                           std::unique_ptr<IrExpr> value)
    : IrExpr(std::move(location), std::move(type)), target(std::move(target)), value(std::move(value)) {}

IrUnaryExpr::IrUnaryExpr(SourceLocation location, TypeInfo type, TokenType op, std::unique_ptr<IrExpr> right)
    : IrExpr(std::move(location), std::move(type)), op(op), right(std::move(right)) {}

IrBinaryExpr::IrBinaryExpr(SourceLocation location,
                           TypeInfo type,
                           std::unique_ptr<IrExpr> left,
                           TokenType op,
                           std::unique_ptr<IrExpr> right)
    : IrExpr(std::move(location), std::move(type)),
      left(std::move(left)),
      op(op),
      right(std::move(right)) {}

IrRangeExpr::IrRangeExpr(SourceLocation location,
                         TypeInfo type,
                         std::unique_ptr<IrExpr> start,
                         std::unique_ptr<IrExpr> end)
    : IrExpr(std::move(location), std::move(type)), start(std::move(start)), end(std::move(end)) {}

IrCallExpr::IrCallExpr(SourceLocation location,
                       TypeInfo type,
                       IrCallKind call_kind,
                       IrBuiltinKind builtin_kind,
                       std::string callee_name,
                       std::vector<std::unique_ptr<IrExpr>> arguments)
    : IrExpr(std::move(location), std::move(type)),
      call_kind(call_kind),
      builtin_kind(builtin_kind),
      callee_name(std::move(callee_name)),
      arguments(std::move(arguments)) {}

IrStructLiteralExpr::IrStructLiteralExpr(SourceLocation location,
                                         TypeInfo type,
                                         std::string type_name,
                                         std::vector<IrStructLiteralField> fields)
    : IrExpr(std::move(location), std::move(type)),
      type_name(std::move(type_name)),
      fields(std::move(fields)) {}

IrMemberExpr::IrMemberExpr(SourceLocation location,
                           TypeInfo type,
                           std::unique_ptr<IrExpr> object,
                           std::string member_name)
    : IrExpr(std::move(location), std::move(type)),
      object(std::move(object)),
      member_name(std::move(member_name)) {}

IrIndexExpr::IrIndexExpr(SourceLocation location,
                         TypeInfo type,
                         std::unique_ptr<IrExpr> object,
                         std::unique_ptr<IrExpr> index)
    : IrExpr(std::move(location), std::move(type)),
      object(std::move(object)),
      index(std::move(index)) {}

IrSliceExpr::IrSliceExpr(SourceLocation location,
                         TypeInfo type,
                         std::unique_ptr<IrExpr> object,
                         std::unique_ptr<IrExpr> start,
                         std::unique_ptr<IrExpr> end)
    : IrExpr(std::move(location), std::move(type)),
      object(std::move(object)),
      start(std::move(start)),
      end(std::move(end)) {}

IrStmt::IrStmt(SourceLocation location) : location(std::move(location)) {}
IrStmt::~IrStmt() = default;

IrBlockStmt::IrBlockStmt(SourceLocation location, std::vector<std::unique_ptr<IrStmt>> statements)
    : IrStmt(std::move(location)), statements(std::move(statements)) {}

IrLetStmt::IrLetStmt(SourceLocation location,
                     std::string name,
                     TypeInfo variable_type,
                     bool has_explicit_type,
                     std::unique_ptr<IrExpr> initializer)
    : IrStmt(std::move(location)),
      name(std::move(name)),
      variable_type(std::move(variable_type)),
      has_explicit_type(has_explicit_type),
      initializer(std::move(initializer)) {}

IrReturnStmt::IrReturnStmt(SourceLocation location, std::unique_ptr<IrExpr> value)
    : IrStmt(std::move(location)), value(std::move(value)) {}

IrIfStmt::IrIfStmt(SourceLocation location,
                   std::unique_ptr<IrExpr> condition,
                   std::unique_ptr<IrStmt> then_branch,
                   std::unique_ptr<IrStmt> else_branch)
    : IrStmt(std::move(location)),
      condition(std::move(condition)),
      then_branch(std::move(then_branch)),
      else_branch(std::move(else_branch)) {}

IrWhileStmt::IrWhileStmt(SourceLocation location, std::unique_ptr<IrExpr> condition, std::unique_ptr<IrStmt> body)
    : IrStmt(std::move(location)), condition(std::move(condition)), body(std::move(body)) {}

IrForStmt::IrForStmt(SourceLocation location,
                     std::optional<std::string> index_name,
                     std::string variable_name,
                     TypeInfo element_type,
                     std::unique_ptr<IrExpr> iterable,
                     std::unique_ptr<IrStmt> body)
    : IrStmt(std::move(location)),
      index_name(std::move(index_name)),
      variable_name(std::move(variable_name)),
      element_type(std::move(element_type)),
      iterable(std::move(iterable)),
      body(std::move(body)) {}

IrExprStmt::IrExprStmt(SourceLocation location, std::unique_ptr<IrExpr> expression)
    : IrStmt(std::move(location)), expression(std::move(expression)) {}

IrProgram LowerProgramToIr(const Program& program) {
    return IrBuilder(program).Lower();
}

std::string FormatIrProgram(const IrProgram& program) {
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
        buffer << "fn " << function.full_name << "(";
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i > 0) {
                buffer << ", ";
            }
            buffer << function.parameters[i].name << ": " << TypeInfoName(function.parameters[i].type);
        }
        buffer << ") -> " << TypeInfoName(function.return_type) << " {\n";
        for (const auto& statement : function.body) {
            buffer << FormatStmt(statement.get(), 1);
        }
        buffer << "}\n\n";
    }

    return buffer.str();
}
