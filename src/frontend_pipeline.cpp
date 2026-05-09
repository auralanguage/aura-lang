#include "frontend_pipeline.hpp"

#include "module_loader.hpp"
#include "semantics.hpp"

FrontendResult FrontendPipeline::Analyze(const std::string& input_path) {
    ModuleLoader loader;
    Program program = loader.LoadProgram(input_path);

    TypeChecker type_checker(program);
    type_checker.Check();

    return FrontendResult{std::move(program), loader.LoadedSources(), loader.EntryPath()};
}
