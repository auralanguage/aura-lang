#pragma once

#include <filesystem>
#include <string>

struct TestOptions {
    std::filesystem::path manifest_path;
    std::filesystem::path executable_path;
    std::string filter;
};

class TestTool {
  public:
    static int Run(const TestOptions& options);
};
