#pragma once

#include "common.hpp"

#include <string>
#include <utility>
#include <vector>

bool AuraExpectBool(const Value& value, const std::string& context);
long long AuraExpectInteger(const Value& value, const std::string& context);
Value AuraEvaluateUnary(TokenType op, const Value& right);
Value AuraEvaluateBinary(TokenType op, const Value& left, const Value& right);
Value AuraMakeIntRange(const Value& start, const Value& end);
Value AuraMakeArrayLiteral(const std::string& element_type_name, const std::vector<Value>& elements);
Value AuraMakeStructLiteral(const std::string& type_name, const std::vector<std::pair<std::string, Value>>& fields);
Value AuraGetStructField(const Value& object, const std::string& field_name);
Value AuraIndexArray(const Value& object, const Value& index);
Value AuraIndexString(const Value& object, const Value& index);
Value AuraSliceArray(const Value& object, bool has_start, const Value& start, bool has_end, const Value& end);
Value AuraSliceString(const Value& object, bool has_start, const Value& start, bool has_end, const Value& end);
Value AuraAssignField(const Value& object, const std::string& field_name, const Value& value);
Value AuraAssignIndex(const Value& object, const Value& index, const Value& value);
Value AuraBuiltinPrint(const std::vector<Value>& arguments);
Value AuraBuiltinLen(const Value& argument);
Value AuraBuiltinPush(const Value& array, const Value& value);
Value AuraBuiltinPop(const Value& array);
Value AuraBuiltinInsert(const Value& array, const Value& index, const Value& value);
Value AuraBuiltinRemoveAt(const Value& array, const Value& index);
Value AuraBuiltinClear(const Value& array);
Value AuraBuiltinContains(const Value& target, const Value& needle);
Value AuraBuiltinStartsWith(const Value& value, const Value& prefix);
Value AuraBuiltinEndsWith(const Value& value, const Value& suffix);
Value AuraBuiltinJoin(const Value& values, const Value& separator);
Value AuraBuiltinFileExists(const Value& path_value, const std::string& source_path);
Value AuraBuiltinReadText(const Value& path_value, const std::string& source_path);
