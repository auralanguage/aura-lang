#pragma once

#include "cfg_ir.hpp"
#include "ir.hpp"

struct LoweringResult {
    IrProgram ir_program;
    CfgProgram cfg_program;
};

class LoweringPipeline {
  public:
    static LoweringResult Lower(const Program& program);
};
