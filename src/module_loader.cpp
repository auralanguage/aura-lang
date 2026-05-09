#include "module_loader.hpp"

#include "lexer.hpp"
#include "parser.hpp"

#include <cstdio>
#include <sstream>
#include <utility>
// we should change this
namespace fs = std::filesystem;

ModuleLoader::ModuleLoader(std::unordered_map<std::string, std::string> embedded_sources)
    : embedded_sources_(std::move(embedded_sources)) {}

Program ModuleLoader::LoadProgram(const std::string& entry_path) {
    loaded_sources_.clear();
    loaded_paths_.clear();
    loading_paths_.clear();
    entry_path_ = PathToDisplayString(NormalizePath(entry_path));
    return LoadProgramFromPath(entry_path_);
}

const std::unordered_map<std::string, std::string>& ModuleLoader::LoadedSources() const {
    return loaded_sources_;
}

const std::string& ModuleLoader::EntryPath() const {
    return entry_path_;
}

Program ModuleLoader::LoadProgramFromPath(const fs::path& path) {
    const fs::path normalized_path = NormalizePath(path);
    const std::string key = PathToDisplayString(normalized_path);

    if (loaded_paths_.find(key) != loaded_paths_.end()) {
        return Program{};
    }

    loading_paths_.insert(key);

    const std::string source = ReadFile(normalized_path);
    loaded_sources_[key] = source;
    Lexer lexer(source, key);
    const std::vector<Token> tokens = lexer.ScanTokens();

    Parser parser(tokens);
    Program current_program = parser.ParseProgram();

    if (key != entry_path_ && !current_program.module_name.has_value()) {
        throw BuildLocationError(SourceLocation{key, 1, 1},
                                 "Imported files must declare `module name;` before exporting symbols");
    }

    Program merged_program;

    for (const auto& import_decl : current_program.imports) {
        fs::path import_path(import_decl.path);
        if (import_path.is_relative()) {
            import_path = normalized_path.parent_path() / import_path;
        }

        import_path = NormalizePath(import_path);
        const std::string import_key = PathToDisplayString(import_path);

        if (loading_paths_.find(import_key) != loading_paths_.end()) {
            throw BuildLocationError(import_decl.location, "Circular import detected: " + import_decl.path);
        }

        if (embedded_sources_.find(import_key) == embedded_sources_.end() && !fs::exists(import_path)) {
            throw BuildLocationError(import_decl.location, "Imported file was not found: " + import_decl.path);
        }

        merged_program.imports.push_back(import_decl);
        AppendProgram(merged_program, LoadProgramFromPath(import_path));
    }

    current_program.imports.clear();
    AppendProgram(merged_program, std::move(current_program));

    loading_paths_.erase(key);
    loaded_paths_.insert(key);
    return merged_program;
}

std::string ModuleLoader::ReadFile(const fs::path& path) const {
    const std::string key = PathToDisplayString(NormalizePath(path));
    const auto embedded_it = embedded_sources_.find(key);
    if (embedded_it != embedded_sources_.end()) {
        return embedded_it->second;
    }

    std::FILE* input = _wfopen(path.c_str(), L"rb");
    if (input == nullptr) {
        throw AuraError("Could not open file: " + PathToDisplayString(path));
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

fs::path ModuleLoader::NormalizePath(const fs::path& path) {
    return path.is_absolute() ? path.lexically_normal() : (GetWorkingDirectoryPath() / path).lexically_normal();
}

void ModuleLoader::AppendProgram(Program& target, Program&& source) {
    for (auto& import_decl : source.imports) {
        target.imports.push_back(std::move(import_decl));
    }

    for (auto& struct_decl : source.structs) {
        target.structs.push_back(std::move(struct_decl));
    }

    for (auto& function : source.functions) {
        target.functions.push_back(std::move(function));
    }
}
