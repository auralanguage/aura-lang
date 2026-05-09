#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

struct EmbeddedAppPackage {
    std::string entry_path;
    std::unordered_map<std::string, std::string> sources;
};

std::filesystem::path GetProcessExecutablePath();
bool TryReadEmbeddedAppPackage(const std::filesystem::path& executable_path, EmbeddedAppPackage& package);
void WriteEmbeddedAppExecutable(const std::filesystem::path& host_executable_path,
                                const std::filesystem::path& output_path,
                                const EmbeddedAppPackage& package,
                                const std::optional<std::filesystem::path>& payload_output_path = std::nullopt);
std::optional<int> TryRunPackagedAppFromCurrentExecutable();
