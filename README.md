# HT Blueprint Toggle Tool

适用于 Unreal Engine 5.6 的编辑器插件，用于生成角色材质切换蓝图、批量分配骨骼网格体材质槽、创建四贴图材质实例，以及导出选定的 Cooked 资产。

## 功能

- 生成单个或多个材质区域的显示/隐藏蓝图。
- 生成单材质槽或多材质槽的多贴图循环切换蓝图。
- 支持普通按键、符号按键，以及 `ctrl`、`shift`、`alt` 组合按键。
- 在角色文件夹中创建对应角色的 AnimBP 和 SaveGame 蓝图，并自动绑定到骨骼网格体的后期处理动画蓝图。
- 根据 Anim Variable 自动创建动画蓝图变量、SaveGame 变量、读取与保存逻辑。
- 在贴图切换模式中分析当前 AnimBP 预览骨骼网格体的材质槽，并按材质分组填入 Material Slot(s)。
- 扫描角色文件夹内的材质球，按插槽名自动匹配或批量分配到骨骼网格体材质槽。
- 根据选定材质创建四贴图材质实例。
- 从工程资产列表中选择需要导出的 Cooked 文件，并保持原目录结构。
- 可选择导出后启动外部打包器。
- 可选择导出后打开实际导出的角色目录。

## 安装

1. 从 GitHub Releases 下载：

   ```text
   HTToggleTool-v1.5.13.zip
   ```

2. 关闭 Unreal Editor。
3. 将压缩包中的 `HTBlueprintToggleTool` 文件夹放到：

   ```text
   YourProject/Plugins/HTBlueprintToggleTool
   ```

4. 打开工程，在 `Tools > HT Blueprint Toggle Tool` 启动插件。

发布包只包含 UE 5.6 Win64 编辑器运行所需文件，不包含 `Source` 和 PDB。自行编译源码时需要 Visual Studio 2022 C++ 工具链。

## 蓝图切换

在 `Settings` 中选择动画蓝图、SaveGame 蓝图和角色文件夹，选择结果会保存到当前工程配置中，关闭并重新打开工具后仍会保留。切换角色文件夹时，工具会自动查找该角色文件夹内的 AnimBP 和 SaveGame 蓝图并填入上方路径。角色文件夹用于扫描当前角色可用的材质球。
点击 `Create AnimBP + SaveGame and bind Skeletal Mesh` 后，插件会按角色文件夹名称创建对应的动画蓝图和 SaveGame 蓝图，自动把 AnimGraph 中的 `Input Pose` 连接到 `Output Pose`，并把该动画蓝图写入角色骨骼网格体的后期处理动画蓝图设置中。

`Function Switch` 可切换功能：

- `Material visibility`：材质区域显示/隐藏；`Material ID(s)` 会自动根据输入数量判断单材质或多材质，例如 `16` 或 `13,20`。
- `Texture switch`：材质贴图循环切换。
- `Material Instance`：重建材质节点并创建材质实例。
- `Slot Materials`：处理骨骼网格体材质槽的材质球分配。

在 `Texture switch` 模式中点击 `Material Slot(s)` 右侧的 `Analyze`，插件会分析当前 AnimBP 的预览骨骼网格体，把使用同一个材质的 Slot 分到同一组。选择某一组后，会自动填写该组的全部 Slot ID，并同步填写 `Source Material`。分组列表会显示对应材质的材质球缩略图，不再使用贴图参数作为预览图。

示例：

```text
Material ID(s): 13,20
Material Slot(s): 12,13
Key: ctrl 6
Key: shift 6
Key: alt 6
```

SaveGame 命名由 `Anim Variable` 自动派生：

```text
Save Variable = AnimVariable + Save
Save Slot = AnimVariable + character name
```

## 材质槽分配

在主面板的 `Function Switch` 一行点击 `Slot Materials`。

工具会读取当前 AnimBP 的预览骨骼网格体，并显示所有材质槽的 ID、插槽名和当前材质。同时会递归扫描 `Settings` 中的角色文件夹，列出其中的材质球和材质实例。
材质槽名称会以较大的粗体显示；选择材质球时，下拉列表和当前选择区域都会显示材质球预览图，方便区分相近名称的材质。
点击 `Use checked slots` 后，左侧对应的材质槽会变暗表示已分配，并自动清空这次勾选，方便继续选择下一组材质槽。左侧材质槽支持 Shift 区间勾选；右侧 `Refresh` 可重新生成材质球预览图。

可用操作：

- `Match Names`：如果材质球名称与插槽名称完全一致，就自动把该材质球分配给对应插槽。
- `Add`：添加一条批量映射，选择一个材质球，再输入一个或多个 Slot ID。
- `Use checked slots`：把左侧勾选的 Slot ID 填入当前映射行。
- `Apply Mappings`：把所有映射一次性写入骨骼网格体。

示例：

```text
Slot IDs: 1,5,15,16,17,18,19,23
Material: MI_player_010_female_cloth_b_Inst2
```

## 材质实例工具

在主面板的 `Function Switch` 一行点击 `Material Instance`。

1. 选择需要修改的 `Material`。
2. `Instance Name` 默认使用原材质名加 `_Inst`，也可以手动修改。
3. 分别选择四张贴图：

   ```text
   BaseColor
   ID_Tex
   LightMap
   NormalMap
   ```

4. 点击 `Rebuild Material and Create Instance`。

执行时会先删除选定材质中的全部节点，再重新创建所需节点和连线。请不要对需要保留原节点的材质直接执行。

工具会重新创建以下材质参数节点：

| 参数 | 采样器类型 | 连接 |
| --- | --- | --- |
| `BaseColor` | Color | `RGB -> Lerp B` |
| `ID_Tex` | Linear Color | `RGB -> MF_PhongToMetalRoughness.SpecularColor` |
| `LightMap` | Linear Color | `RGB -> MF_PhongToMetalRoughness.AmbientColor` |
| `NormalMap` | Normal | `RGB -> Material Normal` |

同时会重新创建：

- 黑色常量到 `Lerp A`
- `DiffuseColorMapWeight` 标量参数到 `Lerp Alpha`
- `Lerp` 到 `MF_PhongToMetalRoughness.DiffuseColor`
- 数值 `25` 到 `MF_PhongToMetalRoughness.Shininess`
- 函数的 `BaseColor / Metallic / Specular / Roughness` 输出到材质对应输入

已存在的同名材质实例会被更新，不会重复创建。

## Cooked 资产导出

点击主面板右上角的 `Cooked Assets`。

1. `Cooked source` 选择角色的 Cooked 目录。
2. `Output directory` 选择外部打包器中的角色父目录。
3. 勾选需要导出的工程资产。
4. 点击 `Export selected assets`。

选项：

- `Overwrite existing files`：覆盖已有文件。
- `Export and package`：导出成功后启动外部打包器。
- `Open output directory after export`：复制成功后打开实际导出的角色目录。

目录、资产勾选和导出选项都会保存到当前工程的编辑器配置中。

选择一个 `.uasset` 时，插件会自动携带存在的同名文件：

```text
.uasset
.uexp
.ubulk
.uptnl
```
