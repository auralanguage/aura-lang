#include "interpreter.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>

namespace {

namespace fs = std::filesystem;

fs::path ResolveRuntimePath(const std::string& raw_path, const SourceLocation& location) {
    fs::path path(raw_path);
    if (path.is_absolute()) {
        return path;
    }

    const fs::path runtime_base = GetRuntimeBasePath();
    if (!runtime_base.empty()) {
        return (runtime_base / path).lexically_normal();
    }

    if (location.file_path.empty()) {
        return path;
    }

    return (fs::path(location.file_path).parent_path() / path).lexically_normal();
}

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool IsStringLikeValue(const Value& value) {
    return std::holds_alternative<StringValuePtr>(value) || std::holds_alternative<char>(value);
}

std::string StringLikeValueToString(const Value& value) {
    if (const auto* string_value = std::get_if<StringValuePtr>(&value)) {
        return StringToString(**string_value);
    }
    if (const auto* char_value = std::get_if<char>(&value)) {
        return std::string(1, *char_value);
    }

    throw AuraError("Internal error: value is not string-like");
}

bool IsStringCollectionElementType(const std::string& element_type_name) {
    return element_type_name == "String" || element_type_name == "Char";
}

bool StringLikeEquals(const Value& left, const Value& right) {
    if (IsStringLikeValue(left) && IsStringLikeValue(right)) {
        return StringLikeValueToString(left) == StringLikeValueToString(right);
    }
    return left == right;
}

std::string ReadTextFile(const fs::path& path) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input.is_open()) {
        throw AuraError("Function `read_text` could not open file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::vector<Value> ListDirectoryEntries(const fs::path& path) {
    std::error_code error_code;
    if (!fs::exists(path, error_code) || error_code) {
        throw AuraError("Function `list_dir` could not open directory: " + path.string());
    }
    if (!fs::is_directory(path, error_code) || error_code) {
        throw AuraError("Function `list_dir` expects a directory path: " + path.string());
    }

    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(path, error_code)) {
        if (error_code) {
            throw AuraError("Function `list_dir` could not iterate directory: " + path.string());
        }
        names.push_back(entry.path().filename().string());
    }

    std::sort(names.begin(), names.end());
    std::vector<Value> values;
    values.reserve(names.size());
    for (const auto& name : names) {
        values.push_back(MakeStringValue(name));
    }
    return values;
}

void WriteTextFile(const fs::path& path, const std::string& text, bool append) {
    std::error_code error_code;
    const fs::path parent = path.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, error_code);
        if (error_code) {
            throw AuraError("Function `" + std::string(append ? "append_text" : "write_text") +
                            "` could not create parent directories: " + path.string());
        }
    }

    const std::ios::openmode mode = std::ios::out | std::ios::binary | (append ? std::ios::app : std::ios::trunc);
    std::ofstream output(path, mode);
    if (!output.is_open()) {
        throw AuraError("Function `" + std::string(append ? "append_text" : "write_text") +
                        "` could not open file: " + path.string());
    }
    output << text;
}

}  // namespace

ReturnSignal::ReturnSignal(Value value) : value(std::move(value)) {}

void ScopeStack::Push() {
    scopes_.emplace_back();
}

void ScopeStack::Pop() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

void ScopeStack::Declare(const std::string& name, Value value) {
    if (scopes_.empty()) {
        Push();
    }
    scopes_.back()[name] = std::move(value);
}

Value ScopeStack::Get(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto value_it = it->find(name);
        if (value_it != it->end()) {
            return value_it->second;
        }
    }
    throw AuraError("Undefined variable: " + name);
}

void ScopeStack::Assign(const std::string& name, Value value) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto value_it = it->find(name);
        if (value_it != it->end()) {
            value_it->second = std::move(value);
            return;
        }
    }
    throw AuraError("Cannot assign to unknown variable: " + name);
}

ScopeFrame::ScopeFrame(ScopeStack& scopes) : scopes_(scopes) {
    scopes_.Push();
}

ScopeFrame::~ScopeFrame() {
    scopes_.Pop();
}

Interpreter::Interpreter(const Program& program) : program_(program) {
    for (const auto& struct_decl : program_.structs) {
        StructRuntimeInfo info;
        for (const auto& field : struct_decl.fields) {
            info.fields[field.name] = true;
        }
        structs_[GetFullStructName(struct_decl)] = std::move(info);
    }

    for (const auto& function : program_.functions) {
        functions_[GetFullFunctionName(function)] = &function;
    }

    for (const auto& struct_decl : program_.structs) {
        for (const auto& method : struct_decl.methods) {
            functions_[GetFullFunctionName(method)] = &method;
        }
    }
}

bool Interpreter::HasFunction(const std::string& name) const {
    return functions_.find(name) != functions_.end();
}

Value Interpreter::ExecuteMain() {
    return ExecuteFunction("main", {});
}

Value Interpreter::ExecuteFunction(const std::string& name, const std::vector<Value>& arguments) {
    const auto function_it = functions_.find(name);
    if (function_it == functions_.end()) {
        throw AuraError("Unknown function: " + name);
    }

    const FunctionDecl& function = *function_it->second;
    if (function.parameters.size() != arguments.size()) {
        std::ostringstream buffer;
        buffer << "Function `" << name << "` expects " << function.parameters.size() << " arguments, but got "
               << arguments.size();
        throw AuraError(buffer.str());
    }

    ScopeStack scopes;
    scopes.Push();
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        scopes.Declare(function.parameters[i].name, arguments[i]);
    }

    try {
        for (const auto& statement : function.body) {
            ExecuteStatement(statement.get(), scopes, function.module_name);
        }
    } catch (const ReturnSignal& signal) {
        return signal.value;
    }

    return std::monostate{};
}

void Interpreter::ExecuteStatement(const Stmt* stmt,
                                   ScopeStack& scopes,
                                   const std::optional<std::string>& current_module_name) {
    try {
        if (const auto* block_stmt = dynamic_cast<const BlockStmt*>(stmt)) {
            ScopeFrame frame(scopes);
            for (const auto& statement : block_stmt->statements) {
                ExecuteStatement(statement.get(), scopes, current_module_name);
            }
            return;
        }

        if (const auto* let_stmt = dynamic_cast<const LetStmt*>(stmt)) {
            scopes.Declare(let_stmt->name, Evaluate(let_stmt->initializer.get(), scopes, current_module_name));
            return;
        }

        if (const auto* return_stmt = dynamic_cast<const ReturnStmt*>(stmt)) {
            Value value = std::monostate{};
            if (return_stmt->value) {
                value = Evaluate(return_stmt->value.get(), scopes, current_module_name);
            }
            throw ReturnSignal(std::move(value));
        }

        if (const auto* if_stmt = dynamic_cast<const IfStmt*>(stmt)) {
            if (ExpectBool(Evaluate(if_stmt->condition.get(), scopes, current_module_name), "if condition")) {
                ExecuteScopedStatement(if_stmt->then_branch.get(), scopes, current_module_name);
            } else if (if_stmt->else_branch) {
                ExecuteScopedStatement(if_stmt->else_branch.get(), scopes, current_module_name);
            }
            return;
        }

        if (const auto* while_stmt = dynamic_cast<const WhileStmt*>(stmt)) {
            while (ExpectBool(Evaluate(while_stmt->condition.get(), scopes, current_module_name), "while condition")) {
                ExecuteScopedStatement(while_stmt->body.get(), scopes, current_module_name);
            }
            return;
        }

        if (const auto* for_stmt = dynamic_cast<const ForStmt*>(stmt)) {
            Value iterable = Evaluate(for_stmt->iterable.get(), scopes, current_module_name);
            if (const auto* array_value = std::get_if<ArrayValuePtr>(&iterable)) {
                if (*array_value == nullptr) {
                    throw AuraError("A for loop requires a slice value or String");
                }

                for (std::size_t i = 0; i < ArrayLength(**array_value); ++i) {
                    ScopeFrame frame(scopes);
                    if (for_stmt->index_name.has_value()) {
                        scopes.Declare(*for_stmt->index_name, static_cast<long long>(i));
                    }
                    scopes.Declare(for_stmt->variable_name, ArrayAt(**array_value, i));
                    ExecuteStatement(for_stmt->body.get(), scopes, current_module_name);
                }
                return;
            }

            if (const auto* string_value = std::get_if<StringValuePtr>(&iterable)) {
                if (*string_value == nullptr) {
                    throw AuraError("A for loop requires a slice value or String");
                }

                for (std::size_t i = 0; i < StringLength(**string_value); ++i) {
                    ScopeFrame frame(scopes);
                    if (for_stmt->index_name.has_value()) {
                        scopes.Declare(*for_stmt->index_name, static_cast<long long>(i));
                    }
                    scopes.Declare(for_stmt->variable_name, StringAt(**string_value, i));
                    ExecuteStatement(for_stmt->body.get(), scopes, current_module_name);
                }
                return;
            }

            throw AuraError("A for loop requires a slice value or String");
        }

        if (const auto* expr_stmt = dynamic_cast<const ExprStmt*>(stmt)) {
            Evaluate(expr_stmt->expression.get(), scopes, current_module_name);
            return;
        }

        throw AuraError("Unsupported statement kind");
    } catch (const AuraError& error) {
        const std::string message = error.what();
        if (!message.empty() && message.front() == '[') {
            throw;
        }
        throw BuildLocationError(stmt->location, message);
    }
}

void Interpreter::ExecuteScopedStatement(const Stmt* stmt,
                                         ScopeStack& scopes,
                                         const std::optional<std::string>& current_module_name) {
    ScopeFrame frame(scopes);
    ExecuteStatement(stmt, scopes, current_module_name);
}

Value Interpreter::Evaluate(const Expr* expr,
                            ScopeStack& scopes,
                            const std::optional<std::string>& current_module_name) {
    try {
        if (const auto* literal = dynamic_cast<const LiteralExpr*>(expr)) {
            return literal->value;
        }

        if (const auto* array_literal = dynamic_cast<const ArrayLiteralExpr*>(expr)) {
            std::vector<Value> elements;
            for (const auto& element : array_literal->elements) {
                elements.push_back(Evaluate(element.get(), scopes, current_module_name));
            }

            std::string element_type_name;
            if (!elements.empty()) {
                element_type_name = TypeInfoName(TypeInfoFromValue(elements.front()));
            }

            return MakeArrayValue(std::move(element_type_name), std::move(elements));
        }

        if (const auto* range = dynamic_cast<const RangeExpr*>(expr)) {
            const long long raw_start =
                ExpectInteger(Evaluate(range->start.get(), scopes, current_module_name), "Range start");
            const long long raw_end =
                ExpectInteger(Evaluate(range->end.get(), scopes, current_module_name), "Range end");

            std::vector<Value> elements;
            if (raw_start < raw_end) {
                elements.reserve(static_cast<std::size_t>(raw_end - raw_start));
                for (long long value = raw_start; value < raw_end; ++value) {
                    elements.push_back(value);
                }
            }

            return MakeArrayValue("Int", std::move(elements));
        }

        if (const auto* struct_literal = dynamic_cast<const StructLiteralExpr*>(expr)) {
            const std::string resolved_struct_name = ResolveStructName(struct_literal->type_name, current_module_name);
            const auto struct_it = structs_.find(resolved_struct_name);
            if (struct_it == structs_.end()) {
                throw AuraError("Unknown struct type: " + struct_literal->type_name);
            }

            auto struct_value = std::make_shared<StructValue>();
            struct_value->type_name = resolved_struct_name;

            for (const auto& field : struct_literal->fields) {
                if (struct_it->second.fields.find(field.name) == struct_it->second.fields.end()) {
                    throw AuraError("Unknown field `" + field.name + "` for `" + resolved_struct_name + "`");
                }
                struct_value->fields[field.name] = Evaluate(field.value.get(), scopes, current_module_name);
            }

            return struct_value;
        }

        if (const auto* variable = dynamic_cast<const VariableExpr*>(expr)) {
            if (variable->name.find("::") != std::string::npos) {
                throw AuraError("Qualified names can only be used in function calls");
            }
            return scopes.Get(variable->name);
        }

        if (const auto* member = dynamic_cast<const MemberExpr*>(expr)) {
            Value object = Evaluate(member->object.get(), scopes, current_module_name);
            const auto* struct_value = std::get_if<StructValuePtr>(&object);
            if (struct_value == nullptr || *struct_value == nullptr) {
                throw AuraError("Member access requires a struct value");
            }

            const auto field_it = (*struct_value)->fields.find(member->member_name);
            if (field_it == (*struct_value)->fields.end()) {
                throw AuraError("Struct `" + (*struct_value)->type_name + "` has no field `" + member->member_name + "`");
            }

            return field_it->second;
        }

        if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
            Value object = Evaluate(index->object.get(), scopes, current_module_name);
            const auto* array_value = std::get_if<ArrayValuePtr>(&object);
            if (array_value != nullptr && *array_value != nullptr) {
                const long long raw_index =
                    ExpectInteger(Evaluate(index->index.get(), scopes, current_module_name), "Slice index");
                if (raw_index < 0) {
                    throw AuraError("Slice index out of bounds");
                }
                return ArrayAt(**array_value, static_cast<std::size_t>(raw_index));
            }

            if (const auto* string_value = std::get_if<StringValuePtr>(&object)) {
                const long long raw_index =
                    ExpectInteger(Evaluate(index->index.get(), scopes, current_module_name), "String index");
                if (raw_index < 0) {
                    throw AuraError("String index out of bounds");
                }
                return StringAt(**string_value, static_cast<std::size_t>(raw_index));
            }

            throw AuraError("Index access requires a slice value or String");
        }

        if (const auto* slice = dynamic_cast<const SliceExpr*>(expr)) {
            Value object = Evaluate(slice->object.get(), scopes, current_module_name);

            if (const auto* array_value = std::get_if<ArrayValuePtr>(&object)) {
                if (*array_value == nullptr) {
                    throw AuraError("Slice access requires a slice value or String");
                }

                const std::size_t size = ArrayLength(**array_value);
                long long raw_start = 0;
                long long raw_end = static_cast<long long>(size);

                if (slice->start) {
                    raw_start = ExpectInteger(Evaluate(slice->start.get(), scopes, current_module_name), "Slice start index");
                }
                if (slice->end) {
                    raw_end = ExpectInteger(Evaluate(slice->end.get(), scopes, current_module_name), "Slice end index");
                }

                if (raw_start < 0 || raw_end < 0) {
                    throw AuraError("Slice indices must not be negative");
                }
                if (raw_start > raw_end) {
                    throw AuraError("Slice start must be less than or equal to slice end");
                }
                if (static_cast<std::size_t>(raw_end) > size) {
                    throw AuraError("Slice range out of bounds");
                }

                return MakeArraySlice(*array_value,
                                      static_cast<std::size_t>(raw_start),
                                      static_cast<std::size_t>(raw_end));
            }

            if (const auto* string_value = std::get_if<StringValuePtr>(&object)) {
                if (*string_value == nullptr) {
                    throw AuraError("Slice access requires a slice value or String");
                }

                const std::size_t size = StringLength(**string_value);
                long long raw_start = 0;
                long long raw_end = static_cast<long long>(size);

                if (slice->start) {
                    raw_start = ExpectInteger(Evaluate(slice->start.get(), scopes, current_module_name), "String slice start index");
                }
                if (slice->end) {
                    raw_end = ExpectInteger(Evaluate(slice->end.get(), scopes, current_module_name), "String slice end index");
                }

                if (raw_start < 0 || raw_end < 0) {
                    throw AuraError("String indices must not be negative");
                }
                if (raw_start > raw_end) {
                    throw AuraError("String start must be less than or equal to string end");
                }
                if (static_cast<std::size_t>(raw_end) > size) {
                    throw AuraError("String range out of bounds");
                }

                return MakeStringSlice(*string_value,
                                       static_cast<std::size_t>(raw_start),
                                       static_cast<std::size_t>(raw_end));
            }

            throw AuraError("Slice access requires a slice value or String");
        }

        if (const auto* assign = dynamic_cast<const AssignExpr*>(expr)) {
            Value value = Evaluate(assign->value.get(), scopes, current_module_name);

            if (const auto* variable = dynamic_cast<const VariableExpr*>(assign->target.get())) {
                scopes.Assign(variable->name, value);
                return value;
            }

            if (const auto* member = dynamic_cast<const MemberExpr*>(assign->target.get())) {
                Value object = Evaluate(member->object.get(), scopes, current_module_name);
                const auto* struct_value = std::get_if<StructValuePtr>(&object);
                if (struct_value == nullptr || *struct_value == nullptr) {
                    throw AuraError("Field assignment requires a struct value");
                }

                const auto field_it = (*struct_value)->fields.find(member->member_name);
                if (field_it == (*struct_value)->fields.end()) {
                    throw AuraError("Struct `" + (*struct_value)->type_name + "` has no field `" + member->member_name + "`");
                }

                field_it->second = value;
                return value;
            }

            if (const auto* index = dynamic_cast<const IndexExpr*>(assign->target.get())) {
                Value object = Evaluate(index->object.get(), scopes, current_module_name);
                if (const auto* array_value = std::get_if<ArrayValuePtr>(&object)) {
                    if (*array_value == nullptr) {
                        throw AuraError("Index assignment requires a slice value");
                    }

                    const long long raw_index =
                        ExpectInteger(Evaluate(index->index.get(), scopes, current_module_name), "Slice index");
                    if (raw_index < 0) {
                        throw AuraError("Slice index out of bounds");
                    }

                    ArrayAt(**array_value, static_cast<std::size_t>(raw_index)) = value;
                    return value;
                }

                if (std::holds_alternative<StringValuePtr>(object)) {
                    throw AuraError("String index assignment is not supported");
                }

                throw AuraError("Index assignment requires a slice value");
            }

            throw AuraError("Invalid assignment target");
        }

        if (const auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
            Value right = Evaluate(unary->right.get(), scopes, current_module_name);
            switch (unary->op.type) {
            case TokenType::Bang:
                return !ExpectBool(right, "Unary `!`");
            case TokenType::Minus:
                return -ExpectInteger(right, "Unary `-`");
            default:
                break;
            }
        }

        if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
            Value left = Evaluate(binary->left.get(), scopes, current_module_name);

            if (binary->op.type == TokenType::AndAnd) {
                const bool left_bool = ExpectBool(left, "`&&`");
                if (!left_bool) {
                    return false;
                }
                Value right = Evaluate(binary->right.get(), scopes, current_module_name);
                return ExpectBool(right, "`&&`");
            }

            if (binary->op.type == TokenType::OrOr) {
                const bool left_bool = ExpectBool(left, "`||`");
                if (left_bool) {
                    return true;
                }
                Value right = Evaluate(binary->right.get(), scopes, current_module_name);
                return ExpectBool(right, "`||`");
            }

            Value right = Evaluate(binary->right.get(), scopes, current_module_name);
            return EvaluateBinary(binary->op, left, right);
        }

        if (const auto* call = dynamic_cast<const CallExpr*>(expr)) {
            if (const auto* callee = dynamic_cast<const VariableExpr*>(call->callee.get())) {
                std::vector<Value> arguments;
                for (const auto& argument : call->arguments) {
                    arguments.push_back(Evaluate(argument.get(), scopes, current_module_name));
                }

                if (callee->name == "print") {
                    for (std::size_t i = 0; i < arguments.size(); ++i) {
                        if (i > 0) {
                            std::cout << ' ';
                        }
                        std::cout << ValueToString(arguments[i]);
                    }
                    std::cout << '\n';
                    return std::monostate{};
                }

                if (callee->name == "len") {
                    if (arguments.size() != 1) {
                        throw AuraError("Function `len` expects 1 argument");
                    }

                    if (const auto* string_value = std::get_if<StringValuePtr>(&arguments[0])) {
                        return static_cast<long long>(StringLength(**string_value));
                    }

                    const auto* array_value = std::get_if<ArrayValuePtr>(&arguments[0]);
                    if (array_value == nullptr || *array_value == nullptr) {
                        throw AuraError("Function `len` expects a slice or String");
                    }

                    return static_cast<long long>(ArrayLength(**array_value));
                }

                if (callee->name == "push") {
                    if (arguments.size() != 2) {
                        throw AuraError("Function `push` expects 2 arguments");
                    }

                    const auto* array_value = std::get_if<ArrayValuePtr>(&arguments[0]);
                    if (array_value == nullptr || *array_value == nullptr) {
                        throw AuraError("Function `push` expects a slice as argument #1");
                    }

                    if (!(*array_value)->element_type_name.empty()) {
                        const std::string actual_type_name = TypeInfoName(TypeInfoFromValue(arguments[1]));
                        if (actual_type_name != (*array_value)->element_type_name) {
                            throw AuraError("Function `push` received a value with the wrong element type");
                        }
                    }

                    ArrayPush(**array_value, arguments[1]);
                    return std::monostate{};
                }

                if (callee->name == "pop") {
                    if (arguments.size() != 1) {
                        throw AuraError("Function `pop` expects 1 argument");
                    }

                    const auto* array_value = std::get_if<ArrayValuePtr>(&arguments[0]);
                    if (array_value == nullptr || *array_value == nullptr) {
                        throw AuraError("Function `pop` expects a slice");
                    }

                    return ArrayPop(**array_value);
                }

                if (callee->name == "insert") {
                    if (arguments.size() != 3) {
                        throw AuraError("Function `insert` expects 3 arguments");
                    }

                    const auto* array_value = std::get_if<ArrayValuePtr>(&arguments[0]);
                    if (array_value == nullptr || *array_value == nullptr) {
                        throw AuraError("Function `insert` expects a slice as argument #1");
                    }

                    const long long raw_index = ExpectInteger(arguments[1], "Function `insert` argument #2");
                    if (raw_index < 0) {
                        throw AuraError("Function `insert` index is out of bounds");
                    }

                    if (!(*array_value)->element_type_name.empty()) {
                        const std::string actual_type_name = TypeInfoName(TypeInfoFromValue(arguments[2]));
                        if (actual_type_name != (*array_value)->element_type_name) {
                            throw AuraError("Function `insert` received a value with the wrong element type");
                        }
                    }

                    ArrayInsert(**array_value, static_cast<std::size_t>(raw_index), arguments[2]);
                    return std::monostate{};
                }

                if (callee->name == "remove_at") {
                    if (arguments.size() != 2) {
                        throw AuraError("Function `remove_at` expects 2 arguments");
                    }

                    const auto* array_value = std::get_if<ArrayValuePtr>(&arguments[0]);
                    if (array_value == nullptr || *array_value == nullptr) {
                        throw AuraError("Function `remove_at` expects a slice as argument #1");
                    }

                    const long long raw_index = ExpectInteger(arguments[1], "Function `remove_at` argument #2");
                    if (raw_index < 0) {
                        throw AuraError("Function `remove_at` index is out of bounds");
                    }

                    return ArrayRemoveAt(**array_value, static_cast<std::size_t>(raw_index));
                }

                if (callee->name == "clear") {
                    if (arguments.size() != 1) {
                        throw AuraError("Function `clear` expects 1 argument");
                    }

                    const auto* array_value = std::get_if<ArrayValuePtr>(&arguments[0]);
                    if (array_value == nullptr || *array_value == nullptr) {
                        throw AuraError("Function `clear` expects a slice");
                    }

                    ArrayClear(**array_value);
                    return std::monostate{};
                }

                if (callee->name == "contains") {
                    if (arguments.size() != 2) {
                        throw AuraError("Function `contains` expects 2 arguments");
                    }

                    if (const auto* string_value = std::get_if<StringValuePtr>(&arguments[0])) {
                        if (!IsStringLikeValue(arguments[1])) {
                            throw AuraError(
                                "Function `contains` expects `String` or `Char` for argument #2 when argument #1 is `String`");
                        }
                        return StringToString(**string_value).find(StringLikeValueToString(arguments[1])) != std::string::npos;
                    }

                    const auto* array_value = std::get_if<ArrayValuePtr>(&arguments[0]);
                    if (array_value != nullptr && *array_value != nullptr &&
                        (*array_value)->element_type_name == "String") {
                        const auto* needle = std::get_if<StringValuePtr>(&arguments[1]);
                        if (needle == nullptr) {
                            throw AuraError(
                                "Function `contains` expects `String` for argument #2 when argument #1 is `[String]`");
                        }

                        for (std::size_t i = 0; i < ArrayLength(**array_value); ++i) {
                            const Value& element = ArrayAt(**array_value, i);
                            const auto* element_string = std::get_if<StringValuePtr>(&element);
                            if (element_string == nullptr) {
                                throw AuraError("Function `contains` expected `[String]` storage to contain only String values");
                            }
                            if (StringToString(**element_string) == StringToString(**needle)) {
                                return true;
                            }
                        }
                        return false;
                    }

                    if (array_value != nullptr && *array_value != nullptr &&
                        (*array_value)->element_type_name == "Char") {
                        const auto* needle = std::get_if<char>(&arguments[1]);
                        if (needle == nullptr) {
                            throw AuraError(
                                "Function `contains` expects `Char` for argument #2 when argument #1 is `[Char]`");
                        }

                        for (std::size_t i = 0; i < ArrayLength(**array_value); ++i) {
                            const Value& element = ArrayAt(**array_value, i);
                            const auto* element_char = std::get_if<char>(&element);
                            if (element_char == nullptr) {
                                throw AuraError("Function `contains` expected `[Char]` storage to contain only Char values");
                            }
                            if (*element_char == *needle) {
                                return true;
                            }
                        }
                        return false;
                    }

                    throw AuraError("Function `contains` expects `String`, `[String]`, or `[Char]` as argument #1");
                }

                if (callee->name == "starts_with") {
                    if (arguments.size() != 2) {
                        throw AuraError("Function `starts_with` expects 2 arguments");
                    }

                    const auto* value = std::get_if<StringValuePtr>(&arguments[0]);
                    if (value == nullptr) {
                        throw AuraError("Function `starts_with` expects `String` as argument #1");
                    }

                    if (!IsStringLikeValue(arguments[1])) {
                        throw AuraError("Function `starts_with` expects `String` or `Char` for argument #2");
                    }

                    return StartsWith(StringToString(**value), StringLikeValueToString(arguments[1]));
                }

                if (callee->name == "ends_with") {
                    if (arguments.size() != 2) {
                        throw AuraError("Function `ends_with` expects 2 arguments");
                    }

                    const auto* value = std::get_if<StringValuePtr>(&arguments[0]);
                    if (value == nullptr) {
                        throw AuraError("Function `ends_with` expects `String` as argument #1");
                    }

                    if (!IsStringLikeValue(arguments[1])) {
                        throw AuraError("Function `ends_with` expects `String` or `Char` for argument #2");
                    }

                    return EndsWith(StringToString(**value), StringLikeValueToString(arguments[1]));
                }

                if (callee->name == "join") {
                    if (arguments.size() != 2) {
                        throw AuraError("Function `join` expects 2 arguments");
                    }

                    const auto* array_value = std::get_if<ArrayValuePtr>(&arguments[0]);
                    if (array_value == nullptr || *array_value == nullptr ||
                        !IsStringCollectionElementType((*array_value)->element_type_name)) {
                        throw AuraError("Function `join` expects `[String]` or `[Char]` as argument #1");
                    }

                    const auto* separator = std::get_if<StringValuePtr>(&arguments[1]);
                    if (separator == nullptr) {
                        throw AuraError("Function `join` expects `String` for argument #2");
                    }

                    std::ostringstream buffer;
                    for (std::size_t i = 0; i < ArrayLength(**array_value); ++i) {
                        if (i > 0) {
                            buffer << StringToString(**separator);
                        }

                        const Value& element = ArrayAt(**array_value, i);
                        if ((*array_value)->element_type_name == "String") {
                            const auto* element_string = std::get_if<StringValuePtr>(&element);
                            if (element_string == nullptr) {
                                throw AuraError("Function `join` expected `[String]` storage to contain only String values");
                            }
                            buffer << StringToString(**element_string);
                            continue;
                        }

                        const auto* element_char = std::get_if<char>(&element);
                        if (element_char == nullptr) {
                            throw AuraError("Function `join` expected `[Char]` storage to contain only Char values");
                        }
                        buffer << *element_char;
                    }

                    return MakeStringValue(buffer.str());
                }

                if (callee->name == "file_exists") {
                    if (arguments.size() != 1) {
                        throw AuraError("Function `file_exists` expects 1 argument");
                    }

                    const auto* path_value = std::get_if<StringValuePtr>(&arguments[0]);
                    if (path_value == nullptr) {
                        throw AuraError("Function `file_exists` expects `String` as argument #1");
                    }

                    const fs::path resolved_path = ResolveRuntimePath(StringToString(**path_value), call->location);
                    std::error_code error_code;
                    return fs::exists(resolved_path, error_code) && !error_code;
                }

                if (callee->name == "read_text") {
                    if (arguments.size() != 1) {
                        throw AuraError("Function `read_text` expects 1 argument");
                    }

                    const auto* path_value = std::get_if<StringValuePtr>(&arguments[0]);
                    if (path_value == nullptr) {
                        throw AuraError("Function `read_text` expects `String` as argument #1");
                    }

                    return MakeStringValue(ReadTextFile(ResolveRuntimePath(StringToString(**path_value), call->location)));
                }

                if (callee->name == "write_text" || callee->name == "append_text") {
                    if (arguments.size() != 2) {
                        throw AuraError("Function `" + callee->name + "` expects 2 arguments");
                    }

                    const auto* path_value = std::get_if<StringValuePtr>(&arguments[0]);
                    if (path_value == nullptr) {
                        throw AuraError("Function `" + callee->name + "` expects `String` as argument #1");
                    }

                    const auto* text_value = std::get_if<StringValuePtr>(&arguments[1]);
                    if (text_value == nullptr) {
                        throw AuraError("Function `" + callee->name + "` expects `String` as argument #2");
                    }

                    WriteTextFile(ResolveRuntimePath(StringToString(**path_value), call->location),
                                  StringToString(**text_value),
                                  callee->name == "append_text");
                    return std::monostate{};
                }

                if (callee->name == "remove_file") {
                    if (arguments.size() != 1) {
                        throw AuraError("Function `remove_file` expects 1 argument");
                    }

                    const auto* path_value = std::get_if<StringValuePtr>(&arguments[0]);
                    if (path_value == nullptr) {
                        throw AuraError("Function `remove_file` expects `String` as argument #1");
                    }

                    std::error_code error_code;
                    const bool removed =
                        fs::remove(ResolveRuntimePath(StringToString(**path_value), call->location), error_code);
                    if (error_code) {
                        throw AuraError("Function `remove_file` could not remove path");
                    }
                    return removed;
                }

                if (callee->name == "create_dir") {
                    if (arguments.size() != 1) {
                        throw AuraError("Function `create_dir` expects 1 argument");
                    }

                    const auto* path_value = std::get_if<StringValuePtr>(&arguments[0]);
                    if (path_value == nullptr) {
                        throw AuraError("Function `create_dir` expects `String` as argument #1");
                    }

                    std::error_code error_code;
                    const fs::path resolved_path = ResolveRuntimePath(StringToString(**path_value), call->location);
                    const bool created = fs::create_directories(resolved_path, error_code);
                    if (error_code) {
                        throw AuraError("Function `create_dir` could not create directory: " + resolved_path.string());
                    }
                    return created;
                }

                if (callee->name == "list_dir") {
                    if (arguments.size() != 1) {
                        throw AuraError("Function `list_dir` expects 1 argument");
                    }

                    const auto* path_value = std::get_if<StringValuePtr>(&arguments[0]);
                    if (path_value == nullptr) {
                        throw AuraError("Function `list_dir` expects `String` as argument #1");
                    }

                    return MakeArrayValue("String",
                                          ListDirectoryEntries(
                                              ResolveRuntimePath(StringToString(**path_value), call->location)));
                }

                if (callee->name == "abs") {
                    if (arguments.size() != 1) {
                        throw AuraError("Function `abs` expects 1 argument");
                    }

                    const long long value = ExpectInteger(arguments[0], "Function `abs` argument #1");
                    return value < 0 ? -value : value;
                }

                if (callee->name == "min") {
                    if (arguments.size() != 2) {
                        throw AuraError("Function `min` expects 2 arguments");
                    }

                    const long long left = ExpectInteger(arguments[0], "Function `min` argument #1");
                    const long long right = ExpectInteger(arguments[1], "Function `min` argument #2");
                    return left < right ? left : right;
                }

                if (callee->name == "max") {
                    if (arguments.size() != 2) {
                        throw AuraError("Function `max` expects 2 arguments");
                    }

                    const long long left = ExpectInteger(arguments[0], "Function `max` argument #1");
                    const long long right = ExpectInteger(arguments[1], "Function `max` argument #2");
                    return left > right ? left : right;
                }

                if (callee->name == "pow") {
                    if (arguments.size() != 2) {
                        throw AuraError("Function `pow` expects 2 arguments");
                    }

                    const long long base = ExpectInteger(arguments[0], "Function `pow` argument #1 (base)");
                    const long long exponent = ExpectInteger(arguments[1], "Function `pow` argument #2 (exponent)");
                    
                    if (exponent < 0) {
                        throw AuraError("Function `pow` does not support negative exponents");
                    }

                    long long result = 1;
                    for (long long i = 0; i < exponent; ++i) {
                        result *= base;
                    }
                    return result;
                }

                return ExecuteFunction(ResolveFunctionName(callee->name, current_module_name), arguments);
            }

            if (const auto* callee = dynamic_cast<const MemberExpr*>(call->callee.get())) {
                Value receiver = Evaluate(callee->object.get(), scopes, current_module_name);
                const auto* struct_value = std::get_if<StructValuePtr>(&receiver);
                if (struct_value == nullptr || *struct_value == nullptr) {
                    throw AuraError("Method calls require a struct value");
                }

                std::vector<Value> arguments;
                arguments.push_back(receiver);
                for (const auto& argument : call->arguments) {
                    arguments.push_back(Evaluate(argument.get(), scopes, current_module_name));
                }

                return ExecuteFunction(ResolveMethodName((*struct_value)->type_name, callee->member_name), arguments);
            }

            throw AuraError("Only named function calls are supported right now");
        }

        throw AuraError("Unsupported expression kind");
    } catch (const AuraError& error) {
        const std::string message = error.what();
        if (!message.empty() && message.front() == '[') {
            throw;
        }
        throw BuildLocationError(expr->location, message);
    }
}

bool Interpreter::ExpectBool(const Value& value, const std::string& context) {
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean;
    }
    throw AuraError(context + " must be Bool");
}

long long Interpreter::ExpectInteger(const Value& value, const std::string& context) {
    if (const auto* integer = std::get_if<long long>(&value)) {
        return *integer;
    }
    throw AuraError(context + " must be Int");
}

Value Interpreter::EvaluateBinary(const Token& op, const Value& left, const Value& right) {
    switch (op.type) {
    case TokenType::EqualEqual:
        return StringLikeEquals(left, right);
    case TokenType::BangEqual:
        return !StringLikeEquals(left, right);
    case TokenType::AndAnd:
    case TokenType::OrOr:
        throw AuraError("Logical operators must be evaluated through short-circuit handling");
    case TokenType::Plus: {
        if (IsStringLikeValue(left) && IsStringLikeValue(right)) {
            if (std::holds_alternative<char>(left) && std::holds_alternative<char>(right)) {
                throw AuraError("`+` only accepts `Int + Int`, `String + String`, `String + Char`, or `Char + String`");
            }
            return MakeStringValue(StringLikeValueToString(left) + StringLikeValueToString(right));
        }
        return ExpectInteger(left, "`+`") + ExpectInteger(right, "`+`");
    }
    case TokenType::Minus:
        return ExpectInteger(left, "`-`") - ExpectInteger(right, "`-`");
    case TokenType::Star:
        return ExpectInteger(left, "`*`") * ExpectInteger(right, "`*`");
    case TokenType::Slash: {
        const long long divisor = ExpectInteger(right, "`/`");
        if (divisor == 0) {
            throw AuraError("Division by zero");
        }
        return ExpectInteger(left, "`/`") / divisor;
    }
    case TokenType::Greater:
        return ExpectInteger(left, "`>`") > ExpectInteger(right, "`>`");
    case TokenType::GreaterEqual:
        return ExpectInteger(left, "`>=`") >= ExpectInteger(right, "`>=`");
    case TokenType::Less:
        return ExpectInteger(left, "`<`") < ExpectInteger(right, "`<`");
    case TokenType::LessEqual:
        return ExpectInteger(left, "`<=`") <= ExpectInteger(right, "`<=`");
    default:
        break;
    }

    throw AuraError("Unsupported binary operator");
}

std::string Interpreter::GetFullFunctionName(const FunctionDecl& function) {
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

std::string Interpreter::GetFullStructName(const StructDecl& struct_decl) {
    if (struct_decl.module_name.has_value()) {
        return *struct_decl.module_name + "::" + struct_decl.name;
    }
    return struct_decl.name;
}

std::string Interpreter::ResolveFunctionName(const std::string& raw_name,
                                             const std::optional<std::string>& current_module_name) const {
    if (raw_name == "print" || raw_name.find("::") != std::string::npos) {
        return raw_name;
    }

    if (current_module_name.has_value()) {
        const std::string scoped_name = *current_module_name + "::" + raw_name;
        if (functions_.find(scoped_name) != functions_.end()) {
            return scoped_name;
        }
    }

    return raw_name;
}

std::string Interpreter::ResolveMethodName(const std::string& struct_name, const std::string& method_name) const {
    const std::string full_name = struct_name + "::" + method_name;
    if (functions_.find(full_name) == functions_.end()) {
        throw AuraError("Struct `" + struct_name + "` has no method `" + method_name + "`");
    }
    return full_name;
}

std::string Interpreter::ResolveStructName(const std::string& raw_name,
                                           const std::optional<std::string>& current_module_name) const {
    if (raw_name.find("::") != std::string::npos) {
        return raw_name;
    }

    if (current_module_name.has_value()) {
        const std::string scoped_name = *current_module_name + "::" + raw_name;
        if (structs_.find(scoped_name) != structs_.end()) {
            return scoped_name;
        }
    }

    return raw_name;
}
