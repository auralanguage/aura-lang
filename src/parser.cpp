#include "parser.hpp"

#include <sstream>
#include <utility>

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

Program Parser::ParseProgram() {
    Program program;
    if (Match(TokenType::Module)) {
        program.module_name = ParseModuleDeclaration();
    }

    while (!IsAtEnd()) {
        if (Match(TokenType::Import)) {
            program.imports.push_back(ParseImport());
        } else if (Match(TokenType::Struct)) {
            StructDecl struct_decl = ParseStruct();
            struct_decl.module_name = program.module_name;
            for (auto& method : struct_decl.methods) {
                method.module_name = program.module_name;
            }
            program.structs.push_back(std::move(struct_decl));
        } else {
            FunctionDecl function = ParseFunction();
            function.module_name = program.module_name;
            program.functions.push_back(std::move(function));
        }
    }
    return program;
}

std::string Parser::ParseModuleDeclaration() {
    const Token name = Consume(TokenType::Identifier, "Expected module name");
    Consume(TokenType::Semicolon, "Expected `;` after module declaration");
    return name.lexeme;
}

ImportDecl Parser::ParseImport() {
    const Token keyword = Previous();
    const Token path = Consume(TokenType::String, "Expected import path string");
    Consume(TokenType::Semicolon, "Expected `;` after import");
    return ImportDecl{keyword.location, path.lexeme};
}

StructDecl Parser::ParseStruct() {
    const Token keyword = Previous();
    const Token name = Consume(TokenType::Identifier, "Expected struct name");
    Consume(TokenType::LeftBrace, "Expected `{`");

    std::vector<StructFieldDecl> fields;
    std::vector<FunctionDecl> methods;
    while (!Check(TokenType::RightBrace) && !IsAtEnd()) {
        if (Check(TokenType::Fn)) {
            FunctionDecl method = ParseFunction();
            method.owner_type_name = name.lexeme;
            methods.push_back(std::move(method));
            continue;
        }

        const Token field_name = Consume(TokenType::Identifier, "Expected field name");
        Consume(TokenType::Colon, "Expected `:`");
        const std::string type_name = ParseTypeName();
        fields.push_back({field_name.lexeme, type_name});

        Match(TokenType::Comma);
    }

    Consume(TokenType::RightBrace, "Expected `}`");
    return StructDecl{keyword.location, name.lexeme, std::nullopt, std::move(fields), std::move(methods)};
}

FunctionDecl Parser::ParseFunction() {
    const Token fn_token = Consume(TokenType::Fn, "Expected `fn`");
    const Token name = Consume(TokenType::Identifier, "Expected function name");
    Consume(TokenType::LeftParen, "Expected `(`");

    std::vector<Parameter> parameters;
    if (!Check(TokenType::RightParen)) {
        do {
            const Token param_name = Consume(TokenType::Identifier, "Expected parameter name");
            Consume(TokenType::Colon, "Expected `:`");
            parameters.push_back({param_name.lexeme, ParseTypeName()});
        } while (Match(TokenType::Comma));
    }

    Consume(TokenType::RightParen, "Expected `)`");

    std::optional<std::string> return_type;
    if (Match(TokenType::Arrow)) {
        return_type = ParseTypeName();
    }

    const Token brace = Consume(TokenType::LeftBrace, "Expected `{`");
    std::vector<std::unique_ptr<Stmt>> body = ParseBlockStatements(brace);

    return FunctionDecl{fn_token.location,
                        name.lexeme,
                        std::nullopt,
                        std::nullopt,
                        std::move(parameters),
                        std::move(return_type),
                        std::move(body)};
}

std::vector<std::unique_ptr<Stmt>> Parser::ParseBlockStatements(const Token& opening_brace) {
    std::vector<std::unique_ptr<Stmt>> statements;
    while (!Check(TokenType::RightBrace) && !IsAtEnd()) {
        statements.push_back(ParseStatement());
    }

    if (IsAtEnd()) {
        throw ErrorAt(opening_brace, "Unterminated block");
    }

    Consume(TokenType::RightBrace, "Expected `}`");
    return statements;
}

std::unique_ptr<Stmt> Parser::ParseStatement() {
    if (Match(TokenType::LeftBrace)) {
        return ParseBlockStatement(Previous());
    }
    if (Match(TokenType::Let)) {
        return ParseLetStatement();
    }
    if (Match(TokenType::Return)) {
        return ParseReturnStatement();
    }
    if (Match(TokenType::If)) {
        return ParseIfStatement();
    }
    if (Match(TokenType::While)) {
        return ParseWhileStatement();
    }
    if (Match(TokenType::For)) {
        return ParseForStatement();
    }
    return ParseExpressionStatement();
}

std::unique_ptr<Stmt> Parser::ParseBlockStatement(const Token& opening_brace) {
    return std::make_unique<BlockStmt>(opening_brace.location, ParseBlockStatements(opening_brace));
}

std::unique_ptr<Stmt> Parser::ParseLetStatement() {
    const Token name = Consume(TokenType::Identifier, "Expected variable name");
    std::optional<std::string> type_name;
    if (Match(TokenType::Colon)) {
        type_name = ParseTypeName();
    }
    Consume(TokenType::Equal, "Expected `=`");
    auto initializer = ParseExpression();
    Consume(TokenType::Semicolon, "Expected `;`");
    return std::make_unique<LetStmt>(name.location, name.lexeme, std::move(type_name), std::move(initializer));
}

std::unique_ptr<Stmt> Parser::ParseReturnStatement() {
    const Token keyword = Previous();
    std::unique_ptr<Expr> value;
    if (!Check(TokenType::Semicolon)) {
        value = ParseExpression();
    }
    Consume(TokenType::Semicolon, "Expected `;`");
    return std::make_unique<ReturnStmt>(keyword.location, std::move(value));
}

std::unique_ptr<Stmt> Parser::ParseIfStatement() {
    const Token keyword = Previous();
    auto condition = ParseConditionExpression();
    auto then_branch = ParseStatement();

    std::unique_ptr<Stmt> else_branch;
    if (Match(TokenType::Else)) {
        if (Match(TokenType::If)) {
            else_branch = ParseIfStatement();
        } else {
            else_branch = ParseStatement();
        }
    }

    return std::make_unique<IfStmt>(keyword.location, std::move(condition), std::move(then_branch), std::move(else_branch));
}

std::unique_ptr<Stmt> Parser::ParseWhileStatement() {
    const Token keyword = Previous();
    auto condition = ParseConditionExpression();
    auto body = ParseStatement();
    return std::make_unique<WhileStmt>(keyword.location, std::move(condition), std::move(body));
}

std::unique_ptr<Stmt> Parser::ParseForStatement() {
    const Token keyword = Previous();
    const Token first_name = Consume(TokenType::Identifier, "Expected loop variable name");
    std::optional<std::string> index_name;
    std::string variable_name = first_name.lexeme;
    if (Match(TokenType::Comma)) {
        index_name = first_name.lexeme;
        variable_name = Consume(TokenType::Identifier, "Expected value variable name after `,`").lexeme;
    }
    Consume(TokenType::In, "Expected `in` after loop variable");
    auto iterable = ParseConditionExpression();
    auto body = ParseStatement();
    return std::make_unique<ForStmt>(keyword.location,
                                     std::move(index_name),
                                     std::move(variable_name),
                                     std::move(iterable),
                                     std::move(body));
}

std::unique_ptr<Stmt> Parser::ParseExpressionStatement() {
    auto expression = ParseExpression();
    const SourceLocation location = expression->location;
    Consume(TokenType::Semicolon, "Expected `;`");
    return std::make_unique<ExprStmt>(location, std::move(expression));
}

std::unique_ptr<Expr> Parser::ParseExpression() {
    return ParseAssignment();
}

std::unique_ptr<Expr> Parser::ParseAssignment() {
    auto expression = ParseLogicalOr();

    if (Match(TokenType::Equal)) {
        const Token equals = Previous();
        auto value = ParseAssignment();
        if (dynamic_cast<const VariableExpr*>(expression.get()) != nullptr ||
            dynamic_cast<const MemberExpr*>(expression.get()) != nullptr ||
            dynamic_cast<const IndexExpr*>(expression.get()) != nullptr) {
            return std::make_unique<AssignExpr>(equals.location, std::move(expression), std::move(value));
        }
        throw ErrorAt(equals, "Invalid assignment target");
    }

    return expression;
}

std::unique_ptr<Expr> Parser::ParseLogicalOr() {
    auto expression = ParseLogicalAnd();
    while (Match(TokenType::OrOr)) {
        const Token op = Previous();
        auto right = ParseLogicalAnd();
        expression = std::make_unique<BinaryExpr>(op.location, std::move(expression), op, std::move(right));
    }
    return expression;
}

std::unique_ptr<Expr> Parser::ParseLogicalAnd() {
    auto expression = ParseEquality();
    while (Match(TokenType::AndAnd)) {
        const Token op = Previous();
        auto right = ParseEquality();
        expression = std::make_unique<BinaryExpr>(op.location, std::move(expression), op, std::move(right));
    }
    return expression;
}

std::unique_ptr<Expr> Parser::ParseEquality() {
    auto expression = ParseComparison();
    while (Match(TokenType::EqualEqual) || Match(TokenType::BangEqual)) {
        const Token op = Previous();
        auto right = ParseComparison();
        expression = std::make_unique<BinaryExpr>(op.location, std::move(expression), op, std::move(right));
    }
    return expression;
}

std::unique_ptr<Expr> Parser::ParseComparison() {
    auto expression = ParseRange();
    while (Match(TokenType::Greater) || Match(TokenType::GreaterEqual) || Match(TokenType::Less) ||
           Match(TokenType::LessEqual)) {
        const Token op = Previous();
        auto right = ParseRange();
        expression = std::make_unique<BinaryExpr>(op.location, std::move(expression), op, std::move(right));
    }
    return expression;
}

std::unique_ptr<Expr> Parser::ParseRange() {
    auto expression = ParseAddition();
    while (Match(TokenType::DotDot)) {
        const Token op = Previous();
        auto right = ParseAddition();
        expression = std::make_unique<RangeExpr>(op.location, std::move(expression), std::move(right));
    }
    return expression;
}

std::unique_ptr<Expr> Parser::ParseAddition() {
    auto expression = ParseMultiplication();
    while (Match(TokenType::Plus) || Match(TokenType::Minus)) {
        const Token op = Previous();
        auto right = ParseMultiplication();
        expression = std::make_unique<BinaryExpr>(op.location, std::move(expression), op, std::move(right));
    }
    return expression;
}

std::unique_ptr<Expr> Parser::ParseMultiplication() {
    auto expression = ParseUnary();
    while (Match(TokenType::Star) || Match(TokenType::Slash)) {
        const Token op = Previous();
        auto right = ParseUnary();
        expression = std::make_unique<BinaryExpr>(op.location, std::move(expression), op, std::move(right));
    }
    return expression;
}

std::unique_ptr<Expr> Parser::ParseUnary() {
    if (Match(TokenType::Bang) || Match(TokenType::Minus)) {
        const Token op = Previous();
        auto right = ParseUnary();
        return std::make_unique<UnaryExpr>(op.location, op, std::move(right));
    }
    return ParseCall();
}

std::unique_ptr<Expr> Parser::ParseCall() {
    auto expression = ParsePrimary();

    while (true) {
        if (Match(TokenType::LeftParen)) {
            std::vector<std::unique_ptr<Expr>> arguments;
            if (!Check(TokenType::RightParen)) {
                do {
                    arguments.push_back(ParseExpression());
                } while (Match(TokenType::Comma));
            }

            const Token closing = Consume(TokenType::RightParen, "Expected `)`");
            expression = std::make_unique<CallExpr>(closing.location, std::move(expression), std::move(arguments));
            continue;
        }

        if (Match(TokenType::Dot)) {
            const Token member = Consume(TokenType::Identifier, "Expected member name after `.`");
            expression = std::make_unique<MemberExpr>(member.location, std::move(expression), member.lexeme);
            continue;
        }

        if (Match(TokenType::LeftBracket)) {
            const Token opening = Previous();
            std::unique_ptr<Expr> start;
            std::unique_ptr<Expr> end;

            if (Match(TokenType::Colon)) {
                if (!Check(TokenType::RightBracket)) {
                    end = ParseExpression();
                }
                Consume(TokenType::RightBracket, "Expected `]`");
                expression = std::make_unique<SliceExpr>(opening.location,
                                                         std::move(expression),
                                                         std::move(start),
                                                         std::move(end));
                continue;
            }

            start = ParseExpression();
            if (Match(TokenType::Colon)) {
                if (!Check(TokenType::RightBracket)) {
                    end = ParseExpression();
                }
                Consume(TokenType::RightBracket, "Expected `]`");
                expression = std::make_unique<SliceExpr>(opening.location,
                                                         std::move(expression),
                                                         std::move(start),
                                                         std::move(end));
                continue;
            }

            Consume(TokenType::RightBracket, "Expected `]`");
            expression = std::make_unique<IndexExpr>(opening.location, std::move(expression), std::move(start));
            continue;
        }

        break;
    }

    return expression;
}

std::unique_ptr<Expr> Parser::ParsePrimary() {
    if (Match(TokenType::Integer)) {
        const Token token = Previous();
        return std::make_unique<LiteralExpr>(token.location, std::stoll(token.lexeme));
    }

    if (Match(TokenType::Char)) {
        const Token token = Previous();
        return std::make_unique<LiteralExpr>(token.location, Value{token.lexeme[0]});
    }

    if (Match(TokenType::String)) {
        const Token token = Previous();
        return std::make_unique<LiteralExpr>(token.location, MakeStringValue(token.lexeme));
    }

    if (Match(TokenType::True)) {
        return std::make_unique<LiteralExpr>(Previous().location, true);
    }

    if (Match(TokenType::False)) {
        return std::make_unique<LiteralExpr>(Previous().location, false);
    }

    if (Match(TokenType::Identifier)) {
        const Token token = Previous();
        const std::string qualified_name = ParseQualifiedName(token);
        if (allow_struct_literals_ && Check(TokenType::LeftBrace)) {
            return ParseStructLiteral(Token{TokenType::Identifier, qualified_name, token.location});
        }
        return std::make_unique<VariableExpr>(token.location, qualified_name);
    }

    if (Match(TokenType::LeftBracket)) {
        return ParseArrayLiteral(Previous());
    }

    if (Match(TokenType::LeftParen)) {
        auto expression = ParseExpression();
        Consume(TokenType::RightParen, "Expected `)`");
        return expression;
    }

    throw ErrorAt(Peek(), "Expected expression");
}

std::string Parser::ParseQualifiedName(const Token& first_identifier) {
    std::string qualified_name = first_identifier.lexeme;

    while (Match(TokenType::DoubleColon)) {
        qualified_name += "::";
        qualified_name += Consume(TokenType::Identifier, "Expected identifier after `::`").lexeme;
    }

    return qualified_name;
}

std::string Parser::ParseTypeName() {
    if (Match(TokenType::LeftBracket)) {
        const Token opening = Previous();
        const std::string inner_type = ParseTypeName();
        Consume(TokenType::RightBracket, "Expected `]` after slice type");
        return "[" + inner_type + "]";
    }

    const Token name = Consume(TokenType::Identifier, "Expected type name");
    return ParseQualifiedName(name);
}

std::unique_ptr<Expr> Parser::ParseArrayLiteral(const Token& opening_bracket) {
    std::vector<std::unique_ptr<Expr>> elements;

    if (!Check(TokenType::RightBracket)) {
        do {
            elements.push_back(ParseExpression());
        } while (Match(TokenType::Comma));
    }

    Consume(TokenType::RightBracket, "Expected `]`");
    return std::make_unique<ArrayLiteralExpr>(opening_bracket.location, std::move(elements));
}

std::unique_ptr<Expr> Parser::ParseStructLiteral(const Token& type_name) {
    Consume(TokenType::LeftBrace, "Expected `{`");

    std::vector<StructLiteralField> fields;
    while (!Check(TokenType::RightBrace) && !IsAtEnd()) {
        const Token field_name = Consume(TokenType::Identifier, "Expected field name");
        Consume(TokenType::Colon, "Expected `:`");
        fields.push_back({field_name.lexeme, ParseExpression()});

        if (!Match(TokenType::Comma)) {
            break;
        }
    }

    Consume(TokenType::RightBrace, "Expected `}`");
    return std::make_unique<StructLiteralExpr>(type_name.location, type_name.lexeme, std::move(fields));
}

std::unique_ptr<Expr> Parser::ParseConditionExpression() {
    const bool previous_value = allow_struct_literals_;
    allow_struct_literals_ = false;
    auto expression = ParseExpression();
    allow_struct_literals_ = previous_value;
    return expression;
}

bool Parser::Match(TokenType type) {
    if (!Check(type)) {
        return false;
    }
    Advance();
    return true;
}

bool Parser::Check(TokenType type) const {
    if (IsAtEnd()) {
        return type == TokenType::EndOfFile;
    }
    return Peek().type == type;
}

const Token& Parser::Advance() {
    if (!IsAtEnd()) {
        ++current_;
    }
    return Previous();
}

bool Parser::IsAtEnd() const {
    return Peek().type == TokenType::EndOfFile;
}

const Token& Parser::Peek() const {
    return tokens_[current_];
}

const Token& Parser::Previous() const {
    return tokens_[current_ - 1];
}

Token Parser::Consume(TokenType type, const std::string& message) {
    if (Check(type)) {
        return Advance();
    }
    throw ErrorAt(Peek(), message + ", but found `" + TokenTypeName(Peek().type) + "`");
}

AuraError Parser::ErrorAt(const Token& token, const std::string& message) {
    return BuildLocationError(token.location, message);
}
