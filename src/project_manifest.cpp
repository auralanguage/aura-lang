#include "project_manifest.hpp"

#include "common.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {

std::string ReadTextFile(const fs::path& path) {
    std::FILE* input = _wfopen(path.c_str(), L"rb");
    if (input == nullptr) {
        throw AuraError("Could not open project manifest: " + PathToDisplayString(path));
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

void WriteTextFile(const fs::path& path, const std::string& text) {
    std::FILE* output = _wfopen(path.c_str(), L"wb");
    if (output == nullptr) {
        throw AuraError("Could not write file: " + PathToDisplayString(path));
    }
    const std::size_t write_count = std::fwrite(text.data(), 1, text.size(), output);
    std::fclose(output);
    if (write_count != text.size()) {
        throw AuraError("Could not write file: " + PathToDisplayString(path));
    }
}

std::string Trim(std::string text) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
    text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
    return text;
}

bool IsSupportedManifestSection(const std::string& name) {
    return name == "build" || name == "build.release" || name == "test";
}

std::string StripComment(const std::string& line) {
    bool in_string = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) {
            in_string = !in_string;
            continue;
        }
        if (!in_string && line[i] == '#') {
            return line.substr(0, i);
        }
    }
    return line;
}

std::string ParseTomlString(const std::string& raw_value, const fs::path& manifest_path, std::size_t line_number) {
    if (raw_value.size() < 2 || raw_value.front() != '"' || raw_value.back() != '"') {
        throw AuraError("Invalid Aura.toml string at " + PathToDisplayString(manifest_path) + ":" +
                        std::to_string(line_number));
    }

    std::ostringstream buffer;
    for (std::size_t i = 1; i + 1 < raw_value.size(); ++i) {
        const char c = raw_value[i];
        if (c != '\\') {
            buffer << c;
            continue;
        }

        if (i + 1 >= raw_value.size() - 1) {
            throw AuraError("Invalid Aura.toml escape at " + PathToDisplayString(manifest_path) + ":" +
                            std::to_string(line_number));
        }

        const char escaped = raw_value[++i];
        switch (escaped) {
        case '\\':
        case '"':
            buffer << escaped;
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
            throw AuraError("Unsupported Aura.toml escape at " + PathToDisplayString(manifest_path) + ":" +
                            std::to_string(line_number));
        }
    }

    return buffer.str();
}

std::unordered_map<std::string, std::string> ParseTomlKeyValueObject(const fs::path& manifest_path) {
    std::unordered_map<std::string, std::string> values;

    std::istringstream lines(ReadTextFile(manifest_path));
    std::string line;
    std::size_t line_number = 0;
    std::string current_section;

    while (std::getline(lines, line)) {
        ++line_number;
        const std::string trimmed = Trim(StripComment(line));
        if (trimmed.empty()) {
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            current_section = Trim(trimmed.substr(1, trimmed.size() - 2));
            if (current_section.empty()) {
                throw AuraError("Aura.toml contains an empty section name at " +
                                PathToDisplayString(manifest_path) + ":" + std::to_string(line_number));
            }
            if (!IsSupportedManifestSection(current_section)) {
                throw AuraError("Aura.toml contains an unsupported section `" + current_section + "` at " +
                                PathToDisplayString(manifest_path) + ":" + std::to_string(line_number));
            }
            continue;
        }

        const std::size_t equal_index = trimmed.find('=');
        if (equal_index == std::string::npos) {
            throw AuraError("Invalid Aura.toml line at " + PathToDisplayString(manifest_path) + ":" +
                            std::to_string(line_number));
        }

        const std::string key = Trim(trimmed.substr(0, equal_index));
        const std::string value = Trim(trimmed.substr(equal_index + 1));
        if (key.empty()) {
            throw AuraError("Aura.toml contains an empty key at " + PathToDisplayString(manifest_path) + ":" +
                            std::to_string(line_number));
        }

        const std::string full_key = current_section.empty() ? key : current_section + "." + key;
        if (values.find(full_key) != values.end()) {
            throw AuraError("Aura.toml contains a duplicate key `" + full_key + "` at " +
                            PathToDisplayString(manifest_path) + ":" + std::to_string(line_number));
        }

        values[full_key] = ParseTomlString(value, manifest_path, line_number);
    }

    return values;
}

fs::path ResolveManifestPath(const fs::path& project_root, const std::string& path_text) {
    const fs::path raw_path(path_text);
    if (raw_path.is_absolute()) {
        return raw_path;
    }
    return project_root / raw_path;
}

std::string FileSafeName(std::string text) {
    std::replace(text.begin(), text.end(), ' ', '_');
    for (char& c : text) {
        const unsigned char value = static_cast<unsigned char>(c);
        if (!std::isalnum(value) && c != '-' && c != '_') {
            c = '_';
        }
    }
    return text;
}

std::string InferProjectName(const fs::path& target_directory) {
    const std::string name = target_directory.filename().string();
    if (name.empty()) {
        throw AuraError("Usage error: project directory must have a name");
    }
    return name;
}

std::optional<std::string> FindAliasedValue(const std::unordered_map<std::string, std::string>& values,
                                            std::initializer_list<const char*> keys,
                                            const fs::path& manifest_path,
                                            const char* logical_name) {
    std::optional<std::string> resolved;
    std::string resolved_key;

    for (const char* key : keys) {
        const auto it = values.find(key);
        if (it == values.end()) {
            continue;
        }

        if (resolved.has_value()) {
            throw AuraError("Aura.toml defines `" + resolved_key + "` and `" + std::string(key) +
                            "` for `" + logical_name + "` in " + PathToDisplayString(manifest_path));
        }

        resolved = it->second;
        resolved_key = key;
    }

    return resolved;
}

std::string NormalizeManifestBackend(const std::string& raw_backend, const fs::path& manifest_path) {
    if (raw_backend == "embedded") {
        return "embedded";
    }
    if (raw_backend == "cpp" || raw_backend == "c") {
        return "cpp";
    }

    throw AuraError("Aura.toml `build.backend` must be `embedded` or `cpp` in " +
                    PathToDisplayString(manifest_path));
}

std::string NormalizeManifestCompiler(const std::string& raw_compiler, const fs::path& manifest_path) {
    if (raw_compiler == "auto") {
        return "auto";
    }
    if (raw_compiler == "gcc" || raw_compiler == "g++") {
        return "gcc";
    }
    if (raw_compiler == "msvc" || raw_compiler == "cl") {
        return "msvc";
    }

    throw AuraError("Aura.toml `build.compiler` must be `auto`, `gcc`, or `msvc` in " +
                    PathToDisplayString(manifest_path));
}

std::string BuildManifestText(const std::string& project_name) {
    const std::string output_name = FileSafeName(project_name) + ".exe";
    const std::string release_output_name = FileSafeName(project_name) + "-release.exe";
    std::ostringstream manifest;
    manifest << "name = \"" << project_name << "\"\n";
    manifest << "version = \"0.1.0\"\n";
    manifest << "entry = \"src/main.aura\"\n";
    manifest << "\n";
    manifest << "[build]\n";
    manifest << "backend = \"embedded\"\n";
    manifest << "compiler = \"auto\"\n";
    manifest << "output = \"build/" << output_name << "\"\n";
    manifest << "\n";
    manifest << "[build.release]\n";
    manifest << "output = \"build/" << release_output_name << "\"\n";
    manifest << "\n";
    manifest << "[test]\n";
    manifest << "manifest = \"tests/test_cases.json\"\n";
    return manifest.str();
}

std::string BuildMainSourceText(const std::string& project_name) {
    std::ostringstream source;
    source << "fn main() {\n";
    source << "    print(\"Hello from " << project_name << "!\");\n";
    source << "}\n";
    return source.str();
}

std::string BuildTestManifestText(const std::string& project_name) {
    std::ostringstream manifest;
    manifest << "{\n";
    manifest << "  \"cases\": [\n";
    manifest << "    {\n";
    manifest << "      \"name\": \"project-main\",\n";
    manifest << "      \"args\": [\"src/main.aura\"],\n";
    manifest << "      \"exit_code\": 0,\n";
    manifest << "      \"stdout_exact\": \"Hello from " << project_name << "!\",\n";
    manifest << "      \"stderr_exact\": \"\"\n";
    manifest << "    }\n";
    manifest << "  ]\n";
    manifest << "}\n";
    return manifest.str();
}

std::optional<fs::path> FindManifestPath(fs::path current) {
    current = current.lexically_normal();
    while (!current.empty()) {
        const fs::path manifest_path = current / "Aura.toml";
        if (fs::exists(manifest_path)) {
            return manifest_path;
        }
        if (current == current.root_path()) {
            break;
        }
        current = current.parent_path();
    }
    return std::nullopt;
}

}  // namespace

std::optional<ProjectManifest> TryLoadProjectManifest(const fs::path& start_directory) {
    const std::optional<fs::path> manifest_path = FindManifestPath(start_directory);
    if (!manifest_path.has_value()) {
        return std::nullopt;
    }
    return ParseProjectManifest(*manifest_path);
}

ProjectManifest LoadProjectManifest(const fs::path& start_directory) {
    const std::optional<ProjectManifest> manifest = TryLoadProjectManifest(start_directory);
    if (!manifest.has_value()) {
        throw AuraError("Could not find Aura.toml from `" + PathToDisplayString(start_directory.lexically_normal()) +
                        "`");
    }
    return *manifest;
}

ProjectManifest ParseProjectManifest(const fs::path& manifest_path) {
    const fs::path normalized_manifest_path = manifest_path.lexically_normal();
    const fs::path project_root = normalized_manifest_path.parent_path();
    const std::unordered_map<std::string, std::string> values = ParseTomlKeyValueObject(normalized_manifest_path);

    const auto name_value = FindAliasedValue(values, {"name"}, normalized_manifest_path, "name");
    if (!name_value.has_value() || name_value->empty()) {
        throw AuraError("Aura.toml must define a non-empty `name`");
    }

    ProjectManifest manifest;
    manifest.manifest_path = normalized_manifest_path;
    manifest.project_root = project_root;
    manifest.name = *name_value;
    manifest.version = "0.1.0";
    if (const auto version_value = FindAliasedValue(values, {"version"}, normalized_manifest_path, "version")) {
        manifest.version = *version_value;
    }

    manifest.entry_relative_path = "src/main.aura";
    if (const auto entry_value = FindAliasedValue(values, {"entry"}, normalized_manifest_path, "entry")) {
        manifest.entry_relative_path = *entry_value;
    }
    manifest.entry_path = ResolveManifestPath(project_root, manifest.entry_relative_path.string());

    manifest.build_backend = "embedded";
    if (const auto backend_value = FindAliasedValue(values, {"build.backend"}, normalized_manifest_path, "build.backend")) {
        manifest.build_backend = NormalizeManifestBackend(*backend_value, normalized_manifest_path);
    }
    manifest.build_compiler = "auto";
    if (const auto compiler_value =
            FindAliasedValue(values, {"build.compiler"}, normalized_manifest_path, "build.compiler")) {
        manifest.build_compiler = NormalizeManifestCompiler(*compiler_value, normalized_manifest_path);
    }
    manifest.output_relative_path = fs::path("build") / (FileSafeName(manifest.name) + ".exe");
    if (const auto output_value = FindAliasedValue(values,
                                                   {"build.output", "output"},
                                                   normalized_manifest_path,
                                                   "build.output")) {
        manifest.output_relative_path = *output_value;
    }
    manifest.output_path = ResolveManifestPath(project_root, manifest.output_relative_path.string());

    const auto release_backend_value =
        FindAliasedValue(values, {"build.release.backend"}, normalized_manifest_path, "build.release.backend");
    const auto release_compiler_value =
        FindAliasedValue(values, {"build.release.compiler"}, normalized_manifest_path, "build.release.compiler");
    const auto release_output_value =
        FindAliasedValue(values, {"build.release.output"}, normalized_manifest_path, "build.release.output");
    manifest.has_release_profile = release_backend_value.has_value() || release_compiler_value.has_value() ||
                                   release_output_value.has_value();
    manifest.release_build_backend = manifest.build_backend;
    manifest.release_build_compiler = manifest.build_compiler;
    manifest.release_output_relative_path = fs::path("build") / (FileSafeName(manifest.name) + "-release.exe");
    manifest.release_output_path = ResolveManifestPath(project_root, manifest.release_output_relative_path.string());
    if (release_backend_value.has_value()) {
        manifest.release_build_backend = NormalizeManifestBackend(*release_backend_value, normalized_manifest_path);
    }
    if (release_compiler_value.has_value()) {
        manifest.release_build_compiler =
            NormalizeManifestCompiler(*release_compiler_value, normalized_manifest_path);
    }
    if (release_output_value.has_value()) {
        manifest.release_output_relative_path = *release_output_value;
        manifest.release_output_path = ResolveManifestPath(project_root, manifest.release_output_relative_path.string());
    }

    manifest.test_manifest_relative_path = fs::path("tests") / "test_cases.json";
    if (const auto test_manifest_value = FindAliasedValue(values,
                                                          {"test.manifest", "test_manifest"},
                                                          normalized_manifest_path,
                                                          "test.manifest")) {
        manifest.test_manifest_relative_path = *test_manifest_value;
    }
    manifest.test_manifest_path = ResolveManifestPath(project_root, manifest.test_manifest_relative_path.string());

    if (const auto test_executable_value = FindAliasedValue(values,
                                                            {"test.executable"},
                                                            normalized_manifest_path,
                                                            "test.executable")) {
        manifest.test_executable_relative_path = *test_executable_value;
        manifest.test_executable_path =
            ResolveManifestPath(project_root, manifest.test_executable_relative_path.string());
    }

    return manifest;
}

fs::path CreateAuraProject(const fs::path& target_directory) {
    const fs::path project_root =
        target_directory.is_absolute() ? target_directory.lexically_normal()
                                       : (GetWorkingDirectoryPath() / target_directory).lexically_normal();
    const std::string project_name = InferProjectName(project_root);

    if (fs::exists(project_root)) {
        if (!fs::is_directory(project_root)) {
            throw AuraError("Usage error: project target must be a directory path");
        }
        if (fs::directory_iterator(project_root) != fs::directory_iterator()) {
            throw AuraError("Usage error: target project directory already exists and is not empty");
        }
    } else {
        fs::create_directories(project_root);
    }

    fs::create_directories(project_root / "src");
    fs::create_directories(project_root / "tests");

    WriteTextFile(project_root / "Aura.toml", BuildManifestText(project_name));
    WriteTextFile(project_root / "src" / "main.aura", BuildMainSourceText(project_name));
    WriteTextFile(project_root / "tests" / "test_cases.json", BuildTestManifestText(project_name));

    return project_root;
}
