#pragma once

#include "ir.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

using CfgValueId = std::size_t;
using CfgBlockId = std::size_t;
constexpr CfgValueId kInvalidCfgValueId = static_cast<CfgValueId>(-1);

enum class CfgInstructionKind {
    Literal,
    Copy,
    Drop,
    ArrayLiteral,
    StructLiteral,
    Unary,
    Binary,
    Range,
    Call,
    GetField,
    GetIndex,
    GetSlice,
    AssignField,
    AssignIndex
};

struct CfgStructFieldValue {
    std::string name;
    CfgValueId value = 0;
};

struct CfgInstruction {
    SourceLocation location;
    CfgValueId result = kInvalidCfgValueId;
    TypeInfo type{TypeKind::Unit, ""};
    CfgInstructionKind kind = CfgInstructionKind::Literal;
    Value literal_value;
    std::string name;
    TokenType token = TokenType::EndOfFile;
    bool has_start = false;
    bool has_end = false;
    IrCallKind call_kind = IrCallKind::UserFunction;
    IrBuiltinKind builtin_kind = IrBuiltinKind::None;
    std::vector<CfgValueId> inputs;
    std::vector<CfgStructFieldValue> fields;
};

struct CfgBlockParameter {
    CfgValueId value = 0;
    std::string name;
    TypeInfo type{TypeKind::Unit, ""};
};

struct CfgBlockTarget {
    CfgBlockId block = 0;
    std::vector<CfgValueId> arguments;
};

enum class CfgTerminatorKind {
    None,
    Jump,
    Branch,
    Return
};

struct CfgTerminator {
    SourceLocation location;
    CfgTerminatorKind kind = CfgTerminatorKind::None;
    std::optional<CfgValueId> condition;
    CfgBlockTarget target;
    CfgBlockTarget true_target;
    CfgBlockTarget false_target;
    std::optional<CfgValueId> return_value;
};

struct CfgBlock {
    CfgBlockId id = 0;
    std::string label;
    std::vector<CfgBlockParameter> parameters;
    std::vector<CfgInstruction> instructions;
    CfgTerminator terminator;
};

struct CfgFunctionParameter {
    std::string name;
    TypeInfo type{TypeKind::Unit, ""};
    CfgValueId value = 0;
};

struct CfgFunctionDecl {
    SourceLocation location;
    std::string name;
    std::optional<std::string> module_name;
    std::optional<std::string> owner_type_name;
    std::string full_name;
    std::vector<CfgFunctionParameter> parameters;
    TypeInfo return_type{TypeKind::Unit, ""};
    CfgBlockId entry_block = 0;
    std::vector<CfgBlock> blocks;
    std::size_t value_count = 0;
};

struct CfgProgram {
    std::optional<std::string> module_name;
    std::vector<ImportDecl> imports;
    std::vector<IrStructDecl> structs;
    std::vector<CfgFunctionDecl> functions;
};

CfgProgram LowerIrToCfg(const IrProgram& program);
std::string FormatCfgProgram(const CfgProgram& program);
