#pragma once

#include "ast.hpp"
#include "common.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

struct IrExpr {
    IrExpr(SourceLocation location, TypeInfo type);
    virtual ~IrExpr();
    SourceLocation location;
    TypeInfo type;
};

enum class IrCallKind {
    UserFunction,
    Builtin
};

enum class IrBuiltinKind {
    None,
    Print,
    Len,
    Push,
    Pop,
    Insert,
    RemoveAt,
    Clear,
    Contains,
    StartsWith,
    EndsWith,
    Join,
    FileExists,
    ReadText,
    WriteText,
    AppendText,
    Abs,
    Min,
    Max,
    Pow
};

struct IrLiteralExpr : IrExpr {
    IrLiteralExpr(SourceLocation location, TypeInfo type, Value value);
    Value value;
};

struct IrStructLiteralField {
    std::string name;
    std::unique_ptr<IrExpr> value;
};

struct IrArrayLiteralExpr : IrExpr {
    IrArrayLiteralExpr(SourceLocation location, TypeInfo type, std::vector<std::unique_ptr<IrExpr>> elements);
    std::vector<std::unique_ptr<IrExpr>> elements;
};

struct IrVariableExpr : IrExpr {
    IrVariableExpr(SourceLocation location, TypeInfo type, std::string name);
    std::string name;
};

struct IrAssignExpr : IrExpr {
    IrAssignExpr(SourceLocation location,
                 TypeInfo type,
                 std::unique_ptr<IrExpr> target,
                 std::unique_ptr<IrExpr> value);
    std::unique_ptr<IrExpr> target;
    std::unique_ptr<IrExpr> value;
};

struct IrUnaryExpr : IrExpr {
    IrUnaryExpr(SourceLocation location, TypeInfo type, TokenType op, std::unique_ptr<IrExpr> right);
    TokenType op;
    std::unique_ptr<IrExpr> right;
};

struct IrBinaryExpr : IrExpr {
    IrBinaryExpr(SourceLocation location,
                 TypeInfo type,
                 std::unique_ptr<IrExpr> left,
                 TokenType op,
                 std::unique_ptr<IrExpr> right);
    std::unique_ptr<IrExpr> left;
    TokenType op;
    std::unique_ptr<IrExpr> right;
};

struct IrRangeExpr : IrExpr {
    IrRangeExpr(SourceLocation location,
                TypeInfo type,
                std::unique_ptr<IrExpr> start,
                std::unique_ptr<IrExpr> end);
    std::unique_ptr<IrExpr> start;
    std::unique_ptr<IrExpr> end;
};

struct IrCallExpr : IrExpr {
    IrCallExpr(SourceLocation location,
               TypeInfo type,
               IrCallKind call_kind,
               IrBuiltinKind builtin_kind,
               std::string callee_name,
               std::vector<std::unique_ptr<IrExpr>> arguments);
    IrCallKind call_kind = IrCallKind::UserFunction;
    IrBuiltinKind builtin_kind = IrBuiltinKind::None;
    std::string callee_name;
    std::vector<std::unique_ptr<IrExpr>> arguments;
};

struct IrStructLiteralExpr : IrExpr {
    IrStructLiteralExpr(SourceLocation location,
                        TypeInfo type,
                        std::string type_name,
                        std::vector<IrStructLiteralField> fields);
    std::string type_name;
    std::vector<IrStructLiteralField> fields;
};

struct IrMemberExpr : IrExpr {
    IrMemberExpr(SourceLocation location,
                 TypeInfo type,
                 std::unique_ptr<IrExpr> object,
                 std::string member_name);
    std::unique_ptr<IrExpr> object;
    std::string member_name;
};

struct IrIndexExpr : IrExpr {
    IrIndexExpr(SourceLocation location,
                TypeInfo type,
                std::unique_ptr<IrExpr> object,
                std::unique_ptr<IrExpr> index);
    std::unique_ptr<IrExpr> object;
    std::unique_ptr<IrExpr> index;
};

struct IrSliceExpr : IrExpr {
    IrSliceExpr(SourceLocation location,
                TypeInfo type,
                std::unique_ptr<IrExpr> object,
                std::unique_ptr<IrExpr> start,
                std::unique_ptr<IrExpr> end);
    std::unique_ptr<IrExpr> object;
    std::unique_ptr<IrExpr> start;
    std::unique_ptr<IrExpr> end;
};

struct IrStmt {
    explicit IrStmt(SourceLocation location);
    virtual ~IrStmt();
    SourceLocation location;
};

struct IrBlockStmt : IrStmt {
    IrBlockStmt(SourceLocation location, std::vector<std::unique_ptr<IrStmt>> statements);
    std::vector<std::unique_ptr<IrStmt>> statements;
};

struct IrLetStmt : IrStmt {
    IrLetStmt(SourceLocation location,
              std::string name,
              TypeInfo variable_type,
              bool has_explicit_type,
              std::unique_ptr<IrExpr> initializer);
    std::string name;
    TypeInfo variable_type;
    bool has_explicit_type = false;
    std::unique_ptr<IrExpr> initializer;
};

struct IrReturnStmt : IrStmt {
    IrReturnStmt(SourceLocation location, std::unique_ptr<IrExpr> value);
    std::unique_ptr<IrExpr> value;
};

struct IrIfStmt : IrStmt {
    IrIfStmt(SourceLocation location,
             std::unique_ptr<IrExpr> condition,
             std::unique_ptr<IrStmt> then_branch,
             std::unique_ptr<IrStmt> else_branch);
    std::unique_ptr<IrExpr> condition;
    std::unique_ptr<IrStmt> then_branch;
    std::unique_ptr<IrStmt> else_branch;
};

struct IrWhileStmt : IrStmt {
    IrWhileStmt(SourceLocation location, std::unique_ptr<IrExpr> condition, std::unique_ptr<IrStmt> body);
    std::unique_ptr<IrExpr> condition;
    std::unique_ptr<IrStmt> body;
};

struct IrForStmt : IrStmt {
    IrForStmt(SourceLocation location,
              std::optional<std::string> index_name,
              std::string variable_name,
              TypeInfo element_type,
              std::unique_ptr<IrExpr> iterable,
              std::unique_ptr<IrStmt> body);
    std::optional<std::string> index_name;
    std::string variable_name;
    TypeInfo element_type;
    std::unique_ptr<IrExpr> iterable;
    std::unique_ptr<IrStmt> body;
};

struct IrExprStmt : IrStmt {
    IrExprStmt(SourceLocation location, std::unique_ptr<IrExpr> expression);
    std::unique_ptr<IrExpr> expression;
};

struct IrParameter {
    std::string name;
    TypeInfo type;
};

struct IrStructFieldDecl {
    std::string name;
    TypeInfo type;
};

struct IrFunctionDecl {
    SourceLocation location;
    std::string name;
    std::optional<std::string> module_name;
    std::optional<std::string> owner_type_name;
    std::string full_name;
    std::vector<IrParameter> parameters;
    TypeInfo return_type{TypeKind::Unit, ""};
    std::vector<std::unique_ptr<IrStmt>> body;
};

struct IrStructDecl {
    SourceLocation location;
    std::string name;
    std::optional<std::string> module_name;
    std::string full_name;
    std::vector<IrStructFieldDecl> fields;
};

struct IrProgram {
    std::optional<std::string> module_name;
    std::vector<ImportDecl> imports;
    std::vector<IrStructDecl> structs;
    std::vector<IrFunctionDecl> functions;
};

IrProgram LowerProgramToIr(const Program& program);
std::string FormatIrProgram(const IrProgram& program);
