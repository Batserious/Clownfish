# Clownfish

一个轻量、现代 C++ 的分层状态机（Hierarchical State Machine）库，API 风格直观，适合业务流程编排、设备控制、游戏状态流转等场景。

## 特性

- Header-only 设计：核心实现在 `include/Clownfish/Clownfish.hpp`
- 强类型状态与触发器：`StateMachine<TState, TTrigger>`
- 转换配置能力完整：`Permit` / `PermitIf` / `PermitDynamic` / `Ignore` / `InternalTransition`
- 分层状态支持：`SubstateOf` + `InitialTransition`
- 回调机制：`OnEntry` / `OnExit` / `OnTransitioned` / `OnTransitionCompleted`
- 参数化触发器：`SetTriggerParameters<T...>()` + 运行时参数校验
- 异步 API：`FireAsync`、异步回调与异步 Entry/Exit/Activate/Deactivate
- 两种触发模式：`FiringMode::Immediate` 与 `FiringMode::Queued`

## 适用场景

- 有明确状态边界的业务流程
- 需要 Guard（守卫条件）控制的状态流转
- 需要父子状态复用行为的复杂流程
- 需要同步 + 异步混合触发的调度流程

## 快速开始

### 1) 构建示例

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON -DBUILD_TESTING=OFF
cmake --build build --target Examples
./build/examples/Examples
```

### 2) 运行测试

> 测试依赖 GoogleTest（`find_package(GTest REQUIRED)`）

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DBUILD_EXAMPLES=OFF
cmake --build build --target Test
ctest --test-dir build --output-on-failure
```

## 作为库使用（CMake）

项目导出接口库目标：

- `clownfish`
- `Clownfish::Clownfish`（别名）

在你的 CMake 工程中可以通过 `add_subdirectory` 方式接入：

```cmake
add_subdirectory(path/to/Clownfish)
target_link_libraries(your_target PRIVATE Clownfish::Clownfish)
```

## 最小示例

```cpp
#include "Clownfish/Clownfish.hpp"

enum class State { Idle, Running };
enum class Trigger { Start };

int main() {
    Clownfish::StateMachine<State, Trigger> sm(State::Idle);

    sm.Configure(State::Idle)
      .Permit(Trigger::Start, State::Running);

    sm.Fire(Trigger::Start);
    return sm.State() == State::Running ? 0 : 1;
}
```

## 示例说明

`examples/` 中包含可直接运行的场景：

- 基础状态迁移（`RunBasicTransitionExample`）
- 异步触发（`RunFireAsyncExample`）
- Guard 与参数化触发器（`RunGuardAndParametersExample`）
- 分层状态与初始子状态（`RunHierarchyAndInitialTransitionExample`）

## 测试覆盖

`tests/state_machine_test.cpp` 包含大量单元测试，覆盖：

- 基础迁移与外部状态存储
- 分层状态行为与循环配置检测
- Guard 判定与未满足 Guard 信息回传
- Entry/Exit/Transition 回调顺序
- 参数化触发器校验
- Dynamic/Internal transition
- Initial transition
- `FireAsync` 基础行为
