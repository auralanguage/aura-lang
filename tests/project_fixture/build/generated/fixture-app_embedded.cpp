#include "ast.hpp"
#include "common.hpp"
#include "interpreter.hpp"
#include "module_loader.hpp"
#include "semantics.hpp"

#include <iostream>
#include <string>
#include <unordered_map>

namespace {

std::unordered_map<std::string, std::string> EmbeddedSources() {
    std::unordered_map<std::string, std::string> sources;
    sources.emplace("C:\\Users\\pvpor\\OneDrive\\Masa\303\274st\303\274\\mains\\aura_lang\\tests\\project_fixture\\src\\main.aura", "fn main() {\n    print(\"Hello from fixture-app!\");\n}\n");
    return sources;
}

}  // namespace

int main() {
    try {
        ModuleLoader loader(EmbeddedSources());
        const Program program = loader.LoadProgram("C:\\Users\\pvpor\\OneDrive\\Masa\303\274st\303\274\\mains\\aura_lang\\tests\\project_fixture\\src\\main.aura");
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
