#include "ast.hpp"

#include <iostream>
#include <utility>

Expr::Expr(SourceLocation location) : location(std::move(location)) {}
Expr::~Expr() = default;

LiteralExpr::LiteralExpr(SourceLocation location, Value value)
    : Expr(std::move(location)), value(std::move(value)) {}

StructLiteralExpr::StructLiteralExpr(SourceLocation location, std::string type_name, std::vector<StructLiteralField> fields)
    : Expr(std::move(location)), type_name(std::move(type_name)), fields(std::move(fields)) {}

ArrayLiteralExpr::ArrayLiteralExpr(SourceLocation location, std::vector<std::unique_ptr<Expr>> elements)
    : Expr(std::move(location)), elements(std::move(elements)) {}

VariableExpr::VariableExpr(SourceLocation location, std::string name)
    : Expr(std::move(location)), name(std::move(name)) {}

AssignExpr::AssignExpr(SourceLocation location, std::unique_ptr<Expr> target, std::unique_ptr<Expr> value)
    : Expr(std::move(location)), target(std::move(target)), value(std::move(value)) {}

UnaryExpr::UnaryExpr(SourceLocation location, Token op, std::unique_ptr<Expr> right)
    : Expr(std::move(location)), op(std::move(op)), right(std::move(right)) {}

BinaryExpr::BinaryExpr(SourceLocation location, std::unique_ptr<Expr> left, Token op, std::unique_ptr<Expr> right)
    : Expr(std::move(location)), left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

RangeExpr::RangeExpr(SourceLocation location, std::unique_ptr<Expr> start, std::unique_ptr<Expr> end)
    : Expr(std::move(location)), start(std::move(start)), end(std::move(end)) {}

CallExpr::CallExpr(SourceLocation location, std::unique_ptr<Expr> callee, std::vector<std::unique_ptr<Expr>> arguments)
    : Expr(std::move(location)), callee(std::move(callee)), arguments(std::move(arguments)) {}

MemberExpr::MemberExpr(SourceLocation location, std::unique_ptr<Expr> object, std::string member_name)
    : Expr(std::move(location)), object(std::move(object)), member_name(std::move(member_name)) {}

IndexExpr::IndexExpr(SourceLocation location, std::unique_ptr<Expr> object, std::unique_ptr<Expr> index)
    : Expr(std::move(location)), object(std::move(object)), index(std::move(index)) {}

SliceExpr::SliceExpr(SourceLocation location,
                     std::unique_ptr<Expr> object,
                     std::unique_ptr<Expr> start,
                     std::unique_ptr<Expr> end)
    : Expr(std::move(location)),
      object(std::move(object)),
      start(std::move(start)),
      end(std::move(end)) {}

Stmt::Stmt(SourceLocation location) : location(std::move(location)) {}
Stmt::~Stmt() = default;

BlockStmt::BlockStmt(SourceLocation location, std::vector<std::unique_ptr<Stmt>> statements)
    : Stmt(std::move(location)), statements(std::move(statements)) {}

LetStmt::LetStmt(SourceLocation location,
                 std::string name,
                 std::optional<std::string> type_name,
                 std::unique_ptr<Expr> initializer)
    : Stmt(std::move(location)),
      name(std::move(name)),
      type_name(std::move(type_name)),
      initializer(std::move(initializer)) {}

ReturnStmt::ReturnStmt(SourceLocation location, std::unique_ptr<Expr> value)
    : Stmt(std::move(location)), value(std::move(value)) {}

IfStmt::IfStmt(SourceLocation location,
               std::unique_ptr<Expr> condition,
               std::unique_ptr<Stmt> then_branch,
               std::unique_ptr<Stmt> else_branch)
    : Stmt(std::move(location)),
      condition(std::move(condition)),
      then_branch(std::move(then_branch)),
      else_branch(std::move(else_branch)) {}

WhileStmt::WhileStmt(SourceLocation location, std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> body)
    : Stmt(std::move(location)), condition(std::move(condition)), body(std::move(body)) {}

ForStmt::ForStmt(SourceLocation location,
                 std::optional<std::string> index_name,
                 std::string variable_name,
                 std::unique_ptr<Expr> iterable,
                 std::unique_ptr<Stmt> body)
    : Stmt(std::move(location)),
      index_name(std::move(index_name)),
      variable_name(std::move(variable_name)),
      iterable(std::move(iterable)),
      body(std::move(body)) {}

ExprStmt::ExprStmt(SourceLocation location, std::unique_ptr<Expr> expression)
    : Stmt(std::move(location)), expression(std::move(expression)) {}

namespace {

std::string Indent(int depth) {
    return std::string(depth * 2, ' ');
}

void PrintExpr(const Expr* expr, int depth);

void PrintStmt(const Stmt* stmt, int depth) {
    if (const auto* block_stmt = dynamic_cast<const BlockStmt*>(stmt)) {
        std::cout << Indent(depth) << "Block\n";
        for (const auto& statement : block_stmt->statements) {
            PrintStmt(statement.get(), depth + 1);
        }
        return;
    }

    if (const auto* let_stmt = dynamic_cast<const LetStmt*>(stmt)) {
        std::cout << Indent(depth) << "Let " << let_stmt->name;
        if (let_stmt->type_name.has_value()) {
            std::cout << ": " << *let_stmt->type_name;
        }
        std::cout << '\n';
        PrintExpr(let_stmt->initializer.get(), depth + 1);
        return;
    }

    if (const auto* return_stmt = dynamic_cast<const ReturnStmt*>(stmt)) {
        std::cout << Indent(depth) << "Return\n";
        if (return_stmt->value) {
            PrintExpr(return_stmt->value.get(), depth + 1);
        }
        return;
    }

    if (const auto* if_stmt = dynamic_cast<const IfStmt*>(stmt)) {
        std::cout << Indent(depth) << "If\n";
        std::cout << Indent(depth + 1) << "Condition\n";
        PrintExpr(if_stmt->condition.get(), depth + 2);
        std::cout << Indent(depth + 1) << "Then\n";
        PrintStmt(if_stmt->then_branch.get(), depth + 2);
        if (if_stmt->else_branch) {
            std::cout << Indent(depth + 1) << "Else\n";
            PrintStmt(if_stmt->else_branch.get(), depth + 2);
        }
        return;
    }

    if (const auto* while_stmt = dynamic_cast<const WhileStmt*>(stmt)) {
        std::cout << Indent(depth) << "While\n";
        std::cout << Indent(depth + 1) << "Condition\n";
        PrintExpr(while_stmt->condition.get(), depth + 2);
        std::cout << Indent(depth + 1) << "Body\n";
        PrintStmt(while_stmt->body.get(), depth + 2);
        return;
    }

    if (const auto* for_stmt = dynamic_cast<const ForStmt*>(stmt)) {
        std::cout << Indent(depth) << "For ";
        if (for_stmt->index_name.has_value()) {
            std::cout << *for_stmt->index_name << ", ";
        }
        std::cout << for_stmt->variable_name << '\n';
        std::cout << Indent(depth + 1) << "Iterable\n";
        PrintExpr(for_stmt->iterable.get(), depth + 2);
        std::cout << Indent(depth + 1) << "Body\n";
        PrintStmt(for_stmt->body.get(), depth + 2);
        return;
    }

    if (const auto* expr_stmt = dynamic_cast<const ExprStmt*>(stmt)) {
        std::cout << Indent(depth) << "ExprStmt\n";
        PrintExpr(expr_stmt->expression.get(), depth + 1);
    }
}

void PrintExpr(const Expr* expr, int depth) {
    if (const auto* literal = dynamic_cast<const LiteralExpr*>(expr)) {
        std::cout << Indent(depth) << "Literal " << ValueToString(literal->value) << '\n';
        return;
    }

    if (const auto* struct_literal = dynamic_cast<const StructLiteralExpr*>(expr)) {
        std::cout << Indent(depth) << "StructLiteral " << struct_literal->type_name << '\n';
        for (const auto& field : struct_literal->fields) {
            std::cout << Indent(depth + 1) << "Field " << field.name << '\n';
            PrintExpr(field.value.get(), depth + 2);
        }
        return;
    }

    if (const auto* array_literal = dynamic_cast<const ArrayLiteralExpr*>(expr)) {
        std::cout << Indent(depth) << "ArrayLiteral\n";
        for (const auto& element : array_literal->elements) {
            PrintExpr(element.get(), depth + 1);
        }
        return;
    }

    if (const auto* variable = dynamic_cast<const VariableExpr*>(expr)) {
        std::cout << Indent(depth) << "Variable " << variable->name << '\n';
        return;
    }

    if (const auto* assign = dynamic_cast<const AssignExpr*>(expr)) {
        std::cout << Indent(depth) << "Assign\n";
        PrintExpr(assign->target.get(), depth + 1);
        PrintExpr(assign->value.get(), depth + 1);
        return;
    }

    if (const auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        std::cout << Indent(depth) << "Unary " << unary->op.lexeme << '\n';
        PrintExpr(unary->right.get(), depth + 1);
        return;
    }

    if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        std::cout << Indent(depth) << "Binary " << binary->op.lexeme << '\n';
        PrintExpr(binary->left.get(), depth + 1);
        PrintExpr(binary->right.get(), depth + 1);
        return;
    }

    if (const auto* range = dynamic_cast<const RangeExpr*>(expr)) {
        std::cout << Indent(depth) << "Range\n";
        PrintExpr(range->start.get(), depth + 1);
        PrintExpr(range->end.get(), depth + 1);
        return;
    }

    if (const auto* call = dynamic_cast<const CallExpr*>(expr)) {
        std::cout << Indent(depth) << "Call\n";
        PrintExpr(call->callee.get(), depth + 1);
        for (const auto& argument : call->arguments) {
            PrintExpr(argument.get(), depth + 1);
        }
        return;
    }

    if (const auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        std::cout << Indent(depth) << "Member " << member->member_name << '\n';
        PrintExpr(member->object.get(), depth + 1);
        return;
    }

    if (const auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        std::cout << Indent(depth) << "Index\n";
        PrintExpr(index->object.get(), depth + 1);
        PrintExpr(index->index.get(), depth + 1);
        return;
    }

    if (const auto* slice = dynamic_cast<const SliceExpr*>(expr)) {
        std::cout << Indent(depth) << "Slice\n";
        PrintExpr(slice->object.get(), depth + 1);
        if (slice->start) {
            PrintExpr(slice->start.get(), depth + 1);
        } else {
            std::cout << Indent(depth + 1) << "Start <default>\n";
        }
        if (slice->end) {
            PrintExpr(slice->end.get(), depth + 1);
        } else {
            std::cout << Indent(depth + 1) << "End <default>\n";
        }
    }
}

}  // namespace

void PrintProgram(const Program& program) {
    std::cout << "=== Aura AST ===\n";
    if (program.module_name.has_value()) {
        std::cout << "Module " << *program.module_name << '\n';
    }
    for (const auto& import_decl : program.imports) {
        std::cout << "Import \"" << import_decl.path << "\"\n";
    }
    for (const auto& struct_decl : program.structs) {
        std::cout << "Struct ";
        if (struct_decl.module_name.has_value()) {
            std::cout << *struct_decl.module_name << "::";
        }
        std::cout << struct_decl.name << '\n';
        for (const auto& field : struct_decl.fields) {
            std::cout << "  Field " << field.name << ": " << field.type_name << '\n';
        }
        for (const auto& method : struct_decl.methods) {
            std::cout << "  Method " << struct_decl.name << "::" << method.name << '(';
            for (std::size_t i = 0; i < method.parameters.size(); ++i) {
                const auto& parameter = method.parameters[i];
                std::cout << parameter.name << ": " << parameter.type_name;
                if (i + 1 < method.parameters.size()) {
                    std::cout << ", ";
                }
            }
            std::cout << ')';
            if (method.return_type.has_value()) {
                std::cout << " -> " << *method.return_type;
            }
            std::cout << '\n';

            for (const auto& statement : method.body) {
                PrintStmt(statement.get(), 2);
            }
        }
    }
    for (const auto& function : program.functions) {
        std::cout << "Function ";
        if (function.module_name.has_value()) {
            std::cout << *function.module_name << "::";
        }
        std::cout << function.name << '(';
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            const auto& parameter = function.parameters[i];
            std::cout << parameter.name << ": " << parameter.type_name;
            if (i + 1 < function.parameters.size()) {
                std::cout << ", ";
            }
        }
        std::cout << ')';
        if (function.return_type.has_value()) {
            std::cout << " -> " << *function.return_type;
        }
        std::cout << '\n';

        for (const auto& statement : function.body) {
            PrintStmt(statement.get(), 1);
        }
    }
}
