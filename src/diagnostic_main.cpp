#include "diagnostic_tools.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        return sensor_daemon::DiagnosticCLI::run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Diagnostic tool error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown error occurred during diagnostics" << std::endl;
        return 1;
    }
}