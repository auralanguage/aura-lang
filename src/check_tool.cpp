#include "check_tool.hpp"

#include "frontend_pipeline.hpp"
#include "lowering_pipeline.hpp"

#include <iostream>

void CheckTool::Run(const CheckOptions& options) {
    const FrontendResult frontend = FrontendPipeline::Analyze(options.input_path);
    const LoweringResult lowered = LoweringPipeline::Lower(frontend.program);
    if (options.dump_ir) {
        std::cout << FormatIrProgram(lowered.ir_program);
        if (options.dump_cfg) {
            std::cout << "=== CFG SSA ===\n";
            std::cout << FormatCfgProgram(lowered.cfg_program);
        }
    } else if (options.dump_cfg) {
        std::cout << FormatCfgProgram(lowered.cfg_program);
    }
}
