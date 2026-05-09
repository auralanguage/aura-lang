#pragma once

#include "common.hpp"

#include <memory>
#include <optional>
#include <vector>

struct Expr {
    explicit Expr(SourceLocation location);
    virtual ~Expr();
    SourceLocation location;
};

struct LiteralExpr : Expr {
    LiteralExpr(SourceLocation location, Value value);
    Value value;
};

struct StructLiteralField {
    std::string name;
    std::unique_ptr<Expr> value;
};

struct ArrayLiteralExpr : Expr {
    ArrayLiteralExpr(SourceLocation location, std::vector<std::unique_ptr<Expr>> elements);
    std::vector<std::unique_ptr<Expr>> elements;
};

struct VariableExpr : Expr {
    VariableExpr(SourceLocation location, std::string name);
    std::string name;
};

struct AssignExpr : Expr {
    AssignExpr(SourceLocation location, std::unique_ptr<Expr> target, std::unique_ptr<Expr> value);
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;
};

struct UnaryExpr : Expr {
    UnaryExpr(SourceLocation location, Token op, std::unique_ptr<Expr> right);
    Token op;
    std::unique_ptr<Expr> right;
};

struct BinaryExpr : Expr {
    BinaryExpr(SourceLocation location, std::unique_ptr<Expr> left, Token op, std::unique_ptr<Expr> right);
    std::unique_ptr<Expr> left;
    Token op;
    std::unique_ptr<Expr> right;
};

struct RangeExpr : Expr {
    RangeExpr(SourceLocation location, std::unique_ptr<Expr> start, std::unique_ptr<Expr> end);
    std::unique_ptr<Expr> start;
    std::unique_ptr<Expr> end;
};

struct CallExpr : Expr {
    CallExpr(SourceLocation location, std::unique_ptr<Expr> callee, std::vector<std::unique_ptr<Expr>> arguments);
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> arguments;
};

struct StructLiteralExpr : Expr {
    StructLiteralExpr(SourceLocation location, std::string type_name, std::vector<StructLiteralField> fields);
    std::string type_name;
    std::vector<StructLiteralField> fields;
};

struct MemberExpr : Expr {
    MemberExpr(SourceLocation location, std::unique_ptr<Expr> object, std::string member_name);
    std::unique_ptr<Expr> object;
    std::string member_name;
};

struct IndexExpr : Expr {
    IndexExpr(SourceLocation location, std::unique_ptr<Expr> object, std::unique_ptr<Expr> index);
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;
};

struct SliceExpr : Expr {
    SliceExpr(SourceLocation location,
              std::unique_ptr<Expr> object,
              std::unique_ptr<Expr> start,
              std::unique_ptr<Expr> end);
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> start;
    std::unique_ptr<Expr> end;
};

struct Stmt {
    explicit Stmt(SourceLocation location);
    virtual ~Stmt();
    SourceLocation location;
};

struct BlockStmt : Stmt {
    BlockStmt(SourceLocation location, std::vector<std::unique_ptr<Stmt>> statements);
    std::vector<std::unique_ptr<Stmt>> statements;
};

struct LetStmt : Stmt {
    LetStmt(SourceLocation location,
            std::string name,
            std::optional<std::string> type_name,
            std::unique_ptr<Expr> initializer);
    std::string name;
    std::optional<std::string> type_name;
    std::unique_ptr<Expr> initializer;
};

struct ReturnStmt : Stmt {
    ReturnStmt(SourceLocation location, std::unique_ptr<Expr> value);
    std::unique_ptr<Expr> value;
};

struct IfStmt : Stmt {
    IfStmt(SourceLocation location,
           std::unique_ptr<Expr> condition,
           std::unique_ptr<Stmt> then_branch,
           std::unique_ptr<Stmt> else_branch);
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> then_branch;
    std::unique_ptr<Stmt> else_branch;
};

struct WhileStmt : Stmt {
    WhileStmt(SourceLocation location, std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> body);
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;
};

struct ForStmt : Stmt {
    ForStmt(SourceLocation location,
            std::optional<std::string> index_name,
            std::string variable_name,
            std::unique_ptr<Expr> iterable,
            std::unique_ptr<Stmt> body);
    std::optional<std::string> index_name;
    std::string variable_name;
    std::unique_ptr<Expr> iterable;
    std::unique_ptr<Stmt> body;
};

struct ExprStmt : Stmt {
    ExprStmt(SourceLocation location, std::unique_ptr<Expr> expression);
    std::unique_ptr<Expr> expression;
};

struct Parameter {
    std::string name;
    std::string type_name;
};

struct ImportDecl {
    SourceLocation location;
    std::string path;
};

struct StructFieldDecl {
    std::string name;
    std::string type_name;
};

struct FunctionDecl {
    SourceLocation location;
    std::string name;
    std::optional<std::string> module_name;
    std::optional<std::string> owner_type_name;
    std::vector<Parameter> parameters;
    std::optional<std::string> return_type;
    std::vector<std::unique_ptr<Stmt>> body;
};

struct StructDecl {
    SourceLocation location;
    std::string name;
    std::optional<std::string> module_name;
    std::vector<StructFieldDecl> fields;
    std::vector<FunctionDecl> methods;
};

struct Program {
    std::optional<std::string> module_name;
    std::vector<ImportDecl> imports;
    std::vector<StructDecl> structs;
    std::vector<FunctionDecl> functions;
};

void PrintProgram(const Program& program);
