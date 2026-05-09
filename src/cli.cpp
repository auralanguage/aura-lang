#include "cli.hpp"

#include "ast.hpp"
#include "build_tool.hpp"
#include "check_tool.hpp"
#include "frontend_pipeline.hpp"
#include "interpreter.hpp"
#include "project_manifest.hpp"
#include "test_tool.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;

struct CliContext {
    fs::path current_directory;
    fs::path executable_path;
};

struct ResolvedInputPath {
    std::string analysis_path;
    std::string display_path;
};

struct RunCommandOptions {
    bool print_ast = false;
    std::optional<std::string> path;
};

void PrintUsage() {
    std::cout << "Usage:\n";
    std::cout << "  aura.exe run [--ast] [path-to-file.aura]\n";
    std::cout << "  aura.exe check [--dump-ir] [--dump-cfg] [path-to-file.aura]\n";
    std::cout << "  aura.exe build [path-to-file.aura] [-o output.exe] [--backend embedded|cpp]"
                 " [--compiler auto|gcc|msvc] [--profile default|release]\n";
    std::cout << "  aura.exe test [--manifest tests/test_cases.json] [--executable build/aura.exe] [--filter text]\n";
    std::cout << "  aura.exe new <project-directory>\n";
    std::cout << "  aura.exe [--ast] [path-to-file.aura]\n";
    std::cout << "\n";
    std::cout << "When an input path is omitted, Aura will use the nearest Aura.toml entry file if one exists.\n";
}

bool IsUsageError(const AuraError& error) {
    const std::string message = error.what();
    return message.rfind("Usage error: ", 0) == 0 || message.rfind("Unknown option: ", 0) == 0;
}

fs::path FindToolRoot(fs::path current) {
    current = current.lexically_normal();
    while (!current.empty()) {
        if (fs::exists(current / "include" / "common.hpp") && fs::exists(current / "src" / "common.cpp")) {
            return current;
        }
        if (current == current.root_path()) {
            break;
        }
        current = current.parent_path();
    }
    throw AuraError("Could not find the Aura tool root");
}

fs::path GetExecutablePath() {
#ifdef _WIN32
    for (DWORD buffer_size = 260; buffer_size <= 32768; buffer_size *= 2) {
        std::wstring buffer(buffer_size, L'\0');
        const DWORD written_size = GetModuleFileNameW(nullptr, buffer.data(), buffer_size);
        if (written_size == 0) {
            break;
        }
        if (written_size < buffer_size - 1) {
            buffer.resize(written_size);
            return fs::path(buffer).lexically_normal();
        }
    }
    throw AuraError("Could not determine the Aura executable path");
#else
    throw AuraError("Could not determine the Aura executable path on this platform");
#endif
}

CliContext BuildCliContext() {
    CliContext context;
    context.current_directory = GetWorkingDirectoryPath();
    context.executable_path = GetExecutablePath();
    return context;
}

ResolvedInputPath ResolveInputPath(const std::optional<std::string>& explicit_path,
                                   const std::optional<ProjectManifest>& manifest,
                                   const std::string& command_name,
                                   bool allow_example_fallback) {
    if (explicit_path.has_value()) {
        return ResolvedInputPath{*explicit_path, *explicit_path};
    }

    if (manifest.has_value()) {
        return ResolvedInputPath{manifest->entry_path.string(), manifest->entry_relative_path.generic_string()};
    }

    if (allow_example_fallback) {
        return ResolvedInputPath{"examples/code_syntax.aura", "examples/code_syntax.aura"};
    }

    throw AuraError("Usage error: " + command_name + " requires an input .aura file or Aura.toml");
}

int ExecuteProgram(const CliContext& context, const RunCommandOptions& options) {
    const std::optional<ProjectManifest> manifest =
        options.path.has_value() ? std::nullopt : TryLoadProjectManifest(context.current_directory);
    const ResolvedInputPath input = ResolveInputPath(options.path, manifest, "run", true);
    const FrontendResult frontend = FrontendPipeline::Analyze(input.analysis_path);
    const Program& program = frontend.program;

    if (options.print_ast) {
        PrintProgram(program);
    }

    Interpreter interpreter(program);
    if (interpreter.HasFunction("main")) {
        const Value result = interpreter.ExecuteMain();
        if (!std::holds_alternative<std::monostate>(result)) {
            std::cout << "=== Program Result ===\n";
            std::cout << ValueToString(result) << '\n';
        }
    } else {
        std::cout << "No main function was found.\n";
    }

    return 0;
}

RunCommandOptions ParseRunCommand(int argc, char** argv, int start_index) {
    RunCommandOptions options;

    for (int i = start_index; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--ast") {
            options.print_ast = true;
            continue;
        }

        if (!arg.empty() && arg.front() == '-') {
            throw AuraError("Unknown option: " + arg);
        }

        if (options.path.has_value()) {
            throw AuraError("Usage error: only one input file can be provided");
        }

        options.path = std::move(arg);
    }

    return options;
}

fs::path NormalizeOutputPath(fs::path output_path) {
    if (!output_path.has_extension()) {
        output_path += ".exe";
    }
    return output_path;
}

BuildBackend ParseBuildBackend(const std::string& raw_backend) {
    if (raw_backend == "embedded") {
        return BuildBackend::Embedded;
    }
    if (raw_backend == "cpp" || raw_backend == "c") {
        return BuildBackend::Cpp;
    }

    throw AuraError("Usage error: build backend must be `embedded` or `cpp`");
}

std::string BuildBackendName(BuildBackend backend) {
    switch (backend) {
    case BuildBackend::Embedded:
        return "embedded";
    case BuildBackend::Cpp:
        return "cpp";
    }

    return "unknown";
}

BuildCompilerKind ParseBuildCompiler(const std::string& raw_compiler) {
    if (raw_compiler == "auto") {
        return BuildCompilerKind::Auto;
    }
    if (raw_compiler == "gcc" || raw_compiler == "g++") {
        return BuildCompilerKind::Gcc;
    }
    if (raw_compiler == "msvc" || raw_compiler == "cl") {
        return BuildCompilerKind::Msvc;
    }

    throw AuraError("Usage error: build compiler must be `auto`, `gcc`, or `msvc`");
}

std::string BuildCompilerName(BuildCompilerKind compiler) {
    switch (compiler) {
    case BuildCompilerKind::Bundled:
        return "bundled";
    case BuildCompilerKind::Auto:
        return "auto";
    case BuildCompilerKind::Gcc:
        return "gcc";
    case BuildCompilerKind::Msvc:
        return "msvc";
    }

    return "unknown";
}

enum class BuildProfileKind {
    Default,
    Release
};

BuildProfileKind ParseBuildProfile(const std::string& raw_profile) {
    if (raw_profile == "default") {
        return BuildProfileKind::Default;
    }
    if (raw_profile == "release") {
        return BuildProfileKind::Release;
    }

    throw AuraError("Usage error: build profile must be `default` or `release`");
}

std::string BuildProfileName(BuildProfileKind profile) {
    switch (profile) {
    case BuildProfileKind::Default:
        return "default";
    case BuildProfileKind::Release:
        return "release";
    }

    return "unknown";
}

int ExecuteBuildCommand(const CliContext& context, int argc, char** argv, int start_index) {
    std::optional<std::string> input_path;
    fs::path output_path;
    BuildBackend backend = BuildBackend::Embedded;
    BuildCompilerKind compiler = BuildCompilerKind::Auto;
    BuildProfileKind profile = BuildProfileKind::Default;
    bool backend_was_explicit = false;
    bool compiler_was_explicit = false;
    bool profile_was_explicit = false;

    for (int i = start_index; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                throw AuraError("Usage error: build requires a path after " + arg);
            }
            output_path = NormalizeOutputPath(argv[++i]);
            continue;
        }

        if (arg == "--backend") {
            if (i + 1 >= argc) {
                throw AuraError("Usage error: build requires a backend name after --backend");
            }
            backend = ParseBuildBackend(argv[++i]);
            backend_was_explicit = true;
            continue;
        }

        if (arg == "--compiler") {
            if (i + 1 >= argc) {
                throw AuraError("Usage error: build requires a compiler name after --compiler");
            }
            compiler = ParseBuildCompiler(argv[++i]);
            compiler_was_explicit = true;
            continue;
        }

        if (arg == "--profile") {
            if (i + 1 >= argc) {
                throw AuraError("Usage error: build requires a profile name after --profile");
            }
            profile = ParseBuildProfile(argv[++i]);
            profile_was_explicit = true;
            continue;
        }

        if (!arg.empty() && arg.front() == '-') {
            throw AuraError("Unknown option: " + arg);
        }

        if (input_path.has_value()) {
            throw AuraError("Usage error: build only accepts one input file");
        }

        input_path = arg;
    }

    const std::optional<ProjectManifest> manifest =
        input_path.has_value() ? std::nullopt : TryLoadProjectManifest(context.current_directory);
    const ResolvedInputPath resolved_input = ResolveInputPath(input_path, manifest, "build", false);
    if (manifest.has_value() && profile == BuildProfileKind::Release && !manifest->has_release_profile) {
        throw AuraError("Usage error: build profile `release` is not defined in Aura.toml");
    }
    if (!backend_was_explicit && manifest.has_value()) {
        backend = ParseBuildBackend(profile == BuildProfileKind::Release ? manifest->release_build_backend
                                                                         : manifest->build_backend);
    }
    if (!compiler_was_explicit && manifest.has_value()) {
        compiler = ParseBuildCompiler(profile == BuildProfileKind::Release ? manifest->release_build_compiler
                                                                           : manifest->build_compiler);
    }

    if (output_path.empty()) {
        if (!input_path.has_value() && manifest.has_value()) {
            output_path =
                profile == BuildProfileKind::Release ? manifest->release_output_path : manifest->output_path;
        } else {
            const std::string stem = fs::path(resolved_input.analysis_path).stem().string();
            output_path = fs::path("build") / "apps" /
                          (profile == BuildProfileKind::Release ? stem + "-release.exe" : stem + ".exe");
        }
    }

    const fs::path artifact_root =
        (!input_path.has_value() && manifest.has_value()) ? manifest->project_root : context.current_directory;
    const FrontendResult frontend = FrontendPipeline::Analyze(resolved_input.analysis_path);
    const BuildArtifact artifact = BuildTool::BuildExecutable(FindToolRoot(context.executable_path.parent_path()),
                                                              artifact_root,
                                                              frontend,
                                                              output_path,
                                                              backend,
                                                              compiler);
    std::cout << "Built executable: " << PathToDisplayString(artifact.executable_path) << '\n';
    std::cout << "Backend: " << BuildBackendName(artifact.backend) << '\n';
    std::cout << "Compiler: " << BuildCompilerName(artifact.compiler) << '\n';
    if (profile_was_explicit || profile == BuildProfileKind::Release) {
        std::cout << "Profile: " << BuildProfileName(profile) << '\n';
    }
    return 0;
}

int ExecuteCheckCommand(const CliContext& context, int argc, char** argv, int start_index) {
    CheckOptions options;

    for (int i = start_index; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--dump-ir") {
            options.dump_ir = true;
            continue;
        }
        if (arg == "--dump-cfg") {
            options.dump_cfg = true;
            continue;
        }

        if (!arg.empty() && arg.front() == '-') {
            throw AuraError("Unknown option: " + arg);
        }

        if (!options.input_path.empty()) {
            throw AuraError("Usage error: check only accepts one input file");
        }

        options.input_path = arg;
    }

    std::string display_path;
    if (options.input_path.empty()) {
        const ResolvedInputPath resolved_input =
            ResolveInputPath(std::nullopt, TryLoadProjectManifest(context.current_directory), "check", false);
        options.input_path = resolved_input.analysis_path;
        display_path = resolved_input.display_path;
    } else {
        display_path = options.input_path;
    }

    CheckTool::Run(options);
    std::cout << "Check passed: " << display_path << '\n';
    return 0;
}

int ExecuteTestCommand(const CliContext& context, int argc, char** argv, int start_index) {
    TestOptions options;
    options.executable_path = context.executable_path;
    std::optional<ProjectManifest> manifest;
    bool executable_was_explicit = false;

    for (int i = start_index; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--manifest") {
            if (i + 1 >= argc) {
                throw AuraError("Usage error: test requires a path after --manifest");
            }
            options.manifest_path = argv[++i];
            continue;
        }

        if (arg == "--executable") {
            if (i + 1 >= argc) {
                throw AuraError("Usage error: test requires a path after --executable");
            }
            options.executable_path = argv[++i];
            executable_was_explicit = true;
            continue;
        }

        if (arg == "--filter") {
            if (i + 1 >= argc) {
                throw AuraError("Usage error: test requires text after --filter");
            }
            options.filter = argv[++i];
            continue;
        }

        throw AuraError("Unknown option: " + arg);
    }

    if (options.manifest_path.empty()) {
        manifest = TryLoadProjectManifest(context.current_directory);
        if (manifest.has_value()) {
            options.manifest_path = manifest->test_manifest_path;
            if (!executable_was_explicit && !manifest->test_executable_path.empty()) {
                options.executable_path = manifest->test_executable_path;
            }
        }
    }

    return TestTool::Run(options);
}

int ExecuteNewCommand(int argc, char** argv, int start_index) {
    if (start_index >= argc) {
        throw AuraError("Usage error: new requires a target project directory");
    }
    if (start_index + 1 != argc) {
        throw AuraError("Usage error: new only accepts one target project directory");
    }

    const fs::path project_root = CreateAuraProject(argv[start_index]);
    std::cout << "Created Aura project: " << PathToDisplayString(project_root) << '\n';
    return 0;
}

}  // namespace

int RunCli(int argc, char** argv) {
    try {
        if (argc >= 2) {
            const std::string command = argv[1];
            if (command == "--help" || command == "-h" || command == "help") {
                PrintUsage();
                return 0;
            }

            const CliContext context = BuildCliContext();
            if (command == "run") {
                return ExecuteProgram(context, ParseRunCommand(argc, argv, 2));
            }
            if (command == "build") {
                return ExecuteBuildCommand(context, argc, argv, 2);
            }
            if (command == "check") {
                return ExecuteCheckCommand(context, argc, argv, 2);
            }
            if (command == "test") {
                return ExecuteTestCommand(context, argc, argv, 2);
            }
            if (command == "new") {
                return ExecuteNewCommand(argc, argv, 2);
            }
        }

        const CliContext context = BuildCliContext();
        return ExecuteProgram(context, ParseRunCommand(argc, argv, 1));
    } catch (const AuraError& error) {
        std::cerr << "Aura error: " << error.what() << '\n';
        if (IsUsageError(error)) {
            PrintUsage();
        }
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Unexpected error: " << error.what() << '\n';
        return 1;
    }
}
