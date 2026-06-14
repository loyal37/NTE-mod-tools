# HT Blueprint Toggle Tool

这是一个 Unreal Engine 5 编辑器插件，用来一键生成「动画蓝图 + SaveGame 蓝图」里的材质显示/隐藏与贴图切换节点。

## 功能

- 在编辑器菜单 `Tools > HT Blueprint Toggle Tool` 打开工具面板。
- 主面板右上角提供 `Cooked Assets` 入口，可以按工程中的原始资产选择需要复制的烘焙文件。
- 右上角 `Settings` 设置窗口可以直接从工程里选择蓝图资产：
  - Anim Blueprint
  - SaveGame Blueprint
- 支持单材质显示/隐藏切换。
- 支持多材质循环切换，例如 `13,20`：
  - 状态 0：显示 13，隐藏 20
  - 状态 1：隐藏 13，显示 20
  - 状态 2：隐藏 13，隐藏 20
- 多材质模式会自动生成对应数量的 `Switch on Int` 引脚，并自动设置取余数量。
- 自动生成初始化逻辑：读取保存状态，并应用材质显示/隐藏。
- 自动生成更新逻辑：按键触发切换，并保存当前状态。
- `Key` 支持直接输入符号按键，例如 `=`, `[`, `]`, `;`，会自动转换为 UE 正确的按键名。
- `Key` 支持组合按键，例如 `ctrl 6`、`shift 6`、`alt 6`。
- 自动生成 `Show Material Section` 节点。
- 支持两张 `Texture2D` 贴图循环切换。
- 贴图模式会自动创建动态材质实例，并生成 `Set Texture Parameter Value` 节点。
- 可设置材质槽编号和 Texture Parameter 名称，例如 `BaseColor`、`ID_Tex`、`LightMap`、`NomralMap`。
- 贴图资产名称可以不同，不需要额外打包材质实例；参数名称必须与游戏原材质一致。
- 如果变量不存在，会自动创建需要的 `int` 变量。
- 只需要输入 `Anim Variable`，SaveGame 相关名称自动生成：
  - Save Variable：`AnimVariable + Save`
  - Save Slot：`AnimVariable`
- `Section Index` 和 `LOD Index` 默认固定为 `0`。
- 生成的节点会按区域排布，并放进注释框，方便后续查看和移动。
- 烘焙源目录和输出目录会保存在当前工程的编辑器配置中，关闭窗口或重启 UE 后仍会保留。
- 资产列表来自工程中与 Cooked 目录对应的 `Content` 文件夹，以缩略图、资产名称、类型和子目录显示；尚未烘焙的资产会标记为 `Not cooked`。
- 每个角色目录的资产勾选状态会自动保存，再次打开窗口时会恢复上次选择。
- 烘焙资产导出时以工程 `.uasset` 为选择单位，并自动携带同名 `.uexp`、`.ubulk`、`.uptnl` 文件。
- 导出会保留角色文件夹和内部子目录结构，不会自动复制未勾选的骨骼、物理资产或材质实例。

## 环境要求

- Unreal Engine 5.6
- Windows Win64 编辑器
- 使用 Release 二进制版本不需要 Visual Studio。
- 只有自行编译源码时才需要 Visual Studio 2022 C++ 工具链和对应 SDK。

## 安装方法

1. 打开本仓库的 `Releases` 页面。
2. 下载插件 zip 附件：

   ```text
   HTToggleTool-v1.3.0.zip
   ```

3. 关闭 Unreal Editor。
4. 解压后，把 `HTBlueprintToggleTool` 文件夹放到工程目录：

   ```text
   YourProject/Plugins/HTBlueprintToggleTool
   ```

5. 打开 `.uproject`。
6. 在编辑器中打开 `Tools > HT Blueprint Toggle Tool`。

## 使用方法

1. 打开 `Tools > HT Blueprint Toggle Tool`。
2. 点击右上角 `Settings`。
3. 选择：
   - Anim Blueprint
   - SaveGame Blueprint
4. 回到主面板，填写：
   - `Anim Variable`
   - `Key`
   - `Material ID(s)`
5. 如果只切换一个材质，不勾选 `Multiple materials`，例如：

   ```text
   Material ID(s): 16
   ```

6. 如果切换多个材质，勾选 `Multiple materials`，例如：

   ```text
   Material ID(s): 13,20
   ```

7. 通常保持下面三个选项开启：
   - `Initialize graph`
   - `Update graph`
   - `Save assets`
8. 点击 `Generate Toggle Nodes`。

### 贴图切换

1. 将 `Toggle Type` 切换为 `Texture switch`。
2. 填写 `Anim Variable` 和触发按键。
3. `Material Slot` 填写骨骼网格体材质列表中的元素编号，从 `0` 开始。
4. `Texture Parameter` 填写游戏原材质的贴图参数名，例如 `BaseColor`。
5. 分别选择 `Texture A (State 0)` 和 `Texture B (State 1)`。
6. 保持 `Initialize graph` 开启，然后生成节点。

插件会自动创建 `AnimVariable + MID` 变量。初始化时为指定材质槽创建动态材质实例；按键触发后在两张贴图之间循环，并把状态保存到 SaveGame。

## 烘焙资产导出

1. 点击主面板右上角、`Settings` 左侧的 `Cooked Assets`。
2. `Cooked source` 选择已烘焙角色资产目录，例如：

   ```text
   Saved/Cooked/Windows/HT/Content/Characters/Player/010_nanally
   ```

3. `Output directory` 选择外部打包器中的角色父目录，例如 `.../Content/Characters`。
4. 插件会把 Cooked 路径映射到工程对应的 `Content` 文件夹，并在列表中显示工程资产及其类型。
5. 在列表中勾选需要打包的工程资产；没有对应烘焙文件的项目会显示 `Not cooked`。
6. 点击 `Export selected assets`。

勾选 `Export and package` 后，插件会在导出成功后启动：

```text
D:/NTE Mod Packager/NTE Mod Packager.exe
```

该选项会记住上次的勾选状态。

两个目录会自动保存，关闭窗口或重启编辑器后不会重置。输出时会自动建立角色目录，例如：

```text
输出目录/010_nanally/nanally_animbp.uasset
输出目录/010_nanally/NEW_ter/ter/cloth_ter/T_player_010_nanally_01_d.uasset
```

选择一个 `.uasset` 时，插件会自动复制同名伴随文件。例如选择：

```text
T_player_010_nanally_01_d.uasset
```

会自动复制存在的：

```text
T_player_010_nanally_01_d.uasset
T_player_010_nanally_01_d.uexp
T_player_010_nanally_01_d.ubulk
T_player_010_nanally_01_d.uptnl
```

没有勾选的骨骼、物理资产、材质实例等文件不会输出。

## 按键输入

`Key` 可以填写数字、字母、UE 按键名，也可以直接填写常见符号：

```text
=, [, ], ;, ,, ., /, \, -, ', ", `
```

符号会自动转换为 UE 内部按键名：

```text
= -> Equals
[ -> LeftBracket
] -> RightBracket
; -> Semicolon
```

也兼容部分中文或全角写法，例如 `等号`、`分号`、`左中括号`、`右中括号`、`＝`、`；`。

组合按键目前支持 `ctrl`、`shift` 和 `alt`：

```text
ctrl 6
shift 6
alt 6
ctrl =
shift ;
alt [
```

输入 `ctrl 6` 时，插件会生成：

```text
6 Just Pressed
AND
(Left Ctrl Down OR Right Ctrl Down)
AND
Was Recently Rendered
```

输入 `shift 6` 时同理，会使用左/右 Shift。

输入 `alt 6` 时同理，会使用左/右 Alt。

## 命名规则

你只需要手动输入 `Anim Variable`。

例如：

```text
Anim Variable: Glove
Save Variable: GloveSave
Save Slot: Glove
```

再例如：

```text
Anim Variable: ChestCloth
Save Variable: ChestClothSave
Save Slot: ChestCloth
```

## 多材质规则

多材质输入 `13,20` 时，变量会在 `0 / 1 / 2` 之间循环：

```text
0 -> show 13, hide 20
1 -> hide 13, show 20
2 -> hide 13, hide 20
```

如果输入 `13,20,25`，变量会在 `0 / 1 / 2 / 3` 之间循环：

```text
0 -> show 13, hide 20, hide 25
1 -> hide 13, show 20, hide 25
2 -> hide 13, hide 20, show 25
3 -> hide 13, hide 20, hide 25
```

## 注意事项

- 插件会追加生成新节点，不会自动删除旧节点。
- 如果之前生成过错误或很乱的节点组，建议先手动删除旧节点，再重新生成。
