#include "test_tool.hpp"

#include "cfg_ir.hpp"
#include "common.hpp"
#include "frontend_pipeline.hpp"
#include "ir.hpp"
#include "ir_verify.hpp"
#include "lowering_pipeline.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {

enum class TestCaseKind {
    Process,
    Lowering,
    Verifier
};

enum class VerifierTarget {
    Ir,
    Cfg
};

struct JsonValue {
    enum class Kind {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Kind kind = Kind::Null;
    bool bool_value = false;
    long long number_value = 0;
    std::string string_value;
    std::vector<JsonValue> array_value;
    std::unordered_map<std::string, JsonValue> object_value;
};

class JsonParser {
  public:
    explicit JsonParser(std::string text) : text_(std::move(text)) {}

    JsonValue Parse() {
        SkipWhitespace();
        JsonValue value = ParseValue();
        SkipWhitespace();
        if (!IsAtEnd()) {
            throw AuraError("Unexpected trailing JSON content");
        }
        return value;
    }

  private:
    JsonValue ParseValue() {
        if (IsAtEnd()) {
            throw AuraError("Unexpected end of JSON input");
        }

        switch (Peek()) {
        case '{':
            return ParseObject();
        case '[':
            return ParseArray();
        case '"':
            return MakeString(ParseString());
        case 't':
            ConsumeLiteral("true");
            return MakeBool(true);
        case 'f':
            ConsumeLiteral("false");
            return MakeBool(false);
        case 'n':
            ConsumeLiteral("null");
            return JsonValue{};
        default:
            if (Peek() == '-' || std::isdigit(static_cast<unsigned char>(Peek()))) {
                return MakeNumber(ParseNumber());
            }
            throw AuraError("Unexpected JSON token");
        }
    }

    JsonValue ParseObject() {
        Consume('{');
        JsonValue value;
        value.kind = JsonValue::Kind::Object;

        SkipWhitespace();
        if (TryConsume('}')) {
            return value;
        }

        while (true) {
            SkipWhitespace();
            if (Peek() != '"') {
                throw AuraError("Expected a JSON object key");
            }

            const std::string key = ParseString();
            SkipWhitespace();
            Consume(':');
            SkipWhitespace();
            value.object_value.emplace(key, ParseValue());
            SkipWhitespace();

            if (TryConsume('}')) {
                return value;
            }

            Consume(',');
            SkipWhitespace();
        }
    }

    JsonValue ParseArray() {
        Consume('[');
        JsonValue value;
        value.kind = JsonValue::Kind::Array;

        SkipWhitespace();
        if (TryConsume(']')) {
            return value;
        }

        while (true) {
            SkipWhitespace();
            value.array_value.push_back(ParseValue());
            SkipWhitespace();

            if (TryConsume(']')) {
                return value;
            }

            Consume(',');
            SkipWhitespace();
        }
    }

    std::string ParseString() {
        Consume('"');

        std::ostringstream buffer;
        while (!IsAtEnd()) {
            const char c = Advance();
            if (c == '"') {
                return buffer.str();
            }
            if (c == '\\') {
                if (IsAtEnd()) {
                    throw AuraError("Invalid JSON string escape");
                }

                switch (const char escaped = Advance()) {
                case '"':
                case '\\':
                case '/':
                    buffer << escaped;
                    break;
                case 'b':
                    buffer << '\b';
                    break;
                case 'f':
                    buffer << '\f';
                    break;
                case 'n':
                    buffer << '\n';
                    break;
                case 'r':
                    buffer << '\r';
                    break;
                case 't':
                    buffer << '\t';
                    break;
                default:
                    throw AuraError("Unsupported JSON string escape");
                }
                continue;
            }

            if (static_cast<unsigned char>(c) < 32) {
                throw AuraError("JSON strings cannot contain control characters");
            }

            buffer << c;
        }

        throw AuraError("Unterminated JSON string");
    }

    long long ParseNumber() {
        const std::size_t start = index_;
        if (Peek() == '-') {
            Advance();
        }
        if (IsAtEnd() || !std::isdigit(static_cast<unsigned char>(Peek()))) {
            throw AuraError("Invalid JSON number");
        }
        while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
            Advance();
        }
        return std::stoll(text_.substr(start, index_ - start));
    }

    void ConsumeLiteral(std::string_view literal) {
        for (const char expected : literal) {
            Consume(expected);
        }
    }

    void SkipWhitespace() {
        while (!IsAtEnd() && std::isspace(static_cast<unsigned char>(Peek()))) {
            ++index_;
        }
    }

    void Consume(char expected) {
        if (IsAtEnd() || text_[index_] != expected) {
            throw AuraError(std::string("Expected JSON token `") + expected + "`");
        }
        ++index_;
    }

    bool TryConsume(char expected) {
        if (!IsAtEnd() && text_[index_] == expected) {
            ++index_;
            return true;
        }
        return false;
    }

    char Peek() const {
        return text_[index_];
    }

    char Advance() {
        return text_[index_++];
    }

    bool IsAtEnd() const {
        return index_ >= text_.size();
    }

    static JsonValue MakeString(std::string value) {
        JsonValue result;
        result.kind = JsonValue::Kind::String;
        result.string_value = std::move(value);
        return result;
    }

    static JsonValue MakeBool(bool value) {
        JsonValue result;
        result.kind = JsonValue::Kind::Bool;
        result.bool_value = value;
        return result;
    }

    static JsonValue MakeNumber(long long value) {
        JsonValue result;
        result.kind = JsonValue::Kind::Number;
        result.number_value = value;
        return result;
    }

    std::string text_;
    std::size_t index_ = 0;
};

struct TestCase {
    TestCaseKind kind = TestCaseKind::Process;
    std::string name;
    std::optional<std::string> input_path;
    bool dump_ir = false;
    bool dump_cfg = false;
    std::optional<VerifierTarget> verifier_target;
    std::optional<std::string> verifier_scenario;
    std::optional<std::string> executable;
    std::optional<std::string> working_directory;
    std::vector<std::string> args;
    int exit_code = 0;
    bool compare_cpp_backend = false;
    std::optional<std::string> stdout_exact;
    std::optional<std::string> stderr_exact;
    std::vector<std::string> stdout_contains;
    std::vector<std::string> stderr_contains;
    std::vector<std::string> stdout_not_contains;
    std::vector<std::string> stderr_not_contains;
};

struct ProcessResult {
    int exit_code = 0;
    std::string stdout_text;
    std::string stderr_text;
};

TestCaseKind ParseTestCaseKind(const std::string& raw_kind) {
    if (raw_kind == "process") {
        return TestCaseKind::Process;
    }
    if (raw_kind == "lowering") {
        return TestCaseKind::Lowering;
    }
    if (raw_kind == "verifier") {
        return TestCaseKind::Verifier;
    }

    throw AuraError("Manifest field `kind` must be `process`, `lowering`, or `verifier`");
}

VerifierTarget ParseVerifierTarget(const std::string& raw_target) {
    if (raw_target == "ir") {
        return VerifierTarget::Ir;
    }
    if (raw_target == "cfg") {
        return VerifierTarget::Cfg;
    }

    throw AuraError("Manifest field `verifier` must be `ir` or `cfg`");
}

std::string ReadTextFile(const fs::path& path) {
    std::FILE* input = _wfopen(path.c_str(), L"rb");
    if (input == nullptr) {
        throw AuraError("Could not open file: " + PathToDisplayString(path.filename()));
    }

    std::string text;
    char chunk[4096];
    while (true) {
        const std::size_t read_count = std::fread(chunk, 1, sizeof(chunk), input);
        if (read_count > 0) {
            text.append(chunk, read_count);
        }
        if (read_count < sizeof(chunk)) {
            break;
        }
    }

    std::fclose(input);
    return text;
}

std::string NormalizeText(std::string text) {
    std::string normalized;
    normalized.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            normalized.push_back('\n');
        } else {
            normalized.push_back(text[i]);
        }
    }

    while (!normalized.empty() && normalized.back() == '\n') {
        normalized.pop_back();
    }

    return normalized;
}

const JsonValue& GetRequiredField(const std::unordered_map<std::string, JsonValue>& object,
                                  const char* key,
                                  JsonValue::Kind expected_kind) {
    const auto it = object.find(key);
    if (it == object.end()) {
        throw AuraError(std::string("Manifest is missing required field `") + key + "`");
    }
    if (it->second.kind != expected_kind) {
        throw AuraError(std::string("Manifest field `") + key + "` has the wrong JSON type");
    }
    return it->second;
}

std::optional<std::reference_wrapper<const JsonValue>> FindField(const std::unordered_map<std::string, JsonValue>& object,
                                                                 const char* key,
                                                                 JsonValue::Kind expected_kind) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return std::nullopt;
    }
    if (it->second.kind != expected_kind) {
        throw AuraError(std::string("Manifest field `") + key + "` has the wrong JSON type");
    }
    return std::cref(it->second);
}

std::vector<std::string> ReadStringArray(const JsonValue& value, const std::string& key) {
    std::vector<std::string> result;
    result.reserve(value.array_value.size());
    for (const JsonValue& entry : value.array_value) {
        if (entry.kind != JsonValue::Kind::String) {
            throw AuraError("Manifest array `" + key + "` only accepts string values");
        }
        result.push_back(entry.string_value);
    }
    return result;
}

std::vector<TestCase> ParseManifest(const fs::path& manifest_path) {
    const JsonValue root = JsonParser(ReadTextFile(manifest_path)).Parse();
    if (root.kind != JsonValue::Kind::Object) {
        throw AuraError("Manifest root must be a JSON object");
    }

    const JsonValue& cases_value = GetRequiredField(root.object_value, "cases", JsonValue::Kind::Array);
    std::vector<TestCase> cases;
    cases.reserve(cases_value.array_value.size());

    for (const JsonValue& case_value : cases_value.array_value) {
        if (case_value.kind != JsonValue::Kind::Object) {
            throw AuraError("Each manifest case must be a JSON object");
        }

        TestCase test_case;
        if (const auto kind = FindField(case_value.object_value, "kind", JsonValue::Kind::String)) {
            test_case.kind = ParseTestCaseKind(kind->get().string_value);
        }
        test_case.name = GetRequiredField(case_value.object_value, "name", JsonValue::Kind::String).string_value;
        test_case.exit_code =
            static_cast<int>(GetRequiredField(case_value.object_value, "exit_code", JsonValue::Kind::Number).number_value);

        if (test_case.kind == TestCaseKind::Process) {
            test_case.args =
                ReadStringArray(GetRequiredField(case_value.object_value, "args", JsonValue::Kind::Array), "args");
        } else if (test_case.kind == TestCaseKind::Lowering) {
            test_case.input_path =
                GetRequiredField(case_value.object_value, "input", JsonValue::Kind::String).string_value;
            if (const auto dump_ir = FindField(case_value.object_value, "dump_ir", JsonValue::Kind::Bool)) {
                test_case.dump_ir = dump_ir->get().bool_value;
            }
            if (const auto dump_cfg = FindField(case_value.object_value, "dump_cfg", JsonValue::Kind::Bool)) {
                test_case.dump_cfg = dump_cfg->get().bool_value;
            }
        } else {
            test_case.verifier_target = ParseVerifierTarget(
                GetRequiredField(case_value.object_value, "verifier", JsonValue::Kind::String).string_value);
            test_case.verifier_scenario =
                GetRequiredField(case_value.object_value, "scenario", JsonValue::Kind::String).string_value;
        }

        if (const auto executable = FindField(case_value.object_value, "executable", JsonValue::Kind::String)) {
            test_case.executable = executable->get().string_value;
        }
        if (const auto working_directory = FindField(case_value.object_value, "working_directory", JsonValue::Kind::String)) {
            test_case.working_directory = working_directory->get().string_value;
        }
        if (const auto compare_cpp_backend = FindField(case_value.object_value, "compare_cpp_backend", JsonValue::Kind::Bool)) {
            test_case.compare_cpp_backend = compare_cpp_backend->get().bool_value;
        }
        if (const auto stdout_exact = FindField(case_value.object_value, "stdout_exact", JsonValue::Kind::String)) {
            test_case.stdout_exact = stdout_exact->get().string_value;
        }
        if (const auto stderr_exact = FindField(case_value.object_value, "stderr_exact", JsonValue::Kind::String)) {
            test_case.stderr_exact = stderr_exact->get().string_value;
        }
        if (const auto stdout_contains = FindField(case_value.object_value, "stdout_contains", JsonValue::Kind::Array)) {
            test_case.stdout_contains = ReadStringArray(stdout_contains->get(), "stdout_contains");
        }
        if (const auto stderr_contains = FindField(case_value.object_value, "stderr_contains", JsonValue::Kind::Array)) {
            test_case.stderr_contains = ReadStringArray(stderr_contains->get(), "stderr_contains");
        }
        if (const auto stdout_not_contains =
                FindField(case_value.object_value, "stdout_not_contains", JsonValue::Kind::Array)) {
            test_case.stdout_not_contains = ReadStringArray(stdout_not_contains->get(), "stdout_not_contains");
        }
        if (const auto stderr_not_contains =
                FindField(case_value.object_value, "stderr_not_contains", JsonValue::Kind::Array)) {
            test_case.stderr_not_contains = ReadStringArray(stderr_not_contains->get(), "stderr_not_contains");
        }

        cases.push_back(std::move(test_case));
    }

    return cases;
}

std::wstring ToWide(std::string_view text) {
    std::wstring result;
    result.reserve(text.size());
    for (const unsigned char c : text) {
        result.push_back(static_cast<wchar_t>(c));
    }
    return result;
}

std::wstring QuoteWindowsArgument(const std::wstring& argument) {
    if (argument.find_first_of(L" \t\"") == std::wstring::npos) {
        return argument;
    }

    std::wstring quoted = L"\"";
    std::size_t backslash_count = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++backslash_count;
            continue;
        }

        if (c == L'"') {
            quoted.append(backslash_count * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslash_count = 0;
            continue;
        }

        if (backslash_count > 0) {
            quoted.append(backslash_count, L'\\');
            backslash_count = 0;
        }
        quoted.push_back(c);
    }

    if (backslash_count > 0) {
        quoted.append(backslash_count * 2, L'\\');
    }
    quoted.push_back(L'"');
    return quoted;
}

std::wstring BuildCommandLine(const fs::path& executable_path, const std::vector<std::string>& args) {
    std::wostringstream command_line;
    command_line << QuoteWindowsArgument(executable_path.native());
    for (const std::string& arg : args) {
        command_line << L' ' << QuoteWindowsArgument(ToWide(arg));
    }
    return command_line.str();
}

std::string SanitizeFileName(std::string text) {
    for (char& c : text) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
            c = '_';
        }
    }
    return text;
}

ProcessResult RunProcess(const fs::path& executable_path,
                         const std::vector<std::string>& args,
                         const fs::path& artifact_directory,
                         const std::string& case_name,
                         std::size_t case_index,
                         const std::optional<fs::path>& working_directory = std::nullopt) {
    fs::create_directories(artifact_directory);

    const std::string prefix =
        SanitizeFileName(case_name) + "_" + std::to_string(case_index) + "_" + std::to_string(GetCurrentProcessId());
    const fs::path stdout_path = artifact_directory / (prefix + "_stdout.txt");
    const fs::path stderr_path = artifact_directory / (prefix + "_stderr.txt");

    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    HANDLE stdout_file =
        CreateFileW(stdout_path.c_str(),
                    GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    &security_attributes,
                    CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr);
    if (stdout_file == INVALID_HANDLE_VALUE) {
        throw AuraError("Could not create stdout capture file for test case `" + case_name + "`");
    }

    HANDLE stderr_file =
        CreateFileW(stderr_path.c_str(),
                    GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    &security_attributes,
                    CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr);
    if (stderr_file == INVALID_HANDLE_VALUE) {
        CloseHandle(stdout_file);
        throw AuraError("Could not create stderr capture file for test case `" + case_name + "`");
    }

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = stdout_file;
    startup_info.hStdError = stderr_file;

    PROCESS_INFORMATION process_info{};
    std::wstring command_line = BuildCommandLine(executable_path, args);
    std::vector<wchar_t> mutable_command_line(command_line.begin(), command_line.end());
    mutable_command_line.push_back(L'\0');
    std::wstring current_directory_text;
    LPCWSTR current_directory = nullptr;
    if (working_directory.has_value()) {
        current_directory_text = working_directory->native();
        current_directory = current_directory_text.c_str();
    }

    const std::wstring executable_text = executable_path.native();
    const BOOL created = CreateProcessW(executable_text.c_str(),
                                        mutable_command_line.data(),
                                        nullptr,
                                        nullptr,
                                        TRUE,
                                        0,
                                        nullptr,
                                        current_directory,
                                        &startup_info,
                                        &process_info);

    CloseHandle(stdout_file);
    CloseHandle(stderr_file);

    if (!created) {
        throw AuraError("Could not launch test executable `" + PathToDisplayString(executable_path.filename()) + "`");
    }

    WaitForSingleObject(process_info.hProcess, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);

    ProcessResult result;
    result.exit_code = static_cast<int>(exit_code);
    result.stdout_text = NormalizeText(ReadTextFile(stdout_path));
    result.stderr_text = NormalizeText(ReadTextFile(stderr_path));

    std::error_code cleanup_error;
    fs::remove(stdout_path, cleanup_error);
    cleanup_error.clear();
    fs::remove(stderr_path, cleanup_error);
    return result;
}

void AssertExact(const std::string& label, const std::string& actual, const std::string& expected) {
    if (actual != expected) {
        throw AuraError(label + " mismatch.\nExpected:\n" + expected + "\n\nActual:\n" + actual);
    }
}

void AssertContains(const std::string& label, const std::string& haystack, const std::vector<std::string>& needles) {
    for (const std::string& needle : needles) {
        if (haystack.find(needle) == std::string::npos) {
            throw AuraError(label + " does not contain `" + needle + "`.\nActual output:\n" + haystack);
        }
    }
}

void AssertNotContains(const std::string& label,
                       const std::string& haystack,
                       const std::vector<std::string>& needles) {
    for (const std::string& needle : needles) {
        if (haystack.find(needle) != std::string::npos) {
            throw AuraError(label + " unexpectedly contains `" + needle + "`.\nActual output:\n" + haystack);
        }
    }
}

std::string ExtractComparableSourcePath(const TestCase& test_case) {
    if (test_case.args.size() == 1) {
        return test_case.args[0];
    }
    if (test_case.args.size() == 2 && test_case.args[0] == "run") {
        return test_case.args[1];
    }
    throw AuraError("Case `" + test_case.name + "` uses `compare_cpp_backend`, but its args do not map to a run command");
}

std::string ReplaceAll(std::string text, const std::string& needle, const std::string& replacement) {
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        text.replace(position, needle.size(), replacement);
        position += replacement.size();
    }
    return text;
}

std::string ExpandTemplateText(std::string text, std::size_t case_index) {
    text = ReplaceAll(std::move(text), "${pid}", std::to_string(GetCurrentProcessId()));
    text = ReplaceAll(std::move(text), "${case}", std::to_string(case_index));
    return text;
}

std::vector<std::string> ExpandTemplateArray(const std::vector<std::string>& values, std::size_t case_index) {
    std::vector<std::string> expanded;
    expanded.reserve(values.size());
    for (const std::string& value : values) {
        expanded.push_back(ExpandTemplateText(value, case_index));
    }
    return expanded;
}

TestCase ExpandTestCase(const TestCase& test_case, std::size_t case_index) {
    TestCase expanded = test_case;
    expanded.name = ExpandTemplateText(expanded.name, case_index);
    if (expanded.input_path.has_value()) {
        expanded.input_path = ExpandTemplateText(*expanded.input_path, case_index);
    }
    if (expanded.verifier_scenario.has_value()) {
        expanded.verifier_scenario = ExpandTemplateText(*expanded.verifier_scenario, case_index);
    }
    if (expanded.executable.has_value()) {
        expanded.executable = ExpandTemplateText(*expanded.executable, case_index);
    }
    if (expanded.working_directory.has_value()) {
        expanded.working_directory = ExpandTemplateText(*expanded.working_directory, case_index);
    }
    expanded.args = ExpandTemplateArray(expanded.args, case_index);
    if (expanded.stdout_exact.has_value()) {
        expanded.stdout_exact = ExpandTemplateText(*expanded.stdout_exact, case_index);
    }
    if (expanded.stderr_exact.has_value()) {
        expanded.stderr_exact = ExpandTemplateText(*expanded.stderr_exact, case_index);
    }
    expanded.stdout_contains = ExpandTemplateArray(expanded.stdout_contains, case_index);
    expanded.stderr_contains = ExpandTemplateArray(expanded.stderr_contains, case_index);
    expanded.stdout_not_contains = ExpandTemplateArray(expanded.stdout_not_contains, case_index);
    expanded.stderr_not_contains = ExpandTemplateArray(expanded.stderr_not_contains, case_index);
    return expanded;
}

fs::path ResolveCasePath(const fs::path& path, const std::optional<fs::path>& working_directory) {
    if (path.is_absolute()) {
        return path.lexically_normal();
    }
    if (working_directory.has_value()) {
        return (*working_directory / path).lexically_normal();
    }
    return (GetWorkingDirectoryPath() / path).lexically_normal();
}

fs::path ResolveExecutablePath(const fs::path& path, const std::optional<fs::path>& working_directory) {
    if (path.is_absolute()) {
        return path.lexically_normal();
    }

    const fs::path workspace_candidate = (GetWorkingDirectoryPath() / path).lexically_normal();
    if (!working_directory.has_value()) {
        return workspace_candidate;
    }

    const fs::path working_candidate = (*working_directory / path).lexically_normal();
    std::error_code exists_error;
    if (fs::exists(working_candidate, exists_error)) {
        return working_candidate;
    }

    exists_error.clear();
    if (fs::exists(workspace_candidate, exists_error)) {
        return workspace_candidate;
    }

    return working_candidate;
}

SourceLocation MakeVerifierLocation(const std::string& scenario) {
    return SourceLocation{"<verifier:" + scenario + ">", 1, 1};
}

TypeInfo UnitType() {
    return {TypeKind::Unit, ""};
}

TypeInfo IntType() {
    return {TypeKind::Int, ""};
}

TypeInfo BoolType() {
    return {TypeKind::Bool, ""};
}

TypeInfo StringType() {
    return {TypeKind::String, ""};
}

CfgTerminator MakeReturnTerminator(const SourceLocation& location, std::optional<CfgValueId> value = std::nullopt) {
    CfgTerminator terminator;
    terminator.location = location;
    terminator.kind = CfgTerminatorKind::Return;
    terminator.return_value = value;
    return terminator;
}

CfgBlock MakeCfgBlock(CfgBlockId id, std::string label, const SourceLocation& location) {
    CfgBlock block;
    block.id = id;
    block.label = std::move(label);
    block.terminator = MakeReturnTerminator(location);
    return block;
}

CfgProgram MakeVerifierCfgProgram(const std::string& scenario) {
    const SourceLocation location = MakeVerifierLocation(scenario);

    CfgProgram program;
    CfgFunctionDecl function;
    function.location = location;
    function.name = "main";
    function.full_name = "main";
    function.return_type = UnitType();
    function.entry_block = 0;

    if (scenario == "cfg_jump_arg_count_mismatch") {
        function.value_count = 1;

        CfgBlock entry = MakeCfgBlock(0, "entry", location);
        entry.terminator.kind = CfgTerminatorKind::Jump;
        entry.terminator.target.block = 1;

        CfgBlock target = MakeCfgBlock(1, "target", location);
        target.parameters.push_back(CfgBlockParameter{0, "value", IntType()});

        function.blocks.push_back(std::move(entry));
        function.blocks.push_back(std::move(target));
    } else if (scenario == "cfg_jump_arg_type_mismatch") {
        function.value_count = 2;

        CfgBlock entry = MakeCfgBlock(0, "entry", location);
        CfgInstruction literal;
        literal.location = location;
        literal.result = 0;
        literal.type = BoolType();
        literal.kind = CfgInstructionKind::Literal;
        literal.literal_value = true;
        entry.instructions.push_back(std::move(literal));
        entry.terminator.kind = CfgTerminatorKind::Jump;
        entry.terminator.target.block = 1;
        entry.terminator.target.arguments.push_back(0);

        CfgBlock target = MakeCfgBlock(1, "target", location);
        target.parameters.push_back(CfgBlockParameter{1, "value", IntType()});

        function.blocks.push_back(std::move(entry));
        function.blocks.push_back(std::move(target));
    } else if (scenario == "cfg_undefined_value_use") {
        function.value_count = 1;

        CfgBlock entry = MakeCfgBlock(0, "entry", location);
        CfgInstruction copy;
        copy.location = location;
        copy.result = 0;
        copy.type = IntType();
        copy.kind = CfgInstructionKind::Copy;
        copy.inputs.push_back(99);
        entry.instructions.push_back(std::move(copy));

        function.blocks.push_back(std::move(entry));
    } else if (scenario == "cfg_branch_condition_not_bool") {
        function.value_count = 1;

        CfgBlock entry = MakeCfgBlock(0, "entry", location);
        CfgInstruction literal;
        literal.location = location;
        literal.result = 0;
        literal.type = IntType();
        literal.kind = CfgInstructionKind::Literal;
        literal.literal_value = static_cast<long long>(1);
        entry.instructions.push_back(std::move(literal));
        entry.terminator.kind = CfgTerminatorKind::Branch;
        entry.terminator.condition = 0;
        entry.terminator.true_target.block = 1;
        entry.terminator.false_target.block = 2;

        CfgBlock true_block = MakeCfgBlock(1, "then", location);
        CfgBlock false_block = MakeCfgBlock(2, "else", location);

        function.blocks.push_back(std::move(entry));
        function.blocks.push_back(std::move(true_block));
        function.blocks.push_back(std::move(false_block));
    } else {
        throw AuraError("Unknown CFG verifier scenario `" + scenario + "`");
    }

    program.functions.push_back(std::move(function));
    return program;
}

IrProgram MakeVerifierIrProgram(const std::string& scenario) {
    const SourceLocation location = MakeVerifierLocation(scenario);

    IrProgram program;
    IrFunctionDecl function;
    function.location = location;
    function.name = "main";
    function.full_name = "main";
    function.return_type = UnitType();

    if (scenario == "ir_for_element_type_mismatch") {
        auto iterable = std::make_unique<IrLiteralExpr>(location, StringType(), MakeStringValue("Aura"));
        auto body = std::make_unique<IrBlockStmt>(location, std::vector<std::unique_ptr<IrStmt>>{});
        function.body.push_back(std::make_unique<IrForStmt>(location,
                                                            std::nullopt,
                                                            "value",
                                                            IntType(),
                                                            std::move(iterable),
                                                            std::move(body)));
    } else if (scenario == "ir_builtin_call_user_tag") {
        std::vector<std::unique_ptr<IrExpr>> arguments;
        arguments.push_back(std::make_unique<IrLiteralExpr>(location, StringType(), MakeStringValue("hello")));
        auto call = std::make_unique<IrCallExpr>(location,
                                                 UnitType(),
                                                 IrCallKind::UserFunction,
                                                 IrBuiltinKind::Print,
                                                 "print",
                                                 std::move(arguments));
        function.body.push_back(std::make_unique<IrExprStmt>(location, std::move(call)));
    } else {
        throw AuraError("Unknown IR verifier scenario `" + scenario + "`");
    }

    program.functions.push_back(std::move(function));
    return program;
}

ProcessResult RunLoweringCase(const TestCase& test_case, const std::optional<fs::path>& working_directory) {
    ProcessResult result;

    try {
        if (!test_case.input_path.has_value()) {
            throw AuraError("Lowering test case `" + test_case.name + "` is missing an input path");
        }

        const fs::path input_path = ResolveCasePath(*test_case.input_path, working_directory);
        const FrontendResult frontend = FrontendPipeline::Analyze(input_path.string());
        const LoweringResult lowered = LoweringPipeline::Lower(frontend.program);

        std::ostringstream output;
        if (test_case.dump_ir) {
            output << FormatIrProgram(lowered.ir_program);
            if (test_case.dump_cfg) {
                output << "=== CFG SSA ===\n";
                output << FormatCfgProgram(lowered.cfg_program);
            }
        } else if (test_case.dump_cfg) {
            output << FormatCfgProgram(lowered.cfg_program);
        }

        result.exit_code = 0;
        result.stdout_text = NormalizeText(output.str());
        result.stderr_text.clear();
        return result;
    } catch (const AuraError& error) {
        result.exit_code = 1;
        result.stdout_text.clear();
        result.stderr_text = NormalizeText("Aura error: " + std::string(error.what()));
        return result;
    } catch (const std::exception& error) {
        result.exit_code = 1;
        result.stdout_text.clear();
        result.stderr_text = NormalizeText("Unexpected error: " + std::string(error.what()));
        return result;
    }
}

ProcessResult RunVerifierCase(const TestCase& test_case) {
    ProcessResult result;

    try {
        if (!test_case.verifier_target.has_value() || !test_case.verifier_scenario.has_value()) {
            throw AuraError("Verifier test case `" + test_case.name + "` is missing required verifier fields");
        }

        if (*test_case.verifier_target == VerifierTarget::Ir) {
            const IrProgram program = MakeVerifierIrProgram(*test_case.verifier_scenario);
            VerifyIrProgram(program);
        } else {
            const CfgProgram program = MakeVerifierCfgProgram(*test_case.verifier_scenario);
            VerifyCfgProgram(program);
        }

        result.exit_code = 0;
        result.stdout_text.clear();
        result.stderr_text.clear();
        return result;
    } catch (const AuraError& error) {
        result.exit_code = 1;
        result.stdout_text.clear();
        result.stderr_text = NormalizeText("Aura error: " + std::string(error.what()));
        return result;
    } catch (const std::exception& error) {
        result.exit_code = 1;
        result.stdout_text.clear();
        result.stderr_text = NormalizeText("Unexpected error: " + std::string(error.what()));
        return result;
    }
}

}  // namespace

int TestTool::Run(const TestOptions& options) {
    const fs::path manifest_path = options.manifest_path.empty() ? fs::path("tests") / "test_cases.json"
                                                                 : options.manifest_path;
    const fs::path default_executable_path = ResolveCasePath(
        options.executable_path.empty() ? fs::path("build") / "aura.exe" : options.executable_path,
        std::nullopt);
    const std::vector<TestCase> cases = ParseManifest(manifest_path);
    const fs::path artifact_directory = fs::path("build") / "test-artifacts";

    int passed = 0;
    int matched = 0;
    std::size_t case_index = 0;

    for (const TestCase& raw_test_case : cases) {
        ++case_index;
        const TestCase test_case = ExpandTestCase(raw_test_case, case_index);
        if (!options.filter.empty() && test_case.name.find(options.filter) == std::string::npos) {
            continue;
        }
        ++matched;

        std::cout << "Running " << test_case.name << "...\n";

        const std::optional<fs::path> working_directory =
            test_case.working_directory.has_value()
                ? std::optional<fs::path>((GetWorkingDirectoryPath() / *test_case.working_directory).lexically_normal())
                                                    : std::nullopt;
        const fs::path executable_path = ResolveExecutablePath(
            test_case.executable.has_value() ? fs::path(*test_case.executable) : default_executable_path,
            working_directory);
        ProcessResult result;
        if (test_case.kind == TestCaseKind::Lowering) {
            result = RunLoweringCase(test_case, working_directory);
        } else if (test_case.kind == TestCaseKind::Verifier) {
            result = RunVerifierCase(test_case);
        } else {
            result = RunProcess(executable_path,
                                test_case.args,
                                artifact_directory,
                                test_case.name,
                                case_index,
                                working_directory);
        }

        if (result.exit_code != test_case.exit_code) {
            throw AuraError("Case `" + test_case.name + "` failed: expected exit code " +
                            std::to_string(test_case.exit_code) + ", got " + std::to_string(result.exit_code));
        }
        if (test_case.stdout_exact.has_value()) {
            AssertExact("stdout for `" + test_case.name + "`",
                        result.stdout_text,
                        NormalizeText(*test_case.stdout_exact));
        }
        if (test_case.stderr_exact.has_value()) {
            AssertExact("stderr for `" + test_case.name + "`",
                        result.stderr_text,
                        NormalizeText(*test_case.stderr_exact));
        }

        AssertContains("stdout for `" + test_case.name + "`", result.stdout_text, test_case.stdout_contains);
        AssertContains("stderr for `" + test_case.name + "`", result.stderr_text, test_case.stderr_contains);
        AssertNotContains("stdout for `" + test_case.name + "`", result.stdout_text, test_case.stdout_not_contains);
        AssertNotContains("stderr for `" + test_case.name + "`", result.stderr_text, test_case.stderr_not_contains);

        if (test_case.compare_cpp_backend) {
            if (test_case.kind != TestCaseKind::Process) {
                throw AuraError("Case `" + test_case.name + "` cannot use `compare_cpp_backend` outside process mode");
            }
            if (test_case.executable.has_value()) {
                throw AuraError("Case `" + test_case.name + "` cannot use `compare_cpp_backend` with a custom executable");
            }

            const fs::path compiled_path =
                fs::path("build") / "generated-tests" / (SanitizeFileName(test_case.name) + "_parity_cpp.exe");
            const std::vector<std::string> build_args = {
                "build",
                ExtractComparableSourcePath(test_case),
                "-o",
                compiled_path.string(),
                "--backend",
                "cpp",
            };

            const ProcessResult build_result = RunProcess(executable_path,
                                                          build_args,
                                                          artifact_directory,
                                                          test_case.name + "_build_cpp",
                                                          case_index,
                                                          working_directory);
            if (build_result.exit_code != 0) {
                throw AuraError("Case `" + test_case.name + "` failed to build the comparison C++ backend executable");
            }
            if (build_result.stdout_text.find("Built executable:") == std::string::npos) {
                throw AuraError("Case `" + test_case.name + "` comparison build did not report a built executable");
            }

            const fs::path resolved_compiled_path = ResolveExecutablePath(compiled_path, working_directory);
            const ProcessResult compiled_result = RunProcess(resolved_compiled_path,
                                                             {},
                                                             artifact_directory,
                                                             test_case.name + "_cpp",
                                                             case_index,
                                                             working_directory);
            AssertExact("cpp backend exit code for `" + test_case.name + "`",
                        std::to_string(compiled_result.exit_code),
                        std::to_string(result.exit_code));
            AssertExact("cpp backend stdout for `" + test_case.name + "`", compiled_result.stdout_text, result.stdout_text);
            AssertExact("cpp backend stderr for `" + test_case.name + "`", compiled_result.stderr_text, result.stderr_text);

            std::error_code cleanup_error;
            fs::remove(resolved_compiled_path, cleanup_error);
        }

        ++passed;
    }

    if (matched == 0) {
        throw AuraError("No test cases matched the requested filter");
    }

    std::cout << "Passed " << passed << " test(s).\n";
    return 0;
}
