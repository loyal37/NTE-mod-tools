# HT Blueprint Toggle Tool

适用于 Unreal Engine 5.6 的编辑器插件，用于生成角色材质切换蓝图、创建四贴图材质实例，以及导出选定的 Cooked 资产。

## 功能

- 生成单个或多个材质区域的显示/隐藏蓝图。
- 生成单材质槽或多材质槽的多贴图循环切换蓝图。
- 支持普通按键、符号按键以及 `ctrl`、`shift`、`alt` 组合按键。
- 自动创建动画蓝图变量、SaveGame 变量、读取与保存逻辑。
- 根据选定材质创建并配置材质实例。
- 从工程资产列表中选择需要导出的 Cooked 文件，并保持原目录结构。
- 可选择导出后启动外部打包器。
- 可选择导出后打开实际导出的角色目录。

## 安装

1. 从 GitHub Releases 下载：

   ```text
   HTToggleTool-v1.5.4.zip
   ```

2. 关闭 Unreal Editor。
3. 将压缩包中的 `HTBlueprintToggleTool` 文件夹放到：

   ```text
   YourProject/Plugins/HTBlueprintToggleTool
   ```

4. 打开工程，在 `Tools > HT Blueprint Toggle Tool` 启动插件。

发布包仅包含 UE 5.6 Win64 编辑器运行所需文件，不包含 `Source` 和 PDB。自行编译源码时需要 Visual Studio 2022 C++ 工具链。

## 材质实例工具

在主面板的 `Function Switch` 一行点击 `Material Instance`。

1. 选择需要修改的 `Material`。
2. `Instance Name` 默认使用原材质名称加 `_Inst`，也可以手动修改。
3. 分别选择四张贴图：

   ```text
   BaseColor
   ID_Tex
   LightMap
   NormalMap
   ```

4. 点击 `Rebuild Material and Create Instance`。

执行时会先删除选定材质中的全部节点，再从零创建下列节点和连线。请不要对需要保留原节点的材质直接执行。

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

## 蓝图切换

在 `Settings` 中选择动画蓝图和 SaveGame 蓝图，然后在主面板选择：

选定的两个蓝图路径会立即保存到当前工程的编辑器配置中，关闭并重新打开工具后仍会保留。

- `Material visibility`：材质区域显示/隐藏。
- `Texture switch`：材质贴图循环切换。

在 `Texture switch` 模式中点击 `Material Slot(s)` 右侧的 `Analyze`，插件会分析当前 AnimBP 的预览骨骼网格体，按材质对 Slot 分组。选择一组后会自动填写该组的全部 Slot ID，并同步填写 `Source Material`。

多材质区域示例：

```text
Material ID(s): 13,20
```

多材质槽同步切换示例：

```text
Material Slot(s): 12,13
```

组合按键示例：

```text
ctrl 6
shift 6
alt 6
```

SaveGame 命名由 `Anim Variable` 自动派生：

```text
Save Variable = AnimVariable + Save
Save Slot = AnimVariable
```

## Cooked 资产导出

点击主面板右上角的 `Cooked Assets`：

1. `Cooked source` 选择角色的 Cooked 目录。
2. `Output directory` 选择外部打包器中的角色父目录。
3. 勾选需要导出的工程资产。
4. 点击 `Export selected assets`。

选项：

- `Overwrite existing files`：覆盖已有文件。
- `Export and package`：导出成功后启动外部打包器。
- `Open output directory after export`：复制成功后打开实际导出的角色目录。

目录、资产勾选和两个导出选项都会保存在当前工程的编辑器配置中。

选择一个 `.uasset` 时，插件会自动携带存在的同名文件：

```text
.uasset
.uexp
.ubulk
.uptnl
```
