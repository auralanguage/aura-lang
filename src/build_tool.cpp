#include "build_tool.hpp"

#include "cpp_backend.hpp"
#include "packaged_app.hpp"
#include "lowering_pipeline.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// for future we can think about native compilation
// namespace fs = std::experimental::filesystem;
namespace fs = std::filesystem;

namespace {

fs::path NormalizeFrom(const fs::path& base, const fs::path& path) {
    return path.is_absolute() ? path.lexically_normal() : (base / path).lexically_normal();
}

fs::path MakeCommandPath(const fs::path& current_directory, const fs::path& path) {
    std::error_code error_code;
    const fs::path relative_path = fs::relative(path, current_directory, error_code);
    if (!error_code && !relative_path.empty()) {
        return relative_path.lexically_normal();
    }
    return path.lexically_normal();
}

fs::path FindToolRoot(fs::path current) {
    while (!current.empty()) {
        if (fs::exists(current / "include" / "common.hpp") && fs::exists(current / "src" / "common.cpp")) {
            return current;
        }
        if (current == current.root_path()) {
            break;
        }
        current = current.parent_path();
    }

    throw AuraError("Could not find the Aura project root");
}

std::string QuoteCommandArgument(const fs::path& path) {
    const std::string text = path.string();
    return "\"" + text + "\"";
}

std::string QuoteRawCommandArgument(const std::string& text) {
    return "\"" + text + "\"";
}

fs::path DefaultGeneratedSourcePath(const fs::path& project_root,
                                    const fs::path& output_path,
                                    BuildBackend backend) {
    const std::string suffix =
        backend == BuildBackend::Embedded ? "_embedded_package.bin" : "_cpp_backend.cpp";
    return project_root / "build" / "generated" / (output_path.stem().string() + suffix);
}

std::string BuildCompilerCommand(const fs::path& include_path,
                                 const std::vector<fs::path>& sources,
                                 const fs::path& output_path,
                                 const fs::path& object_directory,
                                 BuildCompilerKind compiler) {
    std::ostringstream command;

    if (compiler == BuildCompilerKind::Msvc) {
        std::string object_prefix = object_directory.string();
        std::replace(object_prefix.begin(), object_prefix.end(), '\\', '/');
        if (object_prefix.empty() || object_prefix.back() != '/') {
            object_prefix.push_back('/');
        }

        command << "cl /nologo /std:c++17 /EHsc /permissive- /utf-8 /MT /D_CRT_SECURE_NO_WARNINGS"
                   " /W4 /wd4100 /wd4127 /wd4456 /wd4457 /wd4702 /wd4996 /I"
                << QuoteCommandArgument(include_path) << " /Fo" << QuoteRawCommandArgument(object_prefix);
        for (const auto& source : sources) {
            command << ' ' << QuoteCommandArgument(source);
        }
        command << " /link /OUT:" << QuoteCommandArgument(output_path);
        return command.str();
    }

    command << "g++ -std=c++17 -Wall -Wextra -pedantic -Wno-unused-function -Wno-deprecated-declarations -I"
            << QuoteCommandArgument(include_path);
#ifdef _WIN32
    command << " -static -static-libgcc -static-libstdc++";
#endif
    for (const auto& source : sources) {
        command << ' ' << QuoteCommandArgument(source);
    }
    command << " -o " << QuoteCommandArgument(output_path);
    return command.str();
}

bool ToolExistsOnPath(const wchar_t* tool_name) {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD written = SearchPathW(nullptr, tool_name, nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (written == 0) {
        return false;
    }
    if (written < buffer.size()) {
        return true;
    }

    buffer.resize(written);
    written = SearchPathW(nullptr, tool_name, nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    return written > 0 && written < buffer.size();
#else
    (void)tool_name;
    return false;
#endif
}

std::optional<fs::path> CompileWindowsIconResource(const fs::path& current_directory,
                                                   const fs::path& tool_root,
                                                   const fs::path& output_path,
                                                   BuildCompilerKind compiler) {
#ifdef _WIN32
    if (!ToolExistsOnPath(L"windres.exe") && !ToolExistsOnPath(L"windres")) {
        return std::nullopt;
    }

    const fs::path icon_resource_path = tool_root / "branding" / "aura.rc";
    const fs::path icon_file_path = tool_root / "branding" / "aura.ico";
    if (!fs::exists(icon_resource_path) || !fs::exists(icon_file_path)) {
        return std::nullopt;
    }

    fs::create_directories(output_path.parent_path());

    const std::string format = compiler == BuildCompilerKind::Msvc ? "res" : "coff";
    std::ostringstream command;
    command << "windres " << QuoteCommandArgument(MakeCommandPath(current_directory, icon_resource_path))
            << " -I " << QuoteCommandArgument(MakeCommandPath(current_directory, tool_root / "branding"))
            << " -O " << format
            << " -o " << QuoteCommandArgument(MakeCommandPath(current_directory, output_path));

    const int exit_code = std::system(command.str().c_str());
    if (exit_code != 0) {
        throw AuraError("Could not compile the Aura icon resource");
    }

    return output_path;
#else
    (void)current_directory;
    (void)tool_root;
    (void)output_path;
    (void)compiler;
    return std::nullopt;
#endif
}

BuildCompilerKind ResolveCompiler(BuildCompilerKind requested) {
#ifdef _WIN32
    if (requested == BuildCompilerKind::Auto) {
        if (ToolExistsOnPath(L"cl.exe")) {
            return BuildCompilerKind::Msvc;
        }
        if (ToolExistsOnPath(L"g++.exe") || ToolExistsOnPath(L"g++")) {
            return BuildCompilerKind::Gcc;
        }
        throw AuraError("Could not find a supported Windows C++ compiler on PATH. Install Visual Studio Build Tools or g++.");
    }

    if (requested == BuildCompilerKind::Msvc) {
        if (!ToolExistsOnPath(L"cl.exe")) {
            throw AuraError("Requested compiler `msvc`, but `cl.exe` was not found on PATH");
        }
        return BuildCompilerKind::Msvc;
    }

    if (!ToolExistsOnPath(L"g++.exe") && !ToolExistsOnPath(L"g++")) {
        throw AuraError("Requested compiler `gcc`, but `g++` was not found on PATH");
    }
    return BuildCompilerKind::Gcc;
#else
    if (requested == BuildCompilerKind::Msvc) {
        throw AuraError("Requested compiler `msvc`, but MSVC is only available on Windows");
    }
    return BuildCompilerKind::Gcc;
#endif
}

}  // namespace

BuildArtifact BuildTool::BuildExecutable(const fs::path& tool_root,
                                         const fs::path& artifact_root,
                                         const FrontendResult& frontend,
                                         const fs::path& output_path,
                                         BuildBackend backend,
                                         BuildCompilerKind requested_compiler) {
    const fs::path current_directory = GetWorkingDirectoryPath();
    const fs::path normalized_tool_root = FindToolRoot(NormalizeFrom(current_directory, tool_root));
    const fs::path normalized_artifact_root = NormalizeFrom(current_directory, artifact_root);
    const fs::path normalized_output_path = NormalizeFrom(normalized_artifact_root, output_path);

    if (!normalized_output_path.parent_path().empty()) {
        fs::create_directories(normalized_output_path.parent_path());
    }
    const fs::path generated_source_path = DefaultGeneratedSourcePath(normalized_artifact_root,
                                                                      normalized_output_path,
                                                                      backend);
    fs::create_directories(generated_source_path.parent_path());

    if (backend == BuildBackend::Embedded) {
        const EmbeddedAppPackage package{frontend.entry_path, frontend.loaded_sources};
        WriteEmbeddedAppExecutable(GetProcessExecutablePath(),
                                   normalized_output_path,
                                   package,
                                   generated_source_path);
        return BuildArtifact{normalized_output_path,
                             generated_source_path,
                             backend,
                             BuildCompilerKind::Bundled};
    }

    const LoweringResult lowered = LoweringPipeline::Lower(frontend.program);
    BuildCompilerKind compiler = ResolveCompiler(requested_compiler);

    std::ofstream output(generated_source_path, std::ios::out | std::ios::binary);
    if (!output.is_open()) {
        throw AuraError("Could not write generated build file: " + PathToDisplayString(generated_source_path));
    }
    std::vector<fs::path> compile_sources;
    output << GenerateCppBackendSource(lowered.cfg_program);
    compile_sources = {
        MakeCommandPath(current_directory, generated_source_path),
        MakeCommandPath(current_directory, normalized_tool_root / "src" / "common.cpp"),
        MakeCommandPath(current_directory, normalized_tool_root / "src" / "runtime_support.cpp"),
    };
    output.close();

    const fs::path object_directory =
        normalized_artifact_root / "build" / "generated" / (normalized_output_path.stem().string() + "_obj");
    if (compiler == BuildCompilerKind::Msvc) {
        fs::create_directories(object_directory);
    }
    if (const auto icon_resource = CompileWindowsIconResource(
            current_directory,
            normalized_tool_root,
            normalized_artifact_root / "build" / "generated" /
                (normalized_output_path.stem().string() + (compiler == BuildCompilerKind::Msvc ? "_icon.res" : "_icon.o")),
            compiler);
        icon_resource.has_value()) {
        compile_sources.push_back(MakeCommandPath(current_directory, *icon_resource));
    }

    const auto build_command = [&](BuildCompilerKind active_compiler) {
        return BuildCompilerCommand(MakeCommandPath(current_directory, normalized_tool_root / "include"),
                                    compile_sources,
                                    MakeCommandPath(current_directory, normalized_output_path),
                                    MakeCommandPath(current_directory, object_directory),
                                    active_compiler);
    };

    int exit_code = std::system(build_command(compiler).c_str());
#ifdef _WIN32
    if (exit_code != 0 && requested_compiler == BuildCompilerKind::Auto && compiler == BuildCompilerKind::Msvc &&
        (ToolExistsOnPath(L"g++.exe") || ToolExistsOnPath(L"g++"))) {
        compiler = BuildCompilerKind::Gcc;
        exit_code = std::system(build_command(compiler).c_str());
    }
#endif
    if (exit_code != 0) {
        throw AuraError("Build command failed with exit code " + std::to_string(exit_code));
    }

    return BuildArtifact{normalized_output_path, generated_source_path, backend, compiler};
}
