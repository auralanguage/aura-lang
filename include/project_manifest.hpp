#pragma once

#include <filesystem>
#include <optional>
#include <string>

struct ProjectManifest {
    std::filesystem::path manifest_path;
    std::filesystem::path project_root;
    std::string name;
    std::string version;
    std::filesystem::path entry_relative_path;
    std::filesystem::path entry_path;
    std::string build_backend;
    std::string build_compiler;
    bool has_release_profile = false;
    std::string release_build_backend;
    std::string release_build_compiler;
    std::filesystem::path test_manifest_relative_path;
    std::filesystem::path test_manifest_path;
    std::filesystem::path test_executable_relative_path;
    std::filesystem::path test_executable_path;
    std::filesystem::path output_relative_path;
    std::filesystem::path output_path;
    std::filesystem::path release_output_relative_path;
    std::filesystem::path release_output_path;
};

std::optional<ProjectManifest> TryLoadProjectManifest(const std::filesystem::path& start_directory);
ProjectManifest LoadProjectManifest(const std::filesystem::path& start_directory);
ProjectManifest ParseProjectManifest(const std::filesystem::path& manifest_path);
std::filesystem::path CreateAuraProject(const std::filesystem::path& target_directory);
