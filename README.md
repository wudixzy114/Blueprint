# Blueprint

> **C++ 可视化脚本引擎核心（Visual Scripting Engine, VSE）——自研的节点 / 引脚 / 图 / 执行器框架，目标在 ImGui 之上做 Unreal Blueprint 风格的可视化编程。**

## 项目定位

Blueprint 是用 C++17 写的一套**与编辑器无关**的可视化脚本（VSE）核心库。它定义了**节点定义**（`NodeDefinition`）、**引脚定义**（`PinDefinition`）、**图数据结构**（`Node` / `Pin` / `Link` / `Graph` / `GraphVariable`）、**类型注册表**（`TypeRegistry` + 隐式 `Variant` + 类型转换）、**执行上下文**（`ExecutionContext`）和**图执行器**（`GraphExecutor`），目标是把 Unreal Engine 的 Blueprint 范式以**纯 C++ + ImGui** 的方式重新实现一次。

当前阶段：核心数据模型与执行语义已经写好（h 文件丰富，cpp 仅有 `VSE_Core.cpp` 和 `VSE_Logger.cpp`），但 `main.cpp` 是空函数体——也就是说**执行器 / 编辑器 UI 还没有连成一个可运行的 demo**。它现在是一个"准备进入实现期"的工程骨架。

## 仓库结构

```
Blueprint/
├── CMakeLists.txt                # 编译入口（glad + imgui + glfw + spdlog + stduuid）
├── CMakePresets.json             # 单一 MinGW UCRT64 调试预设
├── main.cpp                      # 主入口：当前是空 main()
├── libs/                         # 第三方库 vendored（glad, imgui, glfw, json, sol2, stduuid, spdlog）
└── src/
    ├── VSE_ID.h                  # 全局唯一 ID 生成器
    ├── VSE_Logger.h/.cpp         # spdlog 包装的日志宏
    ├── VSE_Types.h               # Variant / TypeInfo / TypeRegistry / PinType / PinDirection
    ├── VSE_NodeDefinition.h      # PinDefinition + NodeDefinition（蓝图）
    ├── VSE_NodeRegistry.h/.cpp   # 全局 TypeName → NodeDefinition 注册表（单例）
    ├── VSE_GraphData.h           # Pin / Node / Link / GraphVariable / Graph 实例
    ├── VSE_ExecutionContext.h    # 节点 Execute 调用时的环境句柄
    ├── VSE_GraphExecutor.h/.cpp  # pull-based 数据求值 + push-based 执行流
    └── VSE_Core.cpp              # 核心类型 + 转换的注册入口
```

## 技术栈

| 层 | 选型 |
|---|---|
| 语言 | C++17（`CMAKE_CXX_STANDARD 17`） |
| 构建 | CMake ≥ 3.10 + `CMakePresets.json`（单一 MinGW UCRT64 预设） |
| 第三方 | `glad`（OpenGL loader）+ `imgui` + `glfw3`（`libs/imgui` 与 `libs/glfw` 已 vendored） |
| 日志 | `spdlog`（作为子目录 `add_subdirectory(libs/spdlog)`） |
| ID | `stduuid`（`UUID_SYSTEM_GENERATOR ON`） |
| 反射 / 类型擦除 | `std::any` + `std::type_index`（自实现 `Variant`） |
| 计划中的脚本 | `sol2`（Lua 绑定，已 include 进 target，但还没接入） |
| 序列化 | `nlohmann/json`（已 include，但未在代码中用） |

> 注意：仓库中 `add_subdirectory(libs/stduuid)` 和 `add_subdirectory(libs/spdlog)` 声明的子目录**当前并没有在仓库里提供**（只 include 了 `json`、`sol2`、`glfw` 的头文件路径，没有 `add_subdirectory`），需要外部补齐或用 `find_package` 接管。

## 核心模块

### 1. `VSE_Types.h` —— Variant + TypeRegistry
- `Variant` 用 `std::any` 做类型擦除，提供 `HasValue()` / `Reset()` / `GetValue<T>()` / `ConvertTo()`。
- `TypeInfo` 用 `std::type_index` 标识类型，附用户可见名字（"Integer" / "Float" / "String" 等）。
- `TypeRegistry` 单例：
  - `RegisterType<T>(name)`：注册 C++ 类型到引擎类型系统；
  - `RegisterConversion(from, to, fn)`：注册类型转换（`int→float`、`int→string` 等）；
  - `Convert(variant, targetType)`：跑 `BFS/DFS` 的转换链。

### 2. `VSE_NodeDefinition.h` —— 引脚 + 节点蓝图
- `PinDefinition`：
  - 字段：`ID` / `Name` / `Type (Execution|Data)` / `Direction (Input|Output)` / `DataType*` / `DefaultValue` / `Tooltip` / `bIsRequired`；
  - 构造时做合法性检查（Data pin 必须有 DataType；Execution pin 不能有 DataType；Output pin 的 DefaultValue 会被 reset；DefaultValue 类型不匹配会尝试 `TypeRegistry::ConvertTo`）。
- `NodeDefinition`：
  - 字段：`TypeName`（唯一程序标识，如 `"math.add_int"`） / `Title` / `Category` / `Description` / `InputPins` / `OutputPins`；
  - 核心函数指针：`std::function<int(ExecutionContext&)>` 形式的 `Execute`，返回值为下一个要触发的输出执行引脚下标，`-1` 代表终止；
  - 可选：`StaticValidateInstance(node, graph, report)` 用于静态分析；`CreateInitialState()` 用于初始化有状态节点（如 Flip-Flop、Delay）。
  - 辅助方法：`AddInputPin / AddOutputPin / FindInputPinDefinition / FindOutputPinDefinition / FindPinDefinitionByID`。

### 3. `VSE_NodeRegistry.h/.cpp` —— 节点定义注册表
- 单例 `NodeRegistry::Instance()`；
- `RegisterDefinition(NodeDefinition)`：按 `TypeName` 索引，重复注册返回 `false`；
- `GetDefinition(typeName)` / `GetAllDefinitions()` / `GetDefinitionsByCategory(cat)` / `GetAllCategories()`：查询接口。

### 4. `VSE_GraphData.h` —— 图与实例
- `Pin`（实例）：`ParentNode`（非拥有指针）+ `Definition`（非拥有指针）+ `CurrentValue`（执行期缓存，`mutable`）+ `ValueState`（`NotEvaluated / Evaluating / Evaluated`）+ `ConnectedLinkIDs`。
- `Node`（实例）：`InputPins` / `OutputPins`（由 Definition 自动生成）+ `Title`（可覆盖）+ `PosX/PosY`（画布坐标）+ `InternalState`（由 `Definition->CreateInitialState()` 初始化）。
- `Link`：`FromPinID` → `ToPinID`，带唯一 `ID`。
- `GraphVariable`：图中可命名的持久变量（Name / TypeInfo* / Value / Tooltip），构造时做类型校验。
- `Graph`：
  - 拥有 `std::vector<std::unique_ptr<Node>>` / `...<Link>` / `...<GraphVariable>`；
  - 维护 `m_NodeMap` / `m_PinMap` / `m_LinkMap` / `m_VariableMap` 索引以 O(1) 查询；
  - API：`AddNodeFromDefinition(typeName, x, y, id?)` / `AddLink(fromPinID, toPinID)` / `RemoveNode` / `RemoveLink` / `AddVariable` / `RebuildLookups` / `ResetNodesTransientState`；
  - `AddLink` 内部跑 `ValidatePinConnection(from, to)`（方向、类型匹配检查）。

### 5. `VSE_ExecutionContext.h` —— 节点执行时句柄
- 字段：`Executor*` / `CurrentGraph*` / `CurrentNode*`；
- API：`GetInputValue<T>(pinName, default)` —— 触发上游节点 pull-based 求值（委托给 `Executor->ResolvePinValue`）；
- 模板版本做类型安全与默认值回退。

### 6. `VSE_GraphExecutor.h/.cpp` —— 执行引擎
- 拥有 `ValueCache`（pinID → Variant）+ `NodeEvaluationStateMap`（用于数据依赖的环检测）+ `ExecutionStack`（执行节点栈）+ `ActiveDataResolutionStack`；
- 入口：`TriggerEvent(entryNodeID, execPinName)`，从事件节点开始驱动执行流；
- 关键方法：
  - `ResolvePinValue(Pin*, ExecutionContext&)` —— pull-based，必要时递归解析上游数据节点，带状态机防止 cycle；
  - `CacheOutputPinValue(Pin*, Variant, ExecutionContext&)` —— push-based，把当前节点的输出 pin 值塞进 cache；
  - `GetGraphVariableValue(name, graph)` / `SetGraphVariableValue(...)` —— 图级变量存取；
- 调试钩子：`OnNodeEnter / OnNodeExit / OnExecLinkTraversed / OnPinValueResolved / OnPinValueCached`，全部是 `std::function`，供编辑器断点 / 高亮用。

### 7. `VSE_Core.cpp` —— 启动注册
- `VSE::InitializeCoreTypes()`：
  - 注册基础类型：Void / Exec / int / float / double / bool / std::string；
  - 注册转换：int→float、float→int（截断）、int→double、int→string、float→string、bool→string；
  - 每一步都通过 `VSE_CORE_TRACE` / `VSE_CORE_INFO` 日志输出（spdlog）。

## 已完成 / 进行中

- [x] 类型系统（Variant + TypeRegistry + 6 条类型转换）
- [x] NodeDefinition + PinDefinition 数据建模
- [x] NodeRegistry 单例注册表
- [x] Graph / Node / Pin / Link / GraphVariable 实例模型 + 索引表
- [x] ExecutionContext 接口
- [x] GraphExecutor 接口设计（pull data + push exec + 调试回调）
- [x] Logger 包装
- [ ] `VSE_Core.cpp` / `VSE_Logger.cpp` 之外的 `.cpp`（如 `VSE_NodeRegistry.cpp`、`VSE_GraphData.cpp`、`VSE_GraphExecutor.cpp`）——头文件已就位但实现体未提交
- [ ] 任何具体的 `NodeDefinition`（math.add、branch、sequence 等）注册代码
- [ ] ImGui 编辑器画布（节点拖拽、连线、参数编辑）
- [ ] JSON 序列化 / 反序列化图
- [ ] Lua 脚本节点（`sol2` 已 include，未接入）
- [ ] 实际可运行的 demo（`main.cpp` 当前是空函数）

## 本地构建

仓库预设只配了 MinGW UCRT64 + Ninja 风格的 CMake preset，假设工作机是 `C:\msys64\ucrt64\`。

```powershell
# 前置：MSYS2 UCRT64 + CMake 3.10+ + Ninja
cmake --preset "GCC 14.2.0 x86_64-w64-mingw32 (ucrt64)"
cmake --build "out/build/GCC 14.2.0 x86_64-w64-mingw32 (ucrt64)"
.\out\build\"GCC 14.2.0 x86_64-w64-mingw32 (ucrt64)"\Blueprint.exe
```

> 当前可编译但运行后无可见输出（main 为空）。要让 demo 跑起来，需要先把缺失的 `.cpp` 实现补齐，并至少注册一个 BeginPlay 类的 Event 节点。

## 状态

- **版本**：v0.1.1（last commit 2025-06-14）
- **设计阶段**：数据模型 + 接口已稳定；实现期 / 编辑器期未启动
- **可运行性**：可编译；无可执行演示

## License

仓库内未附 LICENSE 文件，源码默认遵循 "All rights reserved"。
