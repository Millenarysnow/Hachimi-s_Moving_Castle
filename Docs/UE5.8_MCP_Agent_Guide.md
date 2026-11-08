# UE 5.8 官方 MCP Agent 使用指南

## 适用范围

本指南用于通过 Epic 随 UE 5.8 发行版提供的 `ModelContextProtocol` 插件操作当前打开的 Unreal Editor。该插件属于 Experimental 功能，运行期间必须保持编辑器开启；MCP 工具调用会在编辑器游戏线程执行。

本项目已启用 `ModelContextProtocol`、`EditorToolset` 和 `AllToolsets`。官方插件位于引擎目录 `Engine/Plugins/Experimental/ModelContextProtocol`，工具集位于 `Engine/Plugins/Experimental/Toolsets`。

当前项目连接配置如下：

| 项目 | 配置 |
| --- | --- |
| MCP 服务名 | `unreal-mcp` |
| 地址 | `http://127.0.0.1:9876/mcp` |
| Codex 项目配置 | `.codex/config.toml` |
| 编辑器自动启动 | `Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini` 中已启用 |
| 默认工具审批 | `prompt`，由 Codex 对需要审批的工具调用进行确认 |

服务默认只绑定本机回环地址，且没有认证层。保持 `127.0.0.1`，不要将监听地址改为公网或局域网地址。

## 连接检查

`codex mcp list` 可以确认 Codex 是否读到了项目配置，但配置显示为 enabled 并不能证明 Unreal Editor 当前在线。应通过一次只读 MCP 调用验证实际连接：

1. 调用 `mcp__unreal_mcp__list_toolsets({})`，确认能够读取当前编辑器已注册的工具集。
2. 调用 `mcp__unreal_mcp__describe_toolset({ toolset_name: "editor_toolset.toolsets.scene.SceneTools" })`，读取场景工具的准确名称和参数 schema。
3. 用 `mcp__unreal_mcp__call_tool` 查询当前关卡：

```jsonc
{
  // 选择 list_toolsets 返回的场景工具集全名。
  "toolset_name": "editor_toolset.toolsets.scene.SceneTools",
  // 读取当前关卡路径，不会修改关卡。
  "tool_name": "get_current_level",
  // get_current_level 不需要参数。
  "arguments": {}
}
```

`get_current_level` 的 `returnValue` 应对应当前编辑器加载的关卡。若返回有效资产路径，说明 MCP 工具已成功访问当前编辑器；若返回空字符串，说明协议调用成功但当前查询没有拿到关卡路径，应检查编辑器是否已加载目标关卡及项目实例。

如果工具不可用，先确认 UE 编辑器已打开且加载了本项目。然后查看 Output Log 中的 `LogModelContextProtocol`。需要手动启动时，在 UE 控制台输入：

```text
ModelContextProtocol.StartServer 9876
```

也可以在 PowerShell 检查端口是否监听：

```powershell
# 只检查本机 TCP 端口；TCP 可达不能单独证明 MCP 协议调用成功。
Test-NetConnection 127.0.0.1 -Port 9876
```

端口连通只说明 TCP 可达；仍需成功调用 `list_toolsets` 或只读工具，才能确认 MCP 协议层工作正常。

## 标准调用流程

1. 先用 `list_toolsets` 发现本次编辑器实际加载的工具集。工具集合会随已启用插件变化，不要依赖记忆中的名称。
2. 用 `describe_toolset` 读取相关工具的用途、精确名称、必填参数和数据结构。不要猜测参数名、对象引用或资产路径。
3. 先用只读工具检查当前状态，再决定是否需要修改。
4. 每次只发起一个 Unreal MCP 工具调用，读取结果后再继续下一步。服务会将调用串行派发到游戏线程，不要并行调用多个 UE 工具。
5. 修改后使用对应的读取工具验证结果。仅保存用户要求修改的 Actor 或资产，不要为了方便而批量保存整个项目。

工具返回的 UObject 引用通常形如 `{"refPath":"..."}`。后续调用应原样传回该引用，不要自行拼造 `refPath`。

## 常用工具集

| 任务 | 工具集 | 典型用途 |
| --- | --- | --- |
| 当前关卡、Outliner 和场景 Actor | `editor_toolset.toolsets.scene.SceneTools` | `get_current_level`、`find_actors`、读取/整理文件夹、加载关卡、增删 Actor |
| Actor 组件、变换、标签 | `editor_toolset.toolsets.actor.ActorTools` | `get_components`、`get_actor_transform`、读取或设置标签和变换 |
| 资产浏览和依赖 | `editor_toolset.toolsets.asset.AssetTools` | `find_assets`、`get_dependencies`、`is_dirty`、检查资产是否存在 |
| Blueprint 图表 | `editor_toolset.toolsets.blueprint.BlueprintTools` | `list_graphs`、`read_graph_dsl`、节点/引脚操作、`compile_blueprint` |
| UObject 属性 | `editor_toolset.toolsets.object.ObjectTools` | `list_properties`、`get_properties`、`set_properties` |
| 编辑器状态与 PIE | `EditorToolset.EditorAppToolset` | 当前选择、内容浏览器路径、视口、`IsPIERunning`、`StartPIE`、`StopPIE` |
| 编辑器日志 | `EditorToolset.LogsToolset` | `GetLogEntries`、`GetLogCategories`、检查或调整日志级别 |
| 自动化测试 | `AutomationTestToolset.AutomationTestToolset` | 发现测试、筛选测试、运行测试和读取结果 |
| UMG、Niagara、PCG、Sequencer 等 | 对应已加载的专用 Toolset | 先通过 `list_toolsets` 和 `describe_toolset` 获取准确 schema |

`find_actors` 当前要求提供 `name`、`tag` 和 `collision_channels` 等参数。先读取 schema，再按目标选择筛选条件。工具的必填字段不代表可以用任意空值替代。

## 修改资产的注意事项

- Actor、Blueprint、UMG、资产移动/删除和属性设置会直接影响正在打开的项目。执行前确认目标资产路径和修改范围符合用户请求。
- 使用 `ObjectTools` 时，按 `list_properties` → `get_properties` → `set_properties` 的顺序工作；属性名以工具实际返回为准。UMG 工具集也要求先发现属性再读写。
- Blueprint 图表写入前先读取 `get_graph_dsl_docs` 和目标图表内容；修改后调用 `compile_blueprint` 并检查编译结果。
- 通过 `AssetTools.is_dirty` 检查资产状态。保存时把精确资产路径传给 `save_assets`；除非用户明确要求，不调用 `delete`、批量移动或范围不明的保存操作。
- 修改完成后用只读查询复查结果，并向用户说明改变了哪些 Actor、资产或编辑器状态。
- 代码文件仍通过项目工作区修改，并遵守项目 `AGENTS.md` 的中文注释要求；MCP 更适合操作编辑器状态、Blueprint 和资产。

## 自动化测试工具流程

`AutomationTestToolset.AutomationTestToolset` 在列出或运行测试前需要先调用 `DiscoverTests`。随后按需求调用 `ListTests`，检查测试名后再运行 `RunTests`；运行结束使用 `GetTestStatus` 和 `GetTestResults` 查看状态、错误及警告。一次运行中的测试尚未结束前，不要启动重叠的测试调用。

## 官方参考

- [Epic Unreal MCP in Unreal Editor（UE 5.8）](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor)
- [Epic Unreal MCP 插件 API 索引](https://dev.epicgames.com/documentation/unreal-engine/API/PluginIndex/ModelContextProtocol)
