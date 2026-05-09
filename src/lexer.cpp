#include "lexer.hpp"

#include <cctype>
#include <sstream>
#include <utility>

namespace {

char DecodeEscapeCharacter(const SourceLocation& start, char escaped) {
    switch (escaped) {
    case 'n':
        return '\n';
    case 't':
        return '\t';
    case '\\':
        return '\\';
    case '"':
        return '"';
    case '\'':
        return '\'';
    default:
        throw BuildLocationError(start, "Invalid escape sequence");
    }
}

}  // namespace

Lexer::Lexer(std::string source, std::string file_path)
    : source_(std::move(source)), file_path_(std::move(file_path)) {}

std::vector<Token> Lexer::ScanTokens() {
    std::vector<Token> tokens;

    while (!IsAtEnd()) {
        SkipWhitespaceAndComments();
        if (IsAtEnd()) {
            break;
        }

        const SourceLocation start = CurrentLocation();
        const char c = Advance();

        switch (c) {
        case '(':
            tokens.push_back(MakeToken(TokenType::LeftParen, "(", start));
            break;
        case ')':
            tokens.push_back(MakeToken(TokenType::RightParen, ")", start));
            break;
        case '{':
            tokens.push_back(MakeToken(TokenType::LeftBrace, "{", start));
            break;
        case '}':
            tokens.push_back(MakeToken(TokenType::RightBrace, "}", start));
            break;
        case '[':
            tokens.push_back(MakeToken(TokenType::LeftBracket, "[", start));
            break;
        case ']':
            tokens.push_back(MakeToken(TokenType::RightBracket, "]", start));
            break;
        case ',':
            tokens.push_back(MakeToken(TokenType::Comma, ",", start));
            break;
        case '.':
            if (Match('.')) {
                tokens.push_back(MakeToken(TokenType::DotDot, "..", start));
            } else {
                tokens.push_back(MakeToken(TokenType::Dot, ".", start));
            }
            break;
        case ':':
            if (Match(':')) {
                tokens.push_back(MakeToken(TokenType::DoubleColon, "::", start));
            } else {
                tokens.push_back(MakeToken(TokenType::Colon, ":", start));
            }
            break;
        case ';':
            tokens.push_back(MakeToken(TokenType::Semicolon, ";", start));
            break;
        case '&':
            if (Match('&')) {
                tokens.push_back(MakeToken(TokenType::AndAnd, "&&", start));
            } else {
                throw ErrorAt(start, "Standalone `&` is not supported");
            }
            break;
        case '!':
            if (Match('=')) {
                tokens.push_back(MakeToken(TokenType::BangEqual, "!=", start));
            } else {
                tokens.push_back(MakeToken(TokenType::Bang, "!", start));
            }
            break;
        case '=':
            if (Match('=')) {
                tokens.push_back(MakeToken(TokenType::EqualEqual, "==", start));
            } else {
                tokens.push_back(MakeToken(TokenType::Equal, "=", start));
            }
            break;
        case '<':
            if (Match('=')) {
                tokens.push_back(MakeToken(TokenType::LessEqual, "<=", start));
            } else {
                tokens.push_back(MakeToken(TokenType::Less, "<", start));
            }
            break;
        case '|':
            if (Match('|')) {
                tokens.push_back(MakeToken(TokenType::OrOr, "||", start));
            } else { // thats not supporting
                throw ErrorAt(start, "Standalone `|` is not supported");
            }
            break;
        case '>':
            if (Match('=')) {
                tokens.push_back(MakeToken(TokenType::GreaterEqual, ">=", start));
            } else {
                tokens.push_back(MakeToken(TokenType::Greater, ">", start));
            }
            break;
        case '+':
            tokens.push_back(MakeToken(TokenType::Plus, "+", start));
            break;
        case '-':
            if (Match('>')) {
                tokens.push_back(MakeToken(TokenType::Arrow, "->", start));
            } else {
                tokens.push_back(MakeToken(TokenType::Minus, "-", start));
            }
            break;
        case '*':
            tokens.push_back(MakeToken(TokenType::Star, "*", start));
            break;
        case '/':
            tokens.push_back(MakeToken(TokenType::Slash, "/", start));
            break;
        case '"':
            tokens.push_back(ReadString(start, '"'));
            break;
        case '\'':
            tokens.push_back(ReadChar(start));
            break;
        default:
            if (std::isdigit(static_cast<unsigned char>(c))) {
                tokens.push_back(ReadNumber(start, c));
            } else if (IsIdentifierStart(c)) {
                tokens.push_back(ReadIdentifier(start, c));
            } else {
                throw ErrorAt(start, "Unexpected character: '" + std::string(1, c) + "'");
            }
            break;
        }
    }

    tokens.push_back(MakeToken(TokenType::EndOfFile, "", CurrentLocation()));
    return tokens;
}

bool Lexer::IsAtEnd() const {
    return index_ >= source_.size();
}

SourceLocation Lexer::CurrentLocation() const {
    return {file_path_, line_, column_};
}

char Lexer::Peek() const {
    if (IsAtEnd()) {
        return '\0';
    }
    return source_[index_];
}

char Lexer::PeekNext() const {
    if (index_ + 1 >= source_.size()) {
        return '\0';
    }
    return source_[index_ + 1];
}

char Lexer::Advance() {
    const char c = source_[index_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return c;
}

bool Lexer::Match(char expected) {
    if (IsAtEnd() || source_[index_] != expected) {
        return false;
    }
    Advance();
    return true;
}

void Lexer::SkipWhitespaceAndComments() {
    while (!IsAtEnd()) {
        const char c = Peek();

        if (c == ' ' || c == '\r' || c == '\t' || c == '\n') {
            Advance();
            continue;
        }

        if (c == '/' && PeekNext() == '/') {
            while (!IsAtEnd() && Peek() != '\n') {
                Advance();
            }
            continue;
        }

        if (c == '/' && PeekNext() == '*') {
            const SourceLocation start = CurrentLocation();
            Advance();
            Advance();

            while (!IsAtEnd()) {
                if (Peek() == '*' && PeekNext() == '/') {
                    Advance();
                    Advance();
                    break;
                }
                Advance();
            }

            if (IsAtEnd() &&
                !(source_.size() >= 2 && source_[source_.size() - 2] == '*' && source_[source_.size() - 1] == '/')) {
                throw ErrorAt(start, "Unterminated block comment");
            }
            continue;
        }

        break;
    }
}

Token Lexer::MakeToken(TokenType type, std::string lexeme, const SourceLocation& location) const {
    return Token{type, std::move(lexeme), location};
}

bool Lexer::IsIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool Lexer::IsIdentifierPart(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

Token Lexer::ReadIdentifier(const SourceLocation& start, char first) {
    std::string text(1, first);
    while (!IsAtEnd() && IsIdentifierPart(Peek())) {
        text.push_back(Advance());
    }

    if (text == "fn") {
        return MakeToken(TokenType::Fn, text, start);
    }
    if (text == "import") {
        return MakeToken(TokenType::Import, text, start);
    }
    if (text == "module") {
        return MakeToken(TokenType::Module, text, start);
    }
    if (text == "struct") {
        return MakeToken(TokenType::Struct, text, start);
    }
    if (text == "let") {
        return MakeToken(TokenType::Let, text, start);
    }
    if (text == "return") {
        return MakeToken(TokenType::Return, text, start);
    }
    if (text == "if") {
        return MakeToken(TokenType::If, text, start);
    }
    if (text == "else") {
        return MakeToken(TokenType::Else, text, start);
    }
    if (text == "while") {
        return MakeToken(TokenType::While, text, start);
    }
    if (text == "for") {
        return MakeToken(TokenType::For, text, start);
    }
    if (text == "in") {
        return MakeToken(TokenType::In, text, start);
    }
    if (text == "true") {
        return MakeToken(TokenType::True, text, start);
    }
    if (text == "false") {
        return MakeToken(TokenType::False, text, start);
    }

    return MakeToken(TokenType::Identifier, text, start);
}

Token Lexer::ReadNumber(const SourceLocation& start, char first) {
    std::string text(1, first);
    while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
        text.push_back(Advance());
    }
    return MakeToken(TokenType::Integer, text, start);
}

Token Lexer::ReadString(const SourceLocation& start, char quote) {
    std::string value;

    while (!IsAtEnd()) {
        const char c = Advance();
        if (c == quote) {
            return MakeToken(TokenType::String, value, start);
        }

        if (c == '\\') {
            if (IsAtEnd()) {
                break;
            }

            value.push_back(DecodeEscapeCharacter(start, Advance()));
            continue;
        }

        value.push_back(c);
    }

    throw ErrorAt(start, "Unterminated string literal");
}

Token Lexer::ReadChar(const SourceLocation& start) {
    if (IsAtEnd()) {
        throw ErrorAt(start, "Unterminated char literal");
    }

    char value = '\0';
    const char c = Advance();
    if (c == '\\') {
        if (IsAtEnd()) {
            throw ErrorAt(start, "Unterminated char literal");
        }
        value = DecodeEscapeCharacter(start, Advance());
    } else if (c == '\'') {
        throw ErrorAt(start, "Char literals must contain exactly one character");
    } else {
        value = c;
    }

    if (IsAtEnd()) {
        throw ErrorAt(start, "Unterminated char literal");
    }

    if (Advance() != '\'') {
        throw ErrorAt(start, "Char literals must contain exactly one character");
    }

    return MakeToken(TokenType::Char, std::string(1, value), start);
}

AuraError Lexer::ErrorAt(const SourceLocation& location, const std::string& message) {
    return BuildLocationError(location, message);
}
