# Clownfish Examples

This folder contains runnable examples built from the APIs used in `tests/state_machine_test.cpp` and `include/Clownfish/Clownfish.hpp`.

## Included scenarios

- `RunBasicTransitionExample()`
- `RunFireAsyncExample()`
- `RunGuardAndParametersExample()`
- `RunHierarchyAndInitialTransitionExample()`

## Build and run

```bash
cmake -S . -B cmake-build-debug -DBUILD_EXAMPLES=ON -DBUILD_TESTING=OFF
cmake --build cmake-build-debug --target Examples
./cmake-build-debug/examples/Examples
```

