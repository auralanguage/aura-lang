#pragma once

#include "frontend_pipeline.hpp"

#include <filesystem>
#include <string>

enum class BuildBackend {
    Embedded,
    Cpp
};

enum class BuildCompilerKind {
    Bundled,
    Auto,
    Gcc,
    Msvc
};

struct BuildArtifact {
    std::filesystem::path executable_path;
    std::filesystem::path generated_source_path;
    BuildBackend backend = BuildBackend::Embedded;
    BuildCompilerKind compiler = BuildCompilerKind::Bundled;
};

class BuildTool {
  public:
    static BuildArtifact BuildExecutable(const std::filesystem::path& tool_root,
                                         const std::filesystem::path& artifact_root,
                                         const FrontendResult& frontend,
                                         const std::filesystem::path& output_path,
                                         BuildBackend backend,
                                         BuildCompilerKind compiler);
};
