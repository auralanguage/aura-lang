#pragma once

#include "ast.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

class ModuleLoader {
  public:
    ModuleLoader() = default;
    explicit ModuleLoader(std::unordered_map<std::string, std::string> embedded_sources);

    Program LoadProgram(const std::string& entry_path);
    const std::unordered_map<std::string, std::string>& LoadedSources() const;
    const std::string& EntryPath() const;

  private:
    std::unordered_map<std::string, std::string> embedded_sources_;
    std::unordered_map<std::string, std::string> loaded_sources_;
    std::unordered_set<std::string> loaded_paths_;
    std::unordered_set<std::string> loading_paths_;
    std::string entry_path_;

    Program LoadProgramFromPath(const std::filesystem::path& path);
    std::string ReadFile(const std::filesystem::path& path) const;
    static std::filesystem::path NormalizePath(const std::filesystem::path& path);
    static void AppendProgram(Program& target, Program&& source);
};
