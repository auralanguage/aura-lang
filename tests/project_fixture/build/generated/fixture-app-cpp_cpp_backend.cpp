#include "common.hpp"
#include "runtime_support.hpp"

#include <windows.h>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::filesystem::path AuraGeneratedRuntimeBasePath() {
    for (DWORD buffer_size = 260; buffer_size <= 32768; buffer_size *= 2) {
        std::wstring buffer(buffer_size, L'\0');
        const DWORD written_size = GetModuleFileNameW(nullptr, buffer.data(), buffer_size);
        if (written_size == 0) {
            break;
        }
        if (written_size < buffer_size - 1) {
            buffer.resize(written_size);
            return std::filesystem::path(buffer).parent_path().lexically_normal();
        }
    }
    return GetWorkingDirectoryPath();
}

Value aura_fn_main(const std::vector<Value>& args);

Value aura_fn_main(const std::vector<Value>& args) {
    if (args.size() != 0) {
        throw AuraError("Function `main` expects 0 arguments, but got " + std::to_string(args.size()));
    }
    Value __aura_v0 = std::monostate{};
    Value __aura_v1 = std::monostate{};

    int __aura_block = 0;
    while (true) {
        switch (__aura_block) {
        case 0: {
            __aura_v0 = Value{MakeStringValue("Hello from fixture-app!")};
            __aura_v1 = AuraBuiltinPrint(std::vector<Value>{__aura_v0});
            return std::monostate{};
        }
        default:
            throw AuraError("Internal error: CFG backend reached an invalid block");
        }
    }
}

}  // namespace

int main() {
    try {
        SetRuntimeBasePath(AuraGeneratedRuntimeBasePath());
        const Value result = aura_fn_main(std::vector<Value>{});
        if (!std::holds_alternative<std::monostate>(result)) {
            std::cout << "=== Program Result ===\n";
            std::cout << ValueToString(result) << '\n';
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
