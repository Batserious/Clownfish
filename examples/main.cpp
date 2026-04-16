#include <exception>
#include <iostream>

#include "scenarios/scenarios.hpp"

int main() {
    try {
        examples::RunBasicTransitionExample();
        examples::RunFireAsyncExample();
        examples::RunGuardAndParametersExample();
        examples::RunHierarchyAndInitialTransitionExample();
    } catch (const std::exception& ex) {
        std::cerr << "Example failed: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "All examples completed.\n";
    return 0;
}
