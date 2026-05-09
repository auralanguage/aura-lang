#pragma once

#include "common.hpp"

#include <string>
#include <vector>

class Lexer {
  public:
    Lexer(std::string source, std::string file_path);
    std::vector<Token> ScanTokens();

  private:
    std::string source_;
    std::string file_path_;
    std::size_t index_ = 0;
    int line_ = 1;
    int column_ = 1;

    bool IsAtEnd() const;
    SourceLocation CurrentLocation() const;
    char Peek() const;
    char PeekNext() const;
    char Advance();
    bool Match(char expected);
    void SkipWhitespaceAndComments();

    Token MakeToken(TokenType type, std::string lexeme, const SourceLocation& location) const;
    static bool IsIdentifierStart(char c);
    static bool IsIdentifierPart(char c);
    Token ReadIdentifier(const SourceLocation& start, char first);
    Token ReadNumber(const SourceLocation& start, char first);
    Token ReadString(const SourceLocation& start, char quote);
    Token ReadChar(const SourceLocation& start);
    static AuraError ErrorAt(const SourceLocation& location, const std::string& message);
};
