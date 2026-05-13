#include "cpp_backend.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {

std::string EscapeForCppString(const std::string& text) {
    std::ostringstream buffer;
    for (const unsigned char c : text) {
        switch (c) {
        case '\\':
            buffer << "\\\\";
            break;
        case '"':
            buffer << "\\\"";
            break;
        case '\n':
            buffer << "\\n";
            break;
        case '\r':
            buffer << "\\r";
            break;
        case '\t':
            buffer << "\\t";
            break;
        default:
            if (c >= 32 && c <= 126) {
                buffer << static_cast<char>(c);
            } else {
                buffer << '\\';
                buffer << static_cast<char>('0' + ((c >> 6) & 0x07));
                buffer << static_cast<char>('0' + ((c >> 3) & 0x07));
                buffer << static_cast<char>('0' + (c & 0x07));
            }
            break;
        }
    }
    return buffer.str();
}

std::string SanitizeIdentifier(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (const unsigned char c : text) {
        if (std::isalnum(c)) {
            result.push_back(static_cast<char>(c));
        } else {
            result.push_back('_');
        }
    }

    if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front()))) {
        result.insert(result.begin(), '_');
    }
    return result;
}

std::string FormatLiteralValue(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return "Value{std::monostate{}}";
    }
    if (const auto* integer = std::get_if<long long>(&value)) {
        return "Value{" + std::to_string(*integer) + "LL}";
    }
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean ? "Value{true}" : "Value{false}";
    }
    if (const auto* char_value = std::get_if<char>(&value)) {
        if (*char_value == '\\') {
            return "Value{'\\\\'}";
        }
        if (*char_value == '\'') {
            return "Value{'\\''}";
        }
        return std::string("Value{'") + *char_value + "'}";
    }
    if (const auto* string_value = std::get_if<StringValuePtr>(&value)) {
        return "Value{MakeStringValue(\"" + EscapeForCppString(StringToString(**string_value)) + "\")}";
    }

    throw std::runtime_error("Cpp backend does not support non-literal struct/array Value constants");
}

std::string TokenTypeCode(TokenType type) {
    switch (type) {
    case TokenType::AndAnd:
        return "TokenType::AndAnd";
    case TokenType::Bang:
        return "TokenType::Bang";
    case TokenType::BangEqual:
        return "TokenType::BangEqual";
    case TokenType::EqualEqual:
        return "TokenType::EqualEqual";
    case TokenType::Greater:
        return "TokenType::Greater";
    case TokenType::GreaterEqual:
        return "TokenType::GreaterEqual";
    case TokenType::Less:
        return "TokenType::Less";
    case TokenType::LessEqual:
        return "TokenType::LessEqual";
    case TokenType::Minus:
        return "TokenType::Minus";
    case TokenType::OrOr:
        return "TokenType::OrOr";
    case TokenType::Plus:
        return "TokenType::Plus";
    case TokenType::Slash:
        return "TokenType::Slash";
    case TokenType::Star:
        return "TokenType::Star";
    default:
        throw std::runtime_error("Cpp backend encountered an unsupported token type");
    }
}

std::string Indent(int depth) {
    return std::string(static_cast<std::size_t>(depth) * 4, ' ');
}

class CfgCppBackendEmitter {
  public:
    explicit CfgCppBackendEmitter(const CfgProgram& program) : program_(program) {}

    std::string Emit() {
        std::ostringstream code;
        code << "#include \"common.hpp\"\n";
        code << "#include \"runtime_support.hpp\"\n\n";
#ifdef _WIN32
        code << "#include <windows.h>\n";
#endif
        code << "#include <iostream>\n";
        code << "#include <string>\n";
        code << "#include <utility>\n";
        code << "#include <vector>\n\n";
        code << "namespace {\n\n";
        code << "std::filesystem::path AuraGeneratedRuntimeBasePath() {\n";
#ifdef _WIN32
        code << "    for (DWORD buffer_size = 260; buffer_size <= 32768; buffer_size *= 2) {\n";
        code << "        std::wstring buffer(buffer_size, L'\\0');\n";
        code << "        const DWORD written_size = GetModuleFileNameW(nullptr, buffer.data(), buffer_size);\n";
        code << "        if (written_size == 0) {\n";
        code << "            break;\n";
        code << "        }\n";
        code << "        if (written_size < buffer_size - 1) {\n";
        code << "            buffer.resize(written_size);\n";
        code << "            return std::filesystem::path(buffer).parent_path().lexically_normal();\n";
        code << "        }\n";
        code << "    }\n";
#endif
        code << "    return GetWorkingDirectoryPath();\n";
        code << "}\n\n";

        for (const auto& function : program_.functions) {
            code << "Value " << MangleFunctionName(function.full_name) << "(const std::vector<Value>& args);\n";
        }
        code << "\n";

        for (const auto& function : program_.functions) {
            code << EmitFunction(function);
        }

        code << "}  // namespace\n\n";
        code << EmitMainWrapper();
        return code.str();
    }

  private:
    const CfgProgram& program_;

    std::string MangleFunctionName(const std::string& full_name) const {
        return "aura_fn_" + SanitizeIdentifier(full_name);
    }

    static std::string ValueVarName(CfgValueId value) {
        return "__aura_v" + std::to_string(value);
    }

    std::string EmitFunction(const CfgFunctionDecl& function) {
        std::ostringstream buffer;
        buffer << "Value " << MangleFunctionName(function.full_name) << "(const std::vector<Value>& args) {\n";
        buffer << Indent(1) << "if (args.size() != " << function.parameters.size() << ") {\n";
        buffer << Indent(2) << "throw AuraError(\"Function `" << EscapeForCppString(function.full_name) << "` expects "
               << function.parameters.size() << " arguments, but got \" + std::to_string(args.size()));\n";
        buffer << Indent(1) << "}\n";

        for (std::size_t value = 0; value < function.value_count; ++value) {
            buffer << Indent(1) << "Value " << ValueVarName(value) << " = std::monostate{};\n";
        }
        if (function.value_count > 0) {
            buffer << "\n";
        }

        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            buffer << Indent(1) << ValueVarName(function.parameters[i].value) << " = args[" << i << "];\n";
        }
        buffer << Indent(1) << "int __aura_block = " << static_cast<int>(function.entry_block) << ";\n";
        buffer << Indent(1) << "while (true) {\n";
        buffer << Indent(2) << "switch (__aura_block) {\n";

        std::unordered_map<CfgBlockId, const CfgBlock*> block_map;
        for (const auto& block : function.blocks) {
            block_map.emplace(block.id, &block);
        }

        for (const auto& block : function.blocks) {
            buffer << Indent(2) << "case " << static_cast<int>(block.id) << ": {\n";
            for (const auto& instruction : block.instructions) {
                buffer << EmitInstruction(instruction, 3);
            }
            buffer << EmitTerminator(block.terminator, block_map, 3);
            buffer << Indent(2) << "}\n";
        }

        buffer << Indent(2) << "default:\n";
        buffer << Indent(3) << "throw AuraError(\"Internal error: CFG backend reached an invalid block\");\n";
        buffer << Indent(2) << "}\n";
        buffer << Indent(1) << "}\n";
        buffer << "}\n\n";
        return buffer.str();
    }

    std::string EmitInstruction(const CfgInstruction& instruction, int depth) {
        std::ostringstream buffer;
        const std::string result = ValueVarName(instruction.result);
        const std::string indent = Indent(depth);

        switch (instruction.kind) {
        case CfgInstructionKind::Literal:
            buffer << indent << result << " = " << FormatLiteralValue(instruction.literal_value) << ";\n";
            return buffer.str();
        case CfgInstructionKind::Copy:
            buffer << indent << result << " = " << ValueVarName(instruction.inputs.at(0)) << ";\n";
            return buffer.str();
        case CfgInstructionKind::Drop:
            buffer << indent << ValueVarName(instruction.inputs.at(0)) << " = std::monostate{};\n";
            return buffer.str();
        case CfgInstructionKind::ArrayLiteral:
            buffer << indent << result << " = AuraMakeArrayLiteral(\"" << EscapeForCppString(instruction.name)
                   << "\", std::vector<Value>{";
            for (std::size_t i = 0; i < instruction.inputs.size(); ++i) {
                if (i > 0) {
                    buffer << ", ";
                }
                buffer << ValueVarName(instruction.inputs[i]);
            }
            buffer << "});\n";
            return buffer.str();
        case CfgInstructionKind::StructLiteral:
            buffer << indent << result << " = AuraMakeStructLiteral(\"" << EscapeForCppString(instruction.name)
                   << "\", std::vector<std::pair<std::string, Value>>{";
            for (std::size_t i = 0; i < instruction.fields.size(); ++i) {
                if (i > 0) {
                    buffer << ", ";
                }
                buffer << "{\"" << EscapeForCppString(instruction.fields[i].name) << "\", "
                       << ValueVarName(instruction.fields[i].value) << "}";
            }
            buffer << "});\n";
            return buffer.str();
        case CfgInstructionKind::Unary:
            buffer << indent << result << " = AuraEvaluateUnary(" << TokenTypeCode(instruction.token) << ", "
                   << ValueVarName(instruction.inputs.at(0)) << ");\n";
            return buffer.str();
        case CfgInstructionKind::Binary:
            buffer << indent << result << " = AuraEvaluateBinary(" << TokenTypeCode(instruction.token) << ", "
                   << ValueVarName(instruction.inputs.at(0)) << ", " << ValueVarName(instruction.inputs.at(1))
                   << ");\n";
            return buffer.str();
        case CfgInstructionKind::Range:
            buffer << indent << result << " = AuraMakeIntRange(" << ValueVarName(instruction.inputs.at(0)) << ", "
                   << ValueVarName(instruction.inputs.at(1)) << ");\n";
            return buffer.str();
        case CfgInstructionKind::Call:
            buffer << indent << result << " = " << EmitCall(instruction) << ";\n";
            return buffer.str();
        case CfgInstructionKind::GetField:
            buffer << indent << result << " = AuraGetStructField(" << ValueVarName(instruction.inputs.at(0)) << ", \""
                   << EscapeForCppString(instruction.name) << "\");\n";
            return buffer.str();
        case CfgInstructionKind::GetIndex:
            buffer << indent << result << " = "
                   << (instruction.name == "String" ? "AuraIndexString" : "AuraIndexArray") << "("
                   << ValueVarName(instruction.inputs.at(0)) << ", " << ValueVarName(instruction.inputs.at(1))
                   << ");\n";
            return buffer.str();
        case CfgInstructionKind::GetSlice: {
            const std::string start = instruction.has_start ? ValueVarName(instruction.inputs.at(1)) : "Value{std::monostate{}}";
            const std::string end = instruction.has_end ? ValueVarName(instruction.inputs.back()) : "Value{std::monostate{}}";
            buffer << indent << result << " = "
                   << (instruction.name == "String" ? "AuraSliceString" : "AuraSliceArray") << "("
                   << ValueVarName(instruction.inputs.at(0)) << ", "
                   << (instruction.has_start ? "true" : "false") << ", " << start << ", "
                   << (instruction.has_end ? "true" : "false") << ", " << end << ");\n";
            return buffer.str();
        }
        case CfgInstructionKind::AssignField:
            buffer << indent << result << " = AuraAssignField(" << ValueVarName(instruction.inputs.at(0)) << ", \""
                   << EscapeForCppString(instruction.name) << "\", " << ValueVarName(instruction.inputs.at(1))
                   << ");\n";
            return buffer.str();
        case CfgInstructionKind::AssignIndex:
            buffer << indent << result << " = AuraAssignIndex(" << ValueVarName(instruction.inputs.at(0)) << ", "
                   << ValueVarName(instruction.inputs.at(1)) << ", " << ValueVarName(instruction.inputs.at(2))
                   << ");\n";
            return buffer.str();
        }

        throw std::runtime_error("Cpp backend encountered an unknown instruction kind");
    }

    std::string EmitCall(const CfgInstruction& instruction) const {
        if (instruction.call_kind == IrCallKind::Builtin) {
            switch (instruction.builtin_kind) {
            case IrBuiltinKind::Print:
                return "AuraBuiltinPrint(" + EmitValueVector(instruction.inputs) + ")";
            case IrBuiltinKind::Len:
                return "AuraBuiltinLen(" + ValueVarName(instruction.inputs.at(0)) + ")";
            case IrBuiltinKind::Push:
                return "AuraBuiltinPush(" + ValueVarName(instruction.inputs.at(0)) + ", " +
                       ValueVarName(instruction.inputs.at(1)) + ")";
            case IrBuiltinKind::Pop:
                return "AuraBuiltinPop(" + ValueVarName(instruction.inputs.at(0)) + ")";
            case IrBuiltinKind::Insert:
                return "AuraBuiltinInsert(" + ValueVarName(instruction.inputs.at(0)) + ", " +
                       ValueVarName(instruction.inputs.at(1)) + ", " + ValueVarName(instruction.inputs.at(2)) + ")";
            case IrBuiltinKind::RemoveAt:
                return "AuraBuiltinRemoveAt(" + ValueVarName(instruction.inputs.at(0)) + ", " +
                       ValueVarName(instruction.inputs.at(1)) + ")";
            case IrBuiltinKind::Clear:
                return "AuraBuiltinClear(" + ValueVarName(instruction.inputs.at(0)) + ")";
            case IrBuiltinKind::Contains:
                return "AuraBuiltinContains(" + ValueVarName(instruction.inputs.at(0)) + ", " +
                       ValueVarName(instruction.inputs.at(1)) + ")";
            case IrBuiltinKind::StartsWith:
                return "AuraBuiltinStartsWith(" + ValueVarName(instruction.inputs.at(0)) + ", " +
                       ValueVarName(instruction.inputs.at(1)) + ")";
            case IrBuiltinKind::EndsWith:
                return "AuraBuiltinEndsWith(" + ValueVarName(instruction.inputs.at(0)) + ", " +
                       ValueVarName(instruction.inputs.at(1)) + ")";
            case IrBuiltinKind::Join:
                return "AuraBuiltinJoin(" + ValueVarName(instruction.inputs.at(0)) + ", " +
                       ValueVarName(instruction.inputs.at(1)) + ")";
            case IrBuiltinKind::FileExists:
                return "AuraBuiltinFileExists(" + ValueVarName(instruction.inputs.at(0)) + ", \"" +
                       EscapeForCppString(instruction.location.file_path) + "\")";
            case IrBuiltinKind::ReadText:
                return "AuraBuiltinReadText(" + ValueVarName(instruction.inputs.at(0)) + ", \"" +
                       EscapeForCppString(instruction.location.file_path) + "\")";
            case IrBuiltinKind::WriteText:
                return "AuraBuiltinWriteText(" + ValueVarName(instruction.inputs.at(0)) + ", " +
                       ValueVarName(instruction.inputs.at(1)) + ", \"" +
                       EscapeForCppString(instruction.location.file_path) + "\")";
            case IrBuiltinKind::AppendText:
                return "AuraBuiltinAppendText(" + ValueVarName(instruction.inputs.at(0)) + ", " +
                       ValueVarName(instruction.inputs.at(1)) + ", \"" +
                       EscapeForCppString(instruction.location.file_path) + "\")";
            case IrBuiltinKind::Abs:
                return "AuraBuiltinAbs(" + ValueVarName(instruction.inputs.at(0)) + ")";
            case IrBuiltinKind::Min:
                return "AuraBuiltinMin(" + ValueVarName(instruction.inputs.at(0)) + ", " +
                       ValueVarName(instruction.inputs.at(1)) + ")";
            case IrBuiltinKind::Max:
                return "AuraBuiltinMax(" + ValueVarName(instruction.inputs.at(0)) + ", " +
                       ValueVarName(instruction.inputs.at(1)) + ")";
            case IrBuiltinKind::Pow:
                return "AuraBuiltinPow(" + ValueVarName(instruction.inputs.at(0)) + ", " +
                       ValueVarName(instruction.inputs.at(1)) + ")";
            case IrBuiltinKind::None:
                break;
            }
        }

        return MangleFunctionName(instruction.name) + "(" + EmitValueVector(instruction.inputs) + ")";
    }

    std::string EmitValueVector(const std::vector<CfgValueId>& arguments) const {
        std::ostringstream buffer;
        buffer << "std::vector<Value>{";
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (i > 0) {
                buffer << ", ";
            }
            buffer << ValueVarName(arguments[i]);
        }
        buffer << "}";
        return buffer.str();
    }

    std::string EmitAssignTargetArguments(const CfgBlockTarget& target,
                                          const std::unordered_map<CfgBlockId, const CfgBlock*>& block_map,
                                          int depth) const {
        const auto block_it = block_map.find(target.block);
        if (block_it == block_map.end()) {
            throw std::runtime_error("Cpp backend could not resolve a target block");
        }

        const CfgBlock& block = *block_it->second;
        if (block.parameters.size() != target.arguments.size()) {
            throw std::runtime_error("Cpp backend found mismatched block arguments");
        }

        std::ostringstream buffer;
        const std::string indent = Indent(depth);
        std::vector<CfgValueId> drops;
        for (std::size_t i = 0; i < block.parameters.size(); ++i) {
            buffer << indent << ValueVarName(block.parameters[i].value) << " = "
                   << ValueVarName(target.arguments[i]) << ";\n";
            if (!block.parameters[i].name.empty() && block.parameters[i].name.rfind("__", 0) != 0 &&
                block.parameters[i].value != target.arguments[i] &&
                std::find(drops.begin(), drops.end(), target.arguments[i]) == drops.end()) {
                drops.push_back(target.arguments[i]);
            }
        }
        for (const CfgValueId drop_value : drops) {
            buffer << indent << ValueVarName(drop_value) << " = std::monostate{};\n";
        }
        return buffer.str();
    }

    std::string EmitTerminator(const CfgTerminator& terminator,
                               const std::unordered_map<CfgBlockId, const CfgBlock*>& block_map,
                               int depth) const {
        std::ostringstream buffer;
        const std::string indent = Indent(depth);

        switch (terminator.kind) {
        case CfgTerminatorKind::None:
            buffer << indent << "throw AuraError(\"Internal error: CFG block has no terminator\");\n";
            return buffer.str();
        case CfgTerminatorKind::Jump:
            buffer << EmitAssignTargetArguments(terminator.target, block_map, depth);
            buffer << indent << "__aura_block = " << static_cast<int>(terminator.target.block) << ";\n";
            buffer << indent << "continue;\n";
            return buffer.str();
        case CfgTerminatorKind::Branch:
            buffer << indent << "if (AuraExpectBool(" << ValueVarName(*terminator.condition)
                   << ", \"branch condition\")) {\n";
            buffer << EmitAssignTargetArguments(terminator.true_target, block_map, depth + 1);
            buffer << Indent(depth + 1) << "__aura_block = " << static_cast<int>(terminator.true_target.block)
                   << ";\n";
            buffer << Indent(depth + 1) << "continue;\n";
            buffer << indent << "} else {\n";
            buffer << EmitAssignTargetArguments(terminator.false_target, block_map, depth + 1);
            buffer << Indent(depth + 1) << "__aura_block = " << static_cast<int>(terminator.false_target.block)
                   << ";\n";
            buffer << Indent(depth + 1) << "continue;\n";
            buffer << indent << "}\n";
            return buffer.str();
        case CfgTerminatorKind::Return:
            if (terminator.return_value.has_value()) {
                buffer << indent << "return " << ValueVarName(*terminator.return_value) << ";\n";
            } else {
                buffer << indent << "return std::monostate{};\n";
            }
            return buffer.str();
        }

        throw std::runtime_error("Cpp backend encountered an unknown terminator");
    }

    std::string EmitMainWrapper() const {
        const bool has_main = std::any_of(program_.functions.begin(), program_.functions.end(), [](const CfgFunctionDecl& function) {
            return function.full_name == "main";
        });

        std::ostringstream code;
        code << "int main() {\n";
        code << Indent(1) << "try {\n";
        code << Indent(2) << "SetRuntimeBasePath(AuraGeneratedRuntimeBasePath());\n";
        if (has_main) {
            code << Indent(2) << "const Value result = " << MangleFunctionName("main") << "(std::vector<Value>{});\n";
            code << Indent(2) << "if (!std::holds_alternative<std::monostate>(result)) {\n";
            code << Indent(3) << "std::cout << \"=== Program Result ===\\n\";\n";
            code << Indent(3) << "std::cout << ValueToString(result) << '\\n';\n";
            code << Indent(2) << "}\n";
        } else {
            code << Indent(2) << "std::cout << \"No main function was found.\\n\";\n";
        }
        code << Indent(2) << "return 0;\n";
        code << Indent(1) << "} catch (const AuraError& error) {\n";
        code << Indent(2) << "std::cerr << \"Aura error: \" << error.what() << '\\n';\n";
        code << Indent(2) << "return 1;\n";
        code << Indent(1) << "} catch (const std::exception& error) {\n";
        code << Indent(2) << "std::cerr << \"Unexpected error: \" << error.what() << '\\n';\n";
        code << Indent(2) << "return 1;\n";
        code << Indent(1) << "}\n";
        code << "}\n";
        return code.str();
    }
};

}  // namespace

std::string GenerateCppBackendSource(const CfgProgram& program) {
    return CfgCppBackendEmitter(program).Emit();
}
