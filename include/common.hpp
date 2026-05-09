#pragma once

#include <filesystem>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

struct SourceLocation {
    std::string file_path;
    int line = 1;
    int column = 1;
};

class AuraError : public std::runtime_error {
  public:
    explicit AuraError(const std::string& message);
};

AuraError BuildLocationError(const SourceLocation& location, const std::string& message);
std::filesystem::path GetWorkingDirectoryPath();
void SetRuntimeBasePath(const std::filesystem::path& path);
std::filesystem::path GetRuntimeBasePath();
void ClearRuntimeBasePath();
std::string PathToDisplayString(const std::filesystem::path& path);

enum class TokenType {
    EndOfFile,
    Fn,
    Import,
    Module,
    Struct,
    Let,
    Return,
    If,
    Else,
    While,
    For,
    In,
    True,
    False,
    Identifier,
    Integer,
    Char,
    String,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Comma,
    Colon,
    DoubleColon,
    DotDot,
    Dot,
    Semicolon,
    Arrow,
    AndAnd,
    Bang,
    BangEqual,
    Equal,
    EqualEqual,
    Greater,
    GreaterEqual,
    Less,
    LessEqual,
    OrOr,
    Plus,
    Minus,
    Star,
    Slash
};

struct Token {
    TokenType type;
    std::string lexeme;
    SourceLocation location;
};

std::string TokenTypeName(TokenType type);

struct StringValue;
struct StructValue;
struct ArrayValue;
using StringValuePtr = std::shared_ptr<StringValue>;
using StructValuePtr = std::shared_ptr<StructValue>;
using ArrayValuePtr = std::shared_ptr<ArrayValue>;
using Value = std::variant<std::monostate, long long, bool, char, StringValuePtr, StructValuePtr, ArrayValuePtr>;
using ArrayStorage = std::vector<Value>;
using ArrayStoragePtr = std::shared_ptr<ArrayStorage>;
using StringStoragePtr = std::shared_ptr<std::string>;

struct StringValue {
    StringStoragePtr storage;
    std::size_t start = 0;
    std::size_t length = 0;
};

struct StructValue {
    std::string type_name;
    std::unordered_map<std::string, Value> fields;
};

struct ArrayValue {
    std::string element_type_name;
    ArrayStoragePtr storage;
    std::size_t start = 0;
    std::size_t length = 0;
};

std::string ValueToString(const Value& value);
StringValuePtr MakeStringValue(std::string text);
StringValuePtr MakeStringSlice(const StringValuePtr& string_value, std::size_t start, std::size_t end);
std::size_t StringLength(const StringValue& string_value);
char StringAt(const StringValue& string_value, std::size_t index);
std::string StringToString(const StringValue& string_value);
ArrayValuePtr MakeArrayValue(std::string element_type_name, std::vector<Value> elements);
ArrayValuePtr MakeArraySlice(const ArrayValuePtr& array, std::size_t start, std::size_t end);
std::size_t ArrayLength(const ArrayValue& array);
const Value& ArrayAt(const ArrayValue& array, std::size_t index);
Value& ArrayAt(ArrayValue& array, std::size_t index);
void ArrayPush(ArrayValue& array, Value value);
Value ArrayPop(ArrayValue& array);
void ArrayInsert(ArrayValue& array, std::size_t index, Value value);
Value ArrayRemoveAt(ArrayValue& array, std::size_t index);
void ArrayClear(ArrayValue& array);

enum class TypeKind {
    Unit,
    Int,
    Bool,
    Char,
    String,
    Slice,
    Named
};

struct TypeInfo {
    TypeKind kind = TypeKind::Unit;
    std::string name;
};

bool operator==(const TypeInfo& left, const TypeInfo& right);
bool operator!=(const TypeInfo& left, const TypeInfo& right);

std::string TypeInfoName(const TypeInfo& type);
TypeInfo TypeInfoFromAnnotation(const std::string& name);
TypeInfo TypeInfoFromValue(const Value& value);
