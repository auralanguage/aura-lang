#pragma once

#include "ast.hpp"

#include <memory>
#include <vector>

class Parser {
  public:
    explicit Parser(std::vector<Token> tokens);
    Program ParseProgram();

  private:
    std::vector<Token> tokens_;
    std::size_t current_ = 0;
    bool allow_struct_literals_ = true;

    std::string ParseModuleDeclaration();
    ImportDecl ParseImport();
    StructDecl ParseStruct();
    FunctionDecl ParseFunction();
    std::vector<std::unique_ptr<Stmt>> ParseBlockStatements(const Token& opening_brace);
    std::unique_ptr<Stmt> ParseStatement();
    std::unique_ptr<Stmt> ParseBlockStatement(const Token& opening_brace);
    std::unique_ptr<Stmt> ParseLetStatement();
    std::unique_ptr<Stmt> ParseReturnStatement();
    std::unique_ptr<Stmt> ParseIfStatement();
    std::unique_ptr<Stmt> ParseWhileStatement();
    std::unique_ptr<Stmt> ParseForStatement();
    std::unique_ptr<Stmt> ParseExpressionStatement();

    std::unique_ptr<Expr> ParseExpression();
    std::unique_ptr<Expr> ParseAssignment();
    std::unique_ptr<Expr> ParseLogicalOr();
    std::unique_ptr<Expr> ParseLogicalAnd();
    std::unique_ptr<Expr> ParseEquality();
    std::unique_ptr<Expr> ParseComparison();
    std::unique_ptr<Expr> ParseRange();
    std::unique_ptr<Expr> ParseAddition();
    std::unique_ptr<Expr> ParseMultiplication();
    std::unique_ptr<Expr> ParseUnary();
    std::unique_ptr<Expr> ParseCall();
    std::unique_ptr<Expr> ParsePrimary();
    std::string ParseQualifiedName(const Token& first_identifier);
    std::string ParseTypeName();
    std::unique_ptr<Expr> ParseArrayLiteral(const Token& opening_bracket);
    std::unique_ptr<Expr> ParseStructLiteral(const Token& type_name);
    std::unique_ptr<Expr> ParseConditionExpression();

    bool Match(TokenType type);
    bool Check(TokenType type) const;
    const Token& Advance();
    bool IsAtEnd() const;
    const Token& Peek() const;
    const Token& Previous() const;
    Token Consume(TokenType type, const std::string& message);
    static AuraError ErrorAt(const Token& token, const std::string& message);
};
