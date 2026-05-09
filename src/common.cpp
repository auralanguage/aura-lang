#include "common.hpp"

#ifdef _WIN32
#include <direct.h>
#endif

#include <codecvt>
#include <locale>
#include <optional>
#include <sstream>

namespace {

std::optional<std::filesystem::path> g_runtime_base_path;

void ValidateStringView(const StringValue& string_value) {
    if (!string_value.storage) {
        throw AuraError("Internal error: string storage is missing");
    }

    if (string_value.start > string_value.storage->size()) {
        throw AuraError("Internal error: string start is out of bounds");
    }

    const std::size_t remaining = string_value.storage->size() - string_value.start;
    if (string_value.length > remaining) {
        throw AuraError("Internal error: string length is out of bounds");
    }
}

std::size_t ResolveStringIndex(const StringValue& string_value, std::size_t index) {
    ValidateStringView(string_value);
    if (index >= string_value.length) {
        throw AuraError("String index out of bounds");
    }

    return string_value.start + index;
}

void ValidateArrayView(const ArrayValue& array) {
    if (!array.storage) {
        throw AuraError("Internal error: array storage is missing");
    }

    if (array.start > array.storage->size()) {
        throw AuraError("Internal error: slice start is out of bounds");
    }

    const std::size_t remaining = array.storage->size() - array.start;
    if (array.length > remaining) {
        throw AuraError("Internal error: slice length is out of bounds");
    }
}

std::size_t ResolveArrayIndex(const ArrayValue& array, std::size_t index) {
    ValidateArrayView(array);
    if (index >= array.length) {
        throw AuraError("Slice index out of bounds");
    }

    return array.start + index;
}

bool IsWholeArrayView(const ArrayValue& array) {
    ValidateArrayView(array);
    return array.start == 0 && array.length == array.storage->size();
}

}  // namespace

AuraError::AuraError(const std::string& message) : std::runtime_error(message) {}

AuraError BuildLocationError(const SourceLocation& location, const std::string& message) {
    std::ostringstream buffer;
    buffer << "[";
    if (!location.file_path.empty()) {
        buffer << location.file_path << ':';
    }
    buffer << "line " << location.line << ", column " << location.column << "] " << message;
    return AuraError(buffer.str());
}

std::filesystem::path GetWorkingDirectoryPath() {
#ifdef _WIN32
    for (std::size_t buffer_size = 260; buffer_size <= 32768; buffer_size *= 2) {
        std::vector<wchar_t> buffer(buffer_size, L'\0');
        if (_wgetcwd(buffer.data(), static_cast<int>(buffer.size())) != nullptr) {
            return std::filesystem::path(buffer.data()).lexically_normal();
        }
    }
    throw AuraError("Could not determine the current working directory");
#else
    return std::filesystem::current_path().lexically_normal();
#endif
}

void SetRuntimeBasePath(const std::filesystem::path& path) {
    g_runtime_base_path = path.lexically_normal();
}

std::filesystem::path GetRuntimeBasePath() {
    return g_runtime_base_path.has_value() ? g_runtime_base_path->lexically_normal() : std::filesystem::path{};
}

void ClearRuntimeBasePath() {
    g_runtime_base_path.reset();
}

std::string PathToDisplayString(const std::filesystem::path& path) {
#ifdef _WIN32
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> converter;
    return converter.to_bytes(path.native());
#else
    return path.string();
#endif
}

std::string TokenTypeName(TokenType type) {
    switch (type) {
    case TokenType::EndOfFile:
        return "EOF";
    case TokenType::Fn:
        return "fn";
    case TokenType::Import:
        return "import";
    case TokenType::Module:
        return "module";
    case TokenType::Struct:
        return "struct";
    case TokenType::Let:
        return "let";
    case TokenType::Return:
        return "return";
    case TokenType::If:
        return "if";
    case TokenType::Else:
        return "else";
    case TokenType::While:
        return "while";
    case TokenType::For:
        return "for";
    case TokenType::In:
        return "in";
    case TokenType::True:
        return "true";
    case TokenType::False:
        return "false";
    case TokenType::Identifier:
        return "identifier";
    case TokenType::Integer:
        return "integer";
    case TokenType::Char:
        return "char";
    case TokenType::String:
        return "string";
    case TokenType::LeftParen:
        return "(";
    case TokenType::RightParen:
        return ")";
    case TokenType::LeftBrace:
        return "{";
    case TokenType::RightBrace:
        return "}";
    case TokenType::LeftBracket:
        return "[";
    case TokenType::RightBracket:
        return "]";
    case TokenType::Comma:
        return ",";
    case TokenType::Colon:
        return ":";
    case TokenType::DoubleColon:
        return "::";
    case TokenType::DotDot:
        return "..";
    case TokenType::Dot:
        return ".";
    case TokenType::Semicolon:
        return ";";
    case TokenType::Arrow:
        return "->";
    case TokenType::AndAnd:
        return "&&";
    case TokenType::Bang:
        return "!";
    case TokenType::BangEqual:
        return "!=";
    case TokenType::Equal:
        return "=";
    case TokenType::EqualEqual:
        return "==";
    case TokenType::Greater:
        return ">";
    case TokenType::GreaterEqual:
        return ">=";
    case TokenType::Less:
        return "<";
    case TokenType::LessEqual:
        return "<=";
    case TokenType::OrOr:
        return "||";
    case TokenType::Plus:
        return "+";
    case TokenType::Minus:
        return "-";
    case TokenType::Star:
        return "*";
    case TokenType::Slash:
        return "/";
    }

    return "unknown";
}

std::string ValueToString(const Value& value) {
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
        return std::string(1, *char_value);
    }
    if (const auto* string_value = std::get_if<StringValuePtr>(&value)) {
        return StringToString(**string_value);
    }
    if (const auto* array_value = std::get_if<ArrayValuePtr>(&value)) {
        std::ostringstream buffer;
        buffer << "[";

        bool first = true;
        for (std::size_t i = 0; i < ArrayLength(**array_value); ++i) {
            if (!first) {
                buffer << ", ";
            }
            first = false;
            buffer << ValueToString(ArrayAt(**array_value, i));
        }

        buffer << "]";
        return buffer.str();
    }

    const StructValuePtr struct_value = std::get<StructValuePtr>(value);
    std::ostringstream buffer;
    buffer << struct_value->type_name << " { ";

    bool first = true;
    for (const auto& [field_name, field_value] : struct_value->fields) {
        if (!first) {
            buffer << ", ";
        }
        first = false;
        buffer << field_name << ": " << ValueToString(field_value);
    }

    buffer << " }";
    return buffer.str();
}

StringValuePtr MakeStringValue(std::string text) {
    auto string_value = std::make_shared<StringValue>();
    string_value->storage = std::make_shared<std::string>(std::move(text));
    string_value->length = string_value->storage->size();
    return string_value;
}

StringValuePtr MakeStringSlice(const StringValuePtr& string_value, std::size_t start, std::size_t end) {
    if (!string_value) {
        throw AuraError("Internal error: cannot create a slice from a null string");
    }

    const std::size_t string_length = StringLength(*string_value);
    if (start > end || end > string_length) {
        throw AuraError("String range out of bounds");
    }

    auto slice_value = std::make_shared<StringValue>();
    slice_value->storage = string_value->storage;
    slice_value->start = string_value->start + start;
    slice_value->length = end - start;
    return slice_value;
}

std::size_t StringLength(const StringValue& string_value) {
    ValidateStringView(string_value);
    return string_value.length;
}

char StringAt(const StringValue& string_value, std::size_t index) {
    return (*string_value.storage)[ResolveStringIndex(string_value, index)];
}

std::string StringToString(const StringValue& string_value) {
    ValidateStringView(string_value);
    return string_value.storage->substr(string_value.start, string_value.length);
}

ArrayValuePtr MakeArrayValue(std::string element_type_name, std::vector<Value> elements) {
    auto array_value = std::make_shared<ArrayValue>();
    array_value->element_type_name = std::move(element_type_name);
    array_value->storage = std::make_shared<ArrayStorage>(std::move(elements));
    array_value->length = array_value->storage->size();
    return array_value;
}

ArrayValuePtr MakeArraySlice(const ArrayValuePtr& array, std::size_t start, std::size_t end) {
    if (!array) {
        throw AuraError("Internal error: cannot create a slice from a null array");
    }

    const std::size_t array_length = ArrayLength(*array);
    if (start > end || end > array_length) {
        throw AuraError("Slice range out of bounds");
    }

    auto slice_value = std::make_shared<ArrayValue>();
    slice_value->element_type_name = array->element_type_name;
    slice_value->storage = array->storage;
    slice_value->start = array->start + start;
    slice_value->length = end - start;
    return slice_value;
}

std::size_t ArrayLength(const ArrayValue& array) {
    ValidateArrayView(array);
    return array.length;
}

const Value& ArrayAt(const ArrayValue& array, std::size_t index) {
    return (*array.storage)[ResolveArrayIndex(array, index)];
}

Value& ArrayAt(ArrayValue& array, std::size_t index) {
    return (*array.storage)[ResolveArrayIndex(array, index)];
}

void ArrayPush(ArrayValue& array, Value value) {
    if (!IsWholeArrayView(array)) {
        throw AuraError("Function `push` does not support slice views yet");
    }
    if (array.storage.use_count() > 1) {
        throw AuraError("Function `push` cannot mutate an array while slice views exist");
    }

    array.storage->push_back(std::move(value));
    array.length = array.storage->size();
}

Value ArrayPop(ArrayValue& array) {
    if (!IsWholeArrayView(array)) {
        throw AuraError("Function `pop` does not support slice views yet");
    }
    if (array.storage.use_count() > 1) {
        throw AuraError("Function `pop` cannot mutate an array while slice views exist");
    }
    if (array.storage->empty()) {
        throw AuraError("Function `pop` cannot pop from an empty slice");
    }

    Value result = array.storage->back();
    array.storage->pop_back();
    array.length = array.storage->size();
    return result;
}

void ArrayInsert(ArrayValue& array, std::size_t index, Value value) {
    if (!IsWholeArrayView(array)) {
        throw AuraError("Function `insert` does not support slice views yet");
    }
    if (array.storage.use_count() > 1) {
        throw AuraError("Function `insert` cannot mutate an array while slice views exist");
    }
    if (index > array.storage->size()) {
        throw AuraError("Function `insert` index is out of bounds");
    }

    array.storage->insert(array.storage->begin() + static_cast<std::ptrdiff_t>(index), std::move(value));
    array.length = array.storage->size();
}

Value ArrayRemoveAt(ArrayValue& array, std::size_t index) {
    if (!IsWholeArrayView(array)) {
        throw AuraError("Function `remove_at` does not support slice views yet");
    }
    if (array.storage.use_count() > 1) {
        throw AuraError("Function `remove_at` cannot mutate an array while slice views exist");
    }
    if (index >= array.storage->size()) {
        throw AuraError("Function `remove_at` index is out of bounds");
    }

    auto iterator = array.storage->begin() + static_cast<std::ptrdiff_t>(index);
    Value result = *iterator;
    array.storage->erase(iterator);
    array.length = array.storage->size();
    return result;
}

void ArrayClear(ArrayValue& array) {
    if (!IsWholeArrayView(array)) {
        throw AuraError("Function `clear` does not support slice views yet");
    }
    if (array.storage.use_count() > 1) {
        throw AuraError("Function `clear` cannot mutate an array while slice views exist");
    }

    array.storage->clear();
    array.length = 0;
}

bool operator==(const TypeInfo& left, const TypeInfo& right) {
    return left.kind == right.kind && left.name == right.name;
}

bool operator!=(const TypeInfo& left, const TypeInfo& right) {
    return !(left == right);
}

std::string TypeInfoName(const TypeInfo& type) {
    switch (type.kind) {
    case TypeKind::Unit:
        return "Unit";
    case TypeKind::Int:
        return "Int";
    case TypeKind::Bool:
        return "Bool";
    case TypeKind::Char:
        return "Char";
    case TypeKind::String:
        return "String";
    case TypeKind::Slice:
        return "[" + type.name + "]";
    case TypeKind::Named:
        return type.name;
    }

    return "Unknown";
}

TypeInfo TypeInfoFromAnnotation(const std::string& name) {
    if (name == "Unit") {
        return {TypeKind::Unit, ""};
    }
    if (name == "Int") {
        return {TypeKind::Int, ""};
    }
    if (name == "Bool") {
        return {TypeKind::Bool, ""};
    }
    if (name == "Char") {
        return {TypeKind::Char, ""};
    }
    if (name == "String") {
        return {TypeKind::String, ""};
    }
    if (name.size() >= 2 && name.front() == '[' && name.back() == ']') {
        return {TypeKind::Slice, name.substr(1, name.size() - 2)};
    }

    return {TypeKind::Named, name};
}

TypeInfo TypeInfoFromValue(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return {TypeKind::Unit, ""};
    }
    if (std::holds_alternative<long long>(value)) {
        return {TypeKind::Int, ""};
    }
    if (std::holds_alternative<bool>(value)) {
        return {TypeKind::Bool, ""};
    }
    if (std::holds_alternative<char>(value)) {
        return {TypeKind::Char, ""};
    }
    if (std::holds_alternative<StringValuePtr>(value)) {
        return {TypeKind::String, ""};
    }
    if (std::holds_alternative<ArrayValuePtr>(value)) {
        const ArrayValuePtr array_value = std::get<ArrayValuePtr>(value);
        return {TypeKind::Slice, array_value->element_type_name};
    }

    const StructValuePtr struct_value = std::get<StructValuePtr>(value);
    return {TypeKind::Named, struct_value->type_name};
}
