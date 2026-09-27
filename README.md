# HT Blueprint Toggle Tool

适用于 Unreal Engine 5.6 的编辑器插件，用于生成角色材质切换蓝图、批量分配骨骼网格体材质槽、创建四贴图材质实例，以及导出选定的 Cooked 资产。

## 功能

- 生成单个或多个材质区域的显示/隐藏蓝图。
- 用 `5+6;8+10+12` 在不同大小的材质组之间循环；分号循环不追加全隐藏状态。
- 生成单材质槽或多材质槽的多贴图循环切换蓝图。
- 为现有材质的 `LightMap` 和 `ID_Tex` 生成临时 RGB 通道调节蓝图，支持向 0 / 255 调节，不需要打包新的材质球。
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
   HTToggleTool-v<版本号>.zip
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

- `Material visibility`：材质区域显示/隐藏及材质组循环。`+` 连接同一状态内同时显示的 Material ID；分号只循环指定组，逗号额外追加“全部隐藏”。`Initial State` 可设置首次没有对应存档时使用的默认状态，不改变状态顺序。
- `Texture switch`：材质贴图循环切换。
- `Material switch`：对一个或多个 Material Slot 循环切换指定材质球；材质球选择列表会限制在当前 Character Folder 内。
- `RGB tuner`：对现有材质的 `LightMap`、`ID_Tex` 或两者生成运行时 RGB 黑白混合调节节点。
- `Material Instance`：重建材质节点并创建材质实例。
- `Slot Materials`：处理骨骼网格体材质槽的材质球分配。

在 `Texture switch` 模式中点击 `Material Slot(s)` 右侧的 `Analyze`，插件会分析当前 AnimBP 的预览骨骼网格体，把使用同一个材质的 Slot 分到同一组。选择某一组后，会自动填写该组的全部 Slot ID，并同步填写 `Source Material`。分组列表会显示对应材质的材质球缩略图，不再使用贴图参数作为预览图。
选择 `Texture 1/2/...` 时，贴图资产列表会限制在 `Settings` 当前选择的角色文件夹及其子目录内，避免误选其他角色的贴图。

### 材质组循环输入（v1.5.24）

在 `Material visibility` 的现有 `Material ID(s)` 输入框中填写即可，不需要额外的分组面板。

| 输入 | 循环状态 |
| --- | --- |
| `5+6;8+10` | 显示 5/6 → 显示 8/10 → 回到 5/6 |
| `5+6；8+10+12` | 显示 5/6 → 显示 8/10/12 → 回到 5/6 |
| `5+6,8+10+12` | 显示 5/6 → 显示 8/10/12 → 全部隐藏 → 回到 5/6 |
| `16` 或 `13+20` | 保留原有显示/隐藏切换 |

英文分号 `;` 和中文分号 `；` 均可，分隔符周围可加空格；每组材质数量可以不同。进入某组时显示该组全部 ID，并隐藏输入中的其他组；未列出的 ID 不受影响。分号和逗号不能混用，ID 不能重复，不能有空组。

`Initial State` 从 0 开始：两组分号循环可选 0/1，两组逗号循环可选 0/1/2（2 为全隐藏）。按键及 SaveGame 仍共用同一个状态变量；原有存档优先于默认值。从旧逗号循环改为分号循环后，需要重新生成并烘焙 AnimBP；使用 NTE Panel System 时也应重新扫描/生成面板以更新状态数量。

### RGB 通道调节器

`RGB tuner` 用于临时观察 `LightMap` 与 `ID_Tex` 三个通道对角色质感的影响，确定数值后再到外部图像工具中修改原贴图。它不会写入 SaveGame，也不会改变材质/贴图切换的状态顺序。

1. 选择 AnimBP，在 `Material Slot(s)` 右侧点击 `Analyze`。
2. 选择目标材质组；工具会自动填入所有使用该材质的 Slot，并读取有效的 `LightMap` 与 `ID_Tex` 参数贴图。
3. 勾选需要调节的贴图，点击生成。
4. 再用 NTE Panel System 扫描并生成游戏内滑块面板。

生成的 MID 使用每个 Slot 当时已有的材质，不会绑定或要求打包一个新的材质球。运行时缓存原贴图，每个通道采用 `-100%～+100%` 的黑白混合调节：`0%` 保持原值，`-100%` 达到 0，`+100%` 达到 255。原来为 0 的像素也可以调高。内部调整量 `t` 为 `-1～1`，计算为 `原像素 × (1 − abs(t)) + 255 × max(t, 0)`；例如原值 60 调到 +50% 后约为 158。百分比表示调整程度，不是整张贴图统一的最终像素值。

每次 Apply 都先从原图完整覆盖临时贴图，再加入向白色调整的分量，保留 Alpha，来回拖动不累计上次结果。`Reset` 恢复原贴图对象与三个 `0%` 调整量。`Export PNG` 会先按当前调整量重新绘制，再把实际处理后的 RGBA8 Render Target 写入运行时项目的 `Saved` 目录，文件名仍为 `<原贴图名>_RGB.png`；重复导出覆盖同一文件。在当前游戏中已确认目录为 `%LOCALAPPDATA%\HT\Saved`。

临时预览的 LightMap 和 ID Render Target 均关闭自动 Mip，只使用当前绘制的原尺寸层级，避免转动视角或拉远时采到未更新的缩小层级而出现残留。远距离预览可能增加锯齿；Reset 后仍使用原贴图及其原有 Mip。跨 ID 判定边界仍可能改变材质类型。

从旧倍率版升级时，先用相同 `Tuner Name` 重新生成 RGB 调节器，再使用 NTE Panel System 0.1.15 重新扫描并生成面板，最后重新烘焙 AnimBP、BPI、WBP。新元数据包含 `Mode=BlackWhiteV1`，调整变量后缀为 `_Adjustment`；新面板会拒绝旧倍率元数据，避免两套算法混用。

示例：

```text
Material ID(s): 13,20
Material ID(s): 13+20
Material ID(s): 1+2,3+4
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
