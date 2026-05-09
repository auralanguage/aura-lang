#include "lowering_pipeline.hpp"

#include "cfg_optimize.hpp"
#include "ir_verify.hpp"

LoweringResult LoweringPipeline::Lower(const Program& program) {
    LoweringResult result;
    result.ir_program = LowerProgramToIr(program);
    VerifyIrProgram(result.ir_program);
    result.cfg_program = LowerIrToCfg(result.ir_program);
    VerifyCfgProgram(result.cfg_program);
    OptimizeCfgProgram(result.cfg_program);
    VerifyCfgProgram(result.cfg_program);
    return result;
}
