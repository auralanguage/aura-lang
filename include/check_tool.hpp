#pragma once

#include <string>

struct CheckOptions {
    std::string input_path;
    bool dump_ir = false;
    bool dump_cfg = false;
};

class CheckTool {
  public:
    static void Run(const CheckOptions& options);
};
