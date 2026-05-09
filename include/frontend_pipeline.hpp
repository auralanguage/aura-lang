#pragma once

#include "ast.hpp"

#include <string>
#include <unordered_map>

struct FrontendResult {
    Program program;
    std::unordered_map<std::string, std::string> loaded_sources;
    std::string entry_path;
};

class FrontendPipeline {
  public:
    static FrontendResult Analyze(const std::string& input_path);
};
