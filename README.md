# HT Blueprint Toggle Tool

适用于 Unreal Engine 5.6 的编辑器插件，用于生成角色材质切换蓝图、批量分配骨骼网格体材质槽、创建四贴图材质实例，以及导出选定的 Cooked 资产。

## 功能

- 生成单个或多个材质区域的显示/隐藏蓝图。
- 用 `5+6;8+10+12` 在不同大小的材质组之间循环；分号循环不追加全隐藏状态。
- 生成单材质槽或多材质槽的多贴图循环切换蓝图。
- 为现有材质的 `LightMap` 和 `ID_Tex` 生成临时 RGB 通道倍率调节蓝图，不需要打包新的材质球。
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
- `RGB tuner`：对现有材质的 `LightMap`、`ID_Tex` 或两者生成运行时 RGB 倍率调节节点。
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

v1.5.28 按用户要求恢复之前的倍率版实现（含 PNG 导出和单 Mip 防残留），撤回黑白混合算法及后续加载门槛/诊断改动。其他材质分组、切换与导出工具不回退。

各通道使用 `0～2` 倍率，默认及 Reset 为 `1`。计算为 `clamp(原像素 × 倍率, 0, 255)`：160 × 2 会截断到 255，60 × 2 为 120；原来为 0 的像素保持 0。倍率并不保证每个原值都能覆盖 0～255。

初始化按旧实现创建槽 MID、读取原图并分配预览 RT；Apply 每次从缓存的原图完整重绘，不累积上次结果。Reset 恢复原贴图和 `(1,1,1,1)` 倍率。恢复旧实现也意味着不再保留 v1.5.25～1.5.27 的懒初始化、完整材质引用恢复、流送等待与检查机制；本次不声称已查明此前游戏问题。

面板 `Export PNG` 先用当前倍率刷新 RT，再导出 `<原贴图名>_RGB.png` 到运行时 `Saved`。当前游戏路径为 `%LOCALAPPDATA%\HT\Saved`。同名导出会覆盖旧文件；未压缩源 PNG 不会被修改。游戏纹理压缩会影响导出像素。

临时预览的 LightMap 和 ID Render Target 均关闭自动 Mip，只使用当前绘制的原尺寸层级，避免转动视角或拉远时采到未更新的缩小层级而出现残留。远距离预览可能增加锯齿；Reset 后仍使用原贴图及其原有 Mip。跨 ID 判定边界仍可能改变材质类型。

回退必须用相同 `Tuner Name` 重新生成 RGB 调节器，然后用 NTE Panel System v0.1.17 重新扫描、生成面板，并重新烘焙打包 AnimBP、BPI、WBP。倍率变量恢复为 `_Gain`，元数据不含 `Mode`；面板拒绝未回退的有模式字段调节器，避免两套算法混用。只换插件 DLL 不会修改已生成/打包的蓝图。

### 嵌套切换（Toggle Tree，v1.5.29 生成器 / v1.5.30 图编辑器 / v1.5.31 节点内编辑 / v1.5.32 自动增减输入 / v1.5.33 显隐开关元数据 / v1.5.34 复制粘贴修复 / v1.5.35 面板行名用变量名 / v1.5.36 贴图切换与材质切换节点 / v1.5.37 放大与折叠）

LoyalTools 风格的嵌套切换：外层切换的每个选项下可以再挂切换，部件可见 = 路径上所有“变量 == 选项”条件的 AND。该功能独立于 `Material visibility` 等现有模式，不改变它们的生成逻辑、`5+6;8+10` 文本语法和面板扫描。从 v1.5.31 起，这个功能的节点和界面文字全部是英文。

使用方式：

1. 在 `Settings` 里选好动画蓝图、SaveGame 蓝图和角色文件夹，点 `Slot Materials` 右侧的 `Open Blueprint`。插件会在动画蓝图旁边创建（或打开）`TG_<动画蓝图名>` 资产并打开图编辑器；点图的空白处，`Details` 面板显示资产属性：`Anim Blueprint`、`SaveGame Blueprint`、`Character Name`（存档槽后缀）、`Tree Name`（受管区域名）、`Save Blueprints After Compile`。
2. 在图里右键添加节点：`Part`、`Switch`、`Texture Switch`、`Material Switch`、`Group`、`Generate`。参数直接在节点上改：
   - `Switch`：`Comment`、`Variable`、`Key` 文本框（`Comment` 只是图里的备注，不会显示在游戏里；游戏面板上这一行的标题是状态变量名）；`Option count` 和 `Initial option` 用 `+`/`-` 调整，增减选项时会同步增删底部的 `Option N` 输入插口，剩下插口上的连线保留；每个 `Option N` 插口右边的小文本框是该选项的标签（面板显示用，空选项显示为 `Hidden`）。
   - `Part`：`Comment`、`Material IDs`（如 `1, 2, 12`）；`By material...` 下拉列出预览网格体上的材质，选一个就自动勾选所有使用这个材质的槽，并记住这个材质（编译时按当前网格体重新查找，网格体换了材质分配也跟着变）；下面的槽列表可以手动勾选，手动改了就回到手动模式。`Part` 的 `Out` 可以连多条线：一条可见性连线（切换选项、群组或 `Generate`），外加任意多条连到 `Texture Switch` 的 `Target` 和 `Material Switch` 的 `Slots`；把它拖到另一个可见性输入时只移动可见性那条线。
   - `Texture Switch`：头部和 `Switch` 一样（`Comment`、`Variable`、`Key`、`Option count`、`Initial option`、每个选项的标签），外加 `Parameter`（右边的下拉列出所有目标材质都有的贴图参数，也可以手填，如 `BaseColor`）。左侧 `Target N` 输入只接 `Part`，一个输入一个部件，末尾自动保留一个空输入；每个 `Target` 行显示部件的槽号和材质，并给每个选项一个贴图选择器，空的选项保留材质自己的贴图。对应主面板原来 Texture 模式里的“材质组”。
   - `Material Switch`：头部和 `Switch` 一样；每个选项一行：标签 + 材质选择器，空的选项恢复槽的原始材质（预览网格体上的材质）。左侧 `Slots N` 输入只接 `Part`，所有部件的槽取并集。
   - `Texture Switch` 和 `Material Switch` 的 `Out` 连到 `Generate`、`Group` 或某个 `Switch` 的选项（挂在选项下时只在该选项生效，面板上随父条件置灰）；它们没有选项插口，下面不能再挂东西。它们和 `Switch` 一样按按键/变量名共用状态变量，不同种类之间也共用（选项数和初始选项必须一致）。
   - `Group`：`Comment`。输入插口自动增减：末尾始终保留一个空的 `In N`，连上最后一个输入就会多出一个新的空输入；断开后会收缩末尾多余的空输入，只留一个；中间断开的输入会保留，其余连线的顺序不变。
   - `Generate`：树的根，输入规则和 `Group` 相同。
   所有节点内修改都可以撤销/重做，选中节点后 `Details` 面板里也能改同样的参数。
   - 折叠：`Part`、`Switch`、`Texture Switch`、`Material Switch` 标题栏右侧有折叠/展开三角，折叠后只显示标题、一行摘要（如 `Key alt 6 · 2 options`、`IDs 10, 11`、贴图切换还显示参数名）和全部插口（`Switch` 的选项插口旁显示选项标签），连线不变，折叠后的节点照样可以连线。折叠状态按节点存进资产，可以撤销/重做，复制粘贴时保留。右键节点有 `Collapse`/`Expand`、`Collapse all`/`Expand all`；右键空白处的 `View` 分类里也有 `Collapse all`/`Expand all`。
   - 缩放：鼠标滚轮直接放大到 1:1 以上（不需要按住 Ctrl），最大 4 倍；默认仍是 1:1。
   - 复制、剪切、粘贴、复制节点（`Ctrl+C`/`Ctrl+X`/`Ctrl+V`/`Ctrl+D`）：一起复制的节点之间的连线保留，连到没有一起复制的节点的连线会断开，`Group`/`Generate` 的空输入随之收缩；也可以粘贴到另一个 `TG_` 资产；粘贴可以撤销/重做。`Generate` 节点不能复制。v1.5.34 修复了粘贴连在一起的多个节点（或把连着线的节点粘贴到另一个资产）时编辑器崩溃（`Assertion failed: OtherPin`）。
3. 连线：部件或切换的 `Out` 连到某个切换的选项、群组或 `Generate`；一个输出只能连一处，再连会替换旧连线；不允许成环。没有连到 `Generate` 的节点会被忽略。
4. 点工具栏 `Compile`：图先转成条件模型并校验（同按键的切换共用变量且选项数/初始选项必须一致，一个材质 ID 只能在一个部件里，按键必须可识别……），出错时在 `Result` 面板列出并选中对应节点；通过后调用生成器写入动画蓝图，默认同时保存两个蓝图。
5. 用 NTE Panel System 重新扫描并生成面板，树的每个状态变量会成为一行。

生成器入口 `FHTBlueprintToggleGenerator::GenerateNestedVisibility`（条件模型 `FHTNestedToggleGraph`：switches（`Kind` = 可见性/贴图/材质）+ parts + 槽的原始材质 `SlotMaterialPaths`）也可以直接调用。

- 填了同一按键的切换共用一个状态变量（也可以直接填相同变量名），一起翻转；它们的选项数和初始选项必须相同，否则报错并指明节点。不填变量名时按按键自动命名为 `<Tree Name>_<按键>`（如 `Typhoeus_Alt_Six`）；同一组里只给一个节点填名字即可。
- 一个 Material ID 只能出现在一条路径/一个部件里，否则报错并指明两个部件。空选项位置任意，不需要特殊表示。
- 按键使用现有按键解析（普通键、符号键、`ctrl/shift/alt` 组合），不支持 hold。
- 生成内容：每个状态变量一个 AnimBP int 变量 + SaveGame 变量（槽名 `<变量> <角色名>`）；`HTN_Apply_<名>` 自定义事件按 AND 条件调用 `ShowMaterialSection`，不在图里的 ID 不碰；`BlueprintUpdateAnimation` 中每个变量一段 `WasInputKeyJustPressed && WasRecentlyRendered → (var+1)%N → 保存 → Apply`，不看父条件；初始化依次读取全部存档（无存档则写初始选项，越界值 Clamp），最后 Apply 一次。
- 整个树是一个受管区域：注释框 `HT Toggle Tree - <名>;Apply=HTN_Apply_<名>;Switch=<变量>,<按键>,<选项数>,<父变量>,<父选项>,<标签>,<选项标签|…>,<空选项位>`（`<标签>` 从 v1.5.35 起写状态变量名，不再来自 `Comment`；无父时父变量为空、父选项 -1；未命名的空选项标签为空，供面板显示 `Hidden`；`<空选项位>` 每个选项一位，`1` 表示该选项下没有部件也没有子切换，面板据此把“两个选项、恰好一个为空”的切换显示成 `ON/OFF` 开关）。按名整体替换，不追加。
- v1.5.30 建的图里旧的中文插口名（如 `输入`、`物体`）在打开或加载时自动刷新为英文，连线不变。
- 贴图/材质切换（v1.5.36）：同一个 `HTN_Apply_<名>` 里先算材质、再算贴图、最后算可见性，所以一个材质 ID 可以同时被显隐、贴图切换和材质切换作用。只有被贴图/材质切换作用的槽会被 `SetMaterial`，其他槽不碰；每个这样的槽：先取原始材质（编译时从预览网格体读出），生效的 `Material Switch` 把它换成当前选项的材质；然后对每个贴图材质（`Part` 的 `By material` 材质，手动模式时是槽的原始材质），只有槽当前正是这个材质时，才在这个槽自己的 MID（首次需要时用 `CreateDynamicMaterialInstance` 创建，之后复用）上先把参数设回材质自己的贴图，再设当前选项的贴图，最后 `SetMaterial(槽, MID)`；否则 `SetMaterial(槽, 当前材质)`。槽的工作变量是 `HTN_<名>_Base<槽>` 和 `HTN_<名>_MID<槽>_<序号>`，不再使用的会在重新编译时删除。
- 冲突检查：同一个槽被两个可能同时生效的 `Material Switch` 作用、或同一个槽的同一个参数被两个可能同时生效的 `Texture Switch` 作用（同一个材质）时编译报错并选中节点；如果两者挂在同一个变量的不同选项下（互斥），则允许。参数不在目标材质上、贴图或材质加载失败、槽在预览网格体上没有材质时也报错。和原有非图切换重复控制同一个 ID 不做检查。
- 面板：贴图/材质切换的选项都算“有内容”（`<空选项位>` 全为 0），面板一律显示循环按钮，标题是变量名，未填的选项标签显示编号。

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
