#include "cli.hpp"
#include "packaged_app.hpp"
// i ll implement cross platform later
// for now windows only
int main(int argc, char** argv) {
    if (const auto packaged_exit_code = TryRunPackagedAppFromCurrentExecutable(); packaged_exit_code.has_value()) {
        return *packaged_exit_code;
    }
    return RunCli(argc, argv);
}
