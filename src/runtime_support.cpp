#include "runtime_support.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace {

bool AuraStartsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool AuraEndsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool AuraIsStringLikeValue(const Value& value) {
    return std::holds_alternative<StringValuePtr>(value) || std::holds_alternative<char>(value);
}

std::string AuraStringLikeValueToString(const Value& value) {
    if (const auto* string_value = std::get_if<StringValuePtr>(&value)) {
        return StringToString(**string_value);
    }
    if (const auto* char_value = std::get_if<char>(&value)) {
        return std::string(1, *char_value);
    }
    throw AuraError("Internal error: value is not string-like");
}

bool AuraStringLikeEquals(const Value& left, const Value& right) {
    if (AuraIsStringLikeValue(left) && AuraIsStringLikeValue(right)) {
        return AuraStringLikeValueToString(left) == AuraStringLikeValueToString(right);
    }
    return left == right;
}

bool AuraIsStringCollectionElementType(const std::string& element_type_name) {
    return element_type_name == "String" || element_type_name == "Char";
}

std::string AuraReadTextFile(const fs::path& path) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input.is_open()) {
        throw AuraError("Function `read_text` could not open file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

fs::path AuraResolveRuntimePath(const std::string& raw_path, const std::string& source_path) {
    fs::path path(raw_path);
    if (path.is_absolute()) {
        return path;
    }

    const fs::path runtime_base = GetRuntimeBasePath();
    if (!runtime_base.empty()) {
        return (runtime_base / path).lexically_normal();
    }

    if (source_path.empty()) {
        return path;
    }

    return (fs::path(source_path).parent_path() / path).lexically_normal();
}

}  // namespace

bool AuraExpectBool(const Value& value, const std::string& context) {
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean;
    }
    throw AuraError(context + " must be Bool");
}

long long AuraExpectInteger(const Value& value, const std::string& context) {
    if (const auto* integer = std::get_if<long long>(&value)) {
        return *integer;
    }
    throw AuraError(context + " must be Int");
}

Value AuraEvaluateUnary(TokenType op, const Value& right) {
    switch (op) {
    case TokenType::Bang:
        return !AuraExpectBool(right, "Unary `!`");
    case TokenType::Minus:
        return -AuraExpectInteger(right, "Unary `-`");
    default:
        break;
    }

    throw AuraError("Unsupported unary operator");
}

Value AuraEvaluateBinary(TokenType op, const Value& left, const Value& right) {
    switch (op) {
    case TokenType::EqualEqual:
        return AuraStringLikeEquals(left, right);
    case TokenType::BangEqual:
        return !AuraStringLikeEquals(left, right);
    case TokenType::AndAnd:
    case TokenType::OrOr:
        throw AuraError("Logical operators must be evaluated through short-circuit handling");
    case TokenType::Plus:
        if (AuraIsStringLikeValue(left) && AuraIsStringLikeValue(right)) {
            if (std::holds_alternative<char>(left) && std::holds_alternative<char>(right)) {
                throw AuraError("`+` only accepts `Int + Int`, `String + String`, `String + Char`, or `Char + String`");
            }
            return MakeStringValue(AuraStringLikeValueToString(left) + AuraStringLikeValueToString(right));
        }
        return AuraExpectInteger(left, "`+`") + AuraExpectInteger(right, "`+`");
    case TokenType::Minus:
        return AuraExpectInteger(left, "`-`") - AuraExpectInteger(right, "`-`");
    case TokenType::Star:
        return AuraExpectInteger(left, "`*`") * AuraExpectInteger(right, "`*`");
    case TokenType::Slash: {
        const long long divisor = AuraExpectInteger(right, "`/`");
        if (divisor == 0) {
            throw AuraError("Division by zero");
        }
        return AuraExpectInteger(left, "`/`") / divisor;
    }
    case TokenType::Greater:
        return AuraExpectInteger(left, "`>`") > AuraExpectInteger(right, "`>`");
    case TokenType::GreaterEqual:
        return AuraExpectInteger(left, "`>=`") >= AuraExpectInteger(right, "`>=`");
    case TokenType::Less:
        return AuraExpectInteger(left, "`<`") < AuraExpectInteger(right, "`<`");
    case TokenType::LessEqual:
        return AuraExpectInteger(left, "`<=`") <= AuraExpectInteger(right, "`<=`");
    default:
        break;
    }

    throw AuraError("Unsupported binary operator");
}

Value AuraMakeIntRange(const Value& start, const Value& end) {
    const long long raw_start = AuraExpectInteger(start, "Range start");
    const long long raw_end = AuraExpectInteger(end, "Range end");

    std::vector<Value> elements;
    if (raw_start < raw_end) {
        elements.reserve(static_cast<std::size_t>(raw_end - raw_start));
        for (long long value = raw_start; value < raw_end; ++value) {
            elements.push_back(value);
        }
    }

    return MakeArrayValue("Int", std::move(elements));
}

Value AuraMakeArrayLiteral(const std::string& element_type_name, const std::vector<Value>& elements) {
    return MakeArrayValue(element_type_name, elements);
}

Value AuraMakeStructLiteral(const std::string& type_name, const std::vector<std::pair<std::string, Value>>& fields) {
    auto struct_value = std::make_shared<StructValue>();
    struct_value->type_name = type_name;
    for (const auto& [field_name, field_value] : fields) {
        struct_value->fields[field_name] = field_value;
    }
    return struct_value;
}

Value AuraGetStructField(const Value& object, const std::string& field_name) {
    const auto* struct_value = std::get_if<StructValuePtr>(&object);
    if (struct_value == nullptr || *struct_value == nullptr) {
        throw AuraError("Field access requires a struct value");
    }

    const auto field_it = (*struct_value)->fields.find(field_name);
    if (field_it == (*struct_value)->fields.end()) {
        throw AuraError("Struct `" + (*struct_value)->type_name + "` has no field `" + field_name + "`");
    }
    return field_it->second;
}

Value AuraIndexArray(const Value& object, const Value& index) {
    const auto* array_value = std::get_if<ArrayValuePtr>(&object);
    if (array_value == nullptr || *array_value == nullptr) {
        throw AuraError("Index access requires a slice value or String");
    }

    const long long raw_index = AuraExpectInteger(index, "Slice index");
    if (raw_index < 0) {
        throw AuraError("Slice index out of bounds");
    }
    return ArrayAt(**array_value, static_cast<std::size_t>(raw_index));
}

Value AuraIndexString(const Value& object, const Value& index) {
    const auto* string_value = std::get_if<StringValuePtr>(&object);
    if (string_value == nullptr || *string_value == nullptr) {
        throw AuraError("Index access requires a slice value or String");
    }

    const long long raw_index = AuraExpectInteger(index, "String index");
    if (raw_index < 0) {
        throw AuraError("String index out of bounds");
    }
    return StringAt(**string_value, static_cast<std::size_t>(raw_index));
}

Value AuraSliceArray(const Value& object, bool has_start, const Value& start, bool has_end, const Value& end) {
    const auto* array_value = std::get_if<ArrayValuePtr>(&object);
    if (array_value == nullptr || *array_value == nullptr) {
        throw AuraError("Slice access requires a slice value or String");
    }

    const std::size_t size = ArrayLength(**array_value);
    long long raw_start = 0;
    long long raw_end = static_cast<long long>(size);

    if (has_start) {
        raw_start = AuraExpectInteger(start, "Slice start index");
    }
    if (has_end) {
        raw_end = AuraExpectInteger(end, "Slice end index");
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

    return MakeArraySlice(*array_value, static_cast<std::size_t>(raw_start), static_cast<std::size_t>(raw_end));
}

Value AuraSliceString(const Value& object, bool has_start, const Value& start, bool has_end, const Value& end) {
    const auto* string_value = std::get_if<StringValuePtr>(&object);
    if (string_value == nullptr || *string_value == nullptr) {
        throw AuraError("Slice access requires a slice value or String");
    }

    const std::size_t size = StringLength(**string_value);
    long long raw_start = 0;
    long long raw_end = static_cast<long long>(size);

    if (has_start) {
        raw_start = AuraExpectInteger(start, "String slice start index");
    }
    if (has_end) {
        raw_end = AuraExpectInteger(end, "String slice end index");
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

    return MakeStringSlice(*string_value, static_cast<std::size_t>(raw_start), static_cast<std::size_t>(raw_end));
}

Value AuraAssignField(const Value& object, const std::string& field_name, const Value& value) {
    const auto* struct_value = std::get_if<StructValuePtr>(&object);
    if (struct_value == nullptr || *struct_value == nullptr) {
        throw AuraError("Field assignment requires a struct value");
    }

    const auto field_it = (*struct_value)->fields.find(field_name);
    if (field_it == (*struct_value)->fields.end()) {
        throw AuraError("Struct `" + (*struct_value)->type_name + "` has no field `" + field_name + "`");
    }

    field_it->second = value;
    return value;
}

Value AuraAssignIndex(const Value& object, const Value& index, const Value& value) {
    if (const auto* array_value = std::get_if<ArrayValuePtr>(&object)) {
        if (*array_value == nullptr) {
            throw AuraError("Index assignment requires a slice value");
        }

        const long long raw_index = AuraExpectInteger(index, "Slice index");
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

Value AuraBuiltinPrint(const std::vector<Value>& arguments) {
    for (const Value& argument : arguments) {
        std::cout << ValueToString(argument) << '\n';
    }
    return std::monostate{};
}

Value AuraBuiltinLen(const Value& argument) {
    if (const auto* string_value = std::get_if<StringValuePtr>(&argument)) {
        return static_cast<long long>(StringLength(**string_value));
    }

    const auto* array_value = std::get_if<ArrayValuePtr>(&argument);
    if (array_value == nullptr || *array_value == nullptr) {
        throw AuraError("Function `len` expects a slice or String");
    }

    return static_cast<long long>(ArrayLength(**array_value));
}

Value AuraBuiltinPush(const Value& array, const Value& value) {
    const auto* array_value = std::get_if<ArrayValuePtr>(&array);
    if (array_value == nullptr || *array_value == nullptr) {
        throw AuraError("Function `push` expects a slice as argument #1");
    }

    if (!(*array_value)->element_type_name.empty()) {
        const std::string actual_type_name = TypeInfoName(TypeInfoFromValue(value));
        if (actual_type_name != (*array_value)->element_type_name) {
            throw AuraError("Function `push` received a value with the wrong element type");
        }
    }

    ArrayPush(**array_value, value);
    return std::monostate{};
}

Value AuraBuiltinPop(const Value& array) {
    const auto* array_value = std::get_if<ArrayValuePtr>(&array);
    if (array_value == nullptr || *array_value == nullptr) {
        throw AuraError("Function `pop` expects a slice");
    }
    return ArrayPop(**array_value);
}

Value AuraBuiltinInsert(const Value& array, const Value& index, const Value& value) {
    const auto* array_value = std::get_if<ArrayValuePtr>(&array);
    if (array_value == nullptr || *array_value == nullptr) {
        throw AuraError("Function `insert` expects a slice as argument #1");
    }

    const long long raw_index = AuraExpectInteger(index, "Function `insert` argument #2");
    if (raw_index < 0) {
        throw AuraError("Function `insert` index is out of bounds");
    }

    if (!(*array_value)->element_type_name.empty()) {
        const std::string actual_type_name = TypeInfoName(TypeInfoFromValue(value));
        if (actual_type_name != (*array_value)->element_type_name) {
            throw AuraError("Function `insert` received a value with the wrong element type");
        }
    }

    ArrayInsert(**array_value, static_cast<std::size_t>(raw_index), value);
    return std::monostate{};
}

Value AuraBuiltinRemoveAt(const Value& array, const Value& index) {
    const auto* array_value = std::get_if<ArrayValuePtr>(&array);
    if (array_value == nullptr || *array_value == nullptr) {
        throw AuraError("Function `remove_at` expects a slice as argument #1");
    }

    const long long raw_index = AuraExpectInteger(index, "Function `remove_at` argument #2");
    if (raw_index < 0) {
        throw AuraError("Function `remove_at` index is out of bounds");
    }

    return ArrayRemoveAt(**array_value, static_cast<std::size_t>(raw_index));
}

Value AuraBuiltinClear(const Value& array) {
    const auto* array_value = std::get_if<ArrayValuePtr>(&array);
    if (array_value == nullptr || *array_value == nullptr) {
        throw AuraError("Function `clear` expects a slice");
    }

    ArrayClear(**array_value);
    return std::monostate{};
}

Value AuraBuiltinContains(const Value& target, const Value& needle) {
    if (const auto* string_value = std::get_if<StringValuePtr>(&target)) {
        if (!AuraIsStringLikeValue(needle)) {
            throw AuraError("Function `contains` expects `String` or `Char` for argument #2 when argument #1 is `String`");
        }
        return StringToString(**string_value).find(AuraStringLikeValueToString(needle)) != std::string::npos;
    }

    const auto* array_value = std::get_if<ArrayValuePtr>(&target);
    if (array_value != nullptr && *array_value != nullptr && (*array_value)->element_type_name == "String") {
        const auto* string_needle = std::get_if<StringValuePtr>(&needle);
        if (string_needle == nullptr) {
            throw AuraError("Function `contains` expects `String` for argument #2 when argument #1 is `[String]`");
        }

        for (std::size_t i = 0; i < ArrayLength(**array_value); ++i) {
            const auto* element_string = std::get_if<StringValuePtr>(&ArrayAt(**array_value, i));
            if (element_string == nullptr) {
                throw AuraError("Function `contains` expected `[String]` storage to contain only String values");
            }
            if (StringToString(**element_string) == StringToString(**string_needle)) {
                return true;
            }
        }
        return false;
    }

    if (array_value != nullptr && *array_value != nullptr && (*array_value)->element_type_name == "Char") {
        const auto* char_needle = std::get_if<char>(&needle);
        if (char_needle == nullptr) {
            throw AuraError("Function `contains` expects `Char` for argument #2 when argument #1 is `[Char]`");
        }

        for (std::size_t i = 0; i < ArrayLength(**array_value); ++i) {
            const auto* element_char = std::get_if<char>(&ArrayAt(**array_value, i));
            if (element_char == nullptr) {
                throw AuraError("Function `contains` expected `[Char]` storage to contain only Char values");
            }
            if (*element_char == *char_needle) {
                return true;
            }
        }
        return false;
    }

    throw AuraError("Function `contains` expects `String`, `[String]`, or `[Char]` as argument #1");
}

Value AuraBuiltinStartsWith(const Value& value, const Value& prefix) {
    const auto* string_value = std::get_if<StringValuePtr>(&value);
    if (string_value == nullptr) {
        throw AuraError("Function `starts_with` expects `String` as argument #1");
    }
    if (!AuraIsStringLikeValue(prefix)) {
        throw AuraError("Function `starts_with` expects `String` or `Char` for argument #2");
    }
    return AuraStartsWith(StringToString(**string_value), AuraStringLikeValueToString(prefix));
}

Value AuraBuiltinEndsWith(const Value& value, const Value& suffix) {
    const auto* string_value = std::get_if<StringValuePtr>(&value);
    if (string_value == nullptr) {
        throw AuraError("Function `ends_with` expects `String` as argument #1");
    }
    if (!AuraIsStringLikeValue(suffix)) {
        throw AuraError("Function `ends_with` expects `String` or `Char` for argument #2");
    }
    return AuraEndsWith(StringToString(**string_value), AuraStringLikeValueToString(suffix));
}

Value AuraBuiltinJoin(const Value& values, const Value& separator) {
    const auto* array_value = std::get_if<ArrayValuePtr>(&values);
    if (array_value == nullptr || *array_value == nullptr ||
        !AuraIsStringCollectionElementType((*array_value)->element_type_name)) {
        throw AuraError("Function `join` expects `[String]` or `[Char]` as argument #1");
    }

    const auto* separator_value = std::get_if<StringValuePtr>(&separator);
    if (separator_value == nullptr) {
        throw AuraError("Function `join` expects `String` for argument #2");
    }

    std::ostringstream buffer;
    for (std::size_t i = 0; i < ArrayLength(**array_value); ++i) {
        if (i > 0) {
            buffer << StringToString(**separator_value);
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

Value AuraBuiltinFileExists(const Value& path_value, const std::string& source_path) {
    const auto* path_string = std::get_if<StringValuePtr>(&path_value);
    if (path_string == nullptr) {
        throw AuraError("Function `file_exists` expects `String` as argument #1");
    }

    const fs::path resolved_path = AuraResolveRuntimePath(StringToString(**path_string), source_path);
    std::error_code error_code;
    return fs::exists(resolved_path, error_code) && !error_code;
}

Value AuraBuiltinReadText(const Value& path_value, const std::string& source_path) {
    const auto* path_string = std::get_if<StringValuePtr>(&path_value);
    if (path_string == nullptr) {
        throw AuraError("Function `read_text` expects `String` as argument #1");
    }

    return MakeStringValue(AuraReadTextFile(AuraResolveRuntimePath(StringToString(**path_string), source_path)));
}
