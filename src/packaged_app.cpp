#include "packaged_app.hpp"

#include "common.hpp"
#include "interpreter.hpp"
#include "module_loader.hpp"
#include "semantics.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr char kPackageMarker[] = "AURA_EMBEDDED_PACKAGE_V1";
constexpr std::uint64_t kPackageVersion = 1;

using ByteBuffer = std::vector<unsigned char>;

void WriteUint64(ByteBuffer& buffer, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        buffer.push_back(static_cast<unsigned char>((value >> shift) & 0xFF));
    }
}

std::uint64_t ReadUint64(const ByteBuffer& buffer, std::size_t& offset) {
    if (offset + 8 > buffer.size()) {
        throw AuraError("Embedded Aura package is truncated");
    }

    std::uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(buffer[offset++]) << shift;
    }
    return value;
}

void WriteString(ByteBuffer& buffer, const std::string& value) {
    WriteUint64(buffer, static_cast<std::uint64_t>(value.size()));
    buffer.insert(buffer.end(), value.begin(), value.end());
}

std::string ReadString(const ByteBuffer& buffer, std::size_t& offset) {
    const std::uint64_t size = ReadUint64(buffer, offset);
    if (size > buffer.size() - offset) {
        throw AuraError("Embedded Aura package string is truncated");
    }

    const char* begin = reinterpret_cast<const char*>(buffer.data() + offset);
    offset += static_cast<std::size_t>(size);
    return std::string(begin, begin + size);
}

ByteBuffer SerializePackage(const EmbeddedAppPackage& package) {
    ByteBuffer buffer;
    WriteUint64(buffer, kPackageVersion);
    WriteString(buffer, package.entry_path);

    std::vector<std::pair<std::string, std::string>> ordered_sources;
    ordered_sources.reserve(package.sources.size());
    for (const auto& entry : package.sources) {
        ordered_sources.push_back(entry);
    }
    std::sort(ordered_sources.begin(),
              ordered_sources.end(),
              [](const auto& left, const auto& right) { return left.first < right.first; });

    WriteUint64(buffer, static_cast<std::uint64_t>(ordered_sources.size()));
    for (const auto& [path, source] : ordered_sources) {
        WriteString(buffer, path);
        WriteString(buffer, source);
    }

    return buffer;
}

EmbeddedAppPackage DeserializePackage(const ByteBuffer& buffer) {
    std::size_t offset = 0;
    const std::uint64_t version = ReadUint64(buffer, offset);
    if (version != kPackageVersion) {
        throw AuraError("Embedded Aura package version is not supported");
    }

    EmbeddedAppPackage package;
    package.entry_path = ReadString(buffer, offset);
    const std::uint64_t source_count = ReadUint64(buffer, offset);
    for (std::uint64_t index = 0; index < source_count; ++index) {
        const std::string path = ReadString(buffer, offset);
        package.sources.emplace(path, ReadString(buffer, offset));
    }

    if (offset != buffer.size()) {
        throw AuraError("Embedded Aura package contains unexpected trailing data");
    }

    return package;
}

ByteBuffer ReadBinaryFile(const fs::path& path) {
    std::FILE* input = _wfopen(path.c_str(), L"rb");
    if (input == nullptr) {
        throw AuraError("Could not open file: " + PathToDisplayString(path));
    }

    ByteBuffer buffer;
    unsigned char chunk[4096];
    while (true) {
        const std::size_t read_count = std::fread(chunk, 1, sizeof(chunk), input);
        if (read_count > 0) {
            buffer.insert(buffer.end(), chunk, chunk + read_count);
        }
        if (read_count < sizeof(chunk)) {
            break;
        }
    }

    std::fclose(input);
    return buffer;
}

void WriteBinaryFile(const fs::path& path, const ByteBuffer& buffer) {
    std::FILE* output = _wfopen(path.c_str(), L"wb");
    if (output == nullptr) {
        throw AuraError("Could not write file: " + PathToDisplayString(path));
    }

    const std::size_t write_count = std::fwrite(buffer.data(), 1, buffer.size(), output);
    std::fclose(output);
    if (write_count != buffer.size()) {
        throw AuraError("Could not write file: " + PathToDisplayString(path));
    }
}

bool TryExtractPackagePayload(const ByteBuffer& executable_bytes, ByteBuffer& payload) {
    const std::size_t marker_size = sizeof(kPackageMarker) - 1;
    if (executable_bytes.size() < marker_size + 8) {
        return false;
    }

    const std::size_t marker_offset = executable_bytes.size() - marker_size;
    if (!std::equal(kPackageMarker,
                    kPackageMarker + marker_size,
                    executable_bytes.begin() + static_cast<std::ptrdiff_t>(marker_offset))) {
        return false;
    }

    std::size_t size_offset = marker_offset - 8;
    std::uint64_t payload_size = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        payload_size |= static_cast<std::uint64_t>(executable_bytes[size_offset++]) << shift;
    }

    if (payload_size > marker_offset - 8) {
        throw AuraError("Embedded Aura package size is invalid");
    }

    const std::size_t payload_offset = marker_offset - 8 - static_cast<std::size_t>(payload_size);
    payload.assign(executable_bytes.begin() + static_cast<std::ptrdiff_t>(payload_offset),
                   executable_bytes.begin() + static_cast<std::ptrdiff_t>(payload_offset + payload_size));
    return true;
}

int RunEmbeddedPackage(const EmbeddedAppPackage& package, const fs::path& executable_path) {
    try {
        SetRuntimeBasePath(executable_path.parent_path().lexically_normal());
        ModuleLoader loader(package.sources);
        const Program program = loader.LoadProgram(package.entry_path);
        TypeChecker type_checker(program);
        type_checker.Check();
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
    } catch (const AuraError& error) {
        std::cerr << "Aura error: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Unexpected error: " << error.what() << '\n';
        return 1;
    }
}

}  // namespace

fs::path GetProcessExecutablePath() {
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
#endif
    throw AuraError("Could not determine the current executable path");
}

bool TryReadEmbeddedAppPackage(const fs::path& executable_path, EmbeddedAppPackage& package) {
    ByteBuffer payload;
    if (!TryExtractPackagePayload(ReadBinaryFile(executable_path), payload)) {
        return false;
    }

    package = DeserializePackage(payload);
    return true;
}

void WriteEmbeddedAppExecutable(const fs::path& host_executable_path,
                                const fs::path& output_path,
                                const EmbeddedAppPackage& package,
                                const std::optional<fs::path>& payload_output_path) {
    std::error_code equivalent_error;
    if (host_executable_path.lexically_normal() == output_path.lexically_normal() ||
        fs::equivalent(host_executable_path, output_path, equivalent_error)) {
        throw AuraError("Embedded build output cannot overwrite the running Aura executable");
    }

    const ByteBuffer payload = SerializePackage(package);
    if (payload_output_path.has_value()) {
        if (!payload_output_path->parent_path().empty()) {
            fs::create_directories(payload_output_path->parent_path());
        }
        WriteBinaryFile(*payload_output_path, payload);
    }

#ifdef _WIN32
    if (!CopyFileW(host_executable_path.c_str(), output_path.c_str(), FALSE)) {
        throw AuraError("Could not copy Aura runtime host to `" + PathToDisplayString(output_path) + "`");
    }
#else
    std::error_code copy_error;
    fs::copy_file(host_executable_path, output_path, fs::copy_options::overwrite_existing, copy_error);
    if (copy_error) {
        throw AuraError("Could not copy Aura runtime host to `" + PathToDisplayString(output_path) + "`");
    }
#endif

    std::FILE* output = _wfopen(output_path.c_str(), L"ab");
    if (output == nullptr) {
        throw AuraError("Could not append embedded Aura package to `" + PathToDisplayString(output_path) + "`");
    }

    const std::size_t payload_write_count = std::fwrite(payload.data(), 1, payload.size(), output);
    ByteBuffer footer;
    WriteUint64(footer, static_cast<std::uint64_t>(payload.size()));
    footer.insert(footer.end(), kPackageMarker, kPackageMarker + sizeof(kPackageMarker) - 1);
    const std::size_t footer_write_count = std::fwrite(footer.data(), 1, footer.size(), output);
    std::fclose(output);

    if (payload_write_count != payload.size() || footer_write_count != footer.size()) {
        throw AuraError("Could not finalize embedded Aura package in `" + PathToDisplayString(output_path) + "`");
    }
}

std::optional<int> TryRunPackagedAppFromCurrentExecutable() {
    try {
        const fs::path executable_path = GetProcessExecutablePath();
        EmbeddedAppPackage package;
        if (!TryReadEmbeddedAppPackage(executable_path, package)) {
            return std::nullopt;
        }

        return RunEmbeddedPackage(package, executable_path);
    } catch (const AuraError& error) {
        std::cerr << "Aura error: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Unexpected error: " << error.what() << '\n';
        return 1;
    }
}
