# HT Blueprint Toggle Tool 项目交接文档

本文档用于交接当前 UE5 编辑器插件项目。接手者应先阅读本文，再看 `README.md` 和对应源码文件。

## 当前状态

- 插件名称：`HT Blueprint Toggle Tool`
- 插件目录：`D:/ueproject/HT/Plugins/HTBlueprintToggleTool`
- 当前版本：`1.5.11`
- UE 版本：Unreal Engine `5.6`
- 平台：Windows / Win64 / Editor 插件
- 当前 GitHub 仓库：`https://github.com/loyal37/NTE-mod-tools`
- 当前 Release：`https://github.com/loyal37/NTE-mod-tools/releases/tag/v1.5.11`
- 当前发布包：`HTToggleTool-v1.5.11.zip`
- 当前发布包 SHA256：`A2DDFA1F70B15B1343507EB91B4165A48BE2F19D03587D04A0E1ACA0F25C3042`

重要边界：

- Release zip 只包含运行插件需要的二进制文件，不包含 `Source` 和 `.pdb`。
- 如果不希望别人看到源码，需要在 GitHub 仓库权限层面设置为私有；仅发布二进制 zip 不会隐藏仓库里的源码。
- 本地插件目录可能长期显示 dirty 状态，原因是本地工作区历史和远端 main 曾经不完全对齐。不要随手 `git reset --hard` 或删除未确认文件。

## 插件用途

插件服务于异环 / NTE 角色 Mod 制作流程，主要解决以下工作：

1. 在动画蓝图中批量生成材质显示/隐藏切换节点。
2. 在动画蓝图中批量生成贴图循环切换节点。
3. 创建角色对应的 AnimBP 和 SaveGame 蓝图，并把 AnimBP 绑定到骨骼网格体的后期处理动画蓝图。
4. 分析骨骼网格体的材质槽，把使用同一个材质球的槽位分组后填入贴图切换参数。
5. 批量把角色文件夹中的材质球分配到骨骼网格体材质槽。
6. 重建指定材质的四贴图节点，并创建或更新材质实例。
7. 从 Cooked 输出中选择需要的资产，保持目录结构复制到打包目录，并可启动外部打包器。

## 入口和菜单

主要入口在 `Source/HTBlueprintToggleTool/Private/HTBlueprintToggleToolModule.cpp`。

- 菜单入口：`Tools > HT Blueprint Toggle Tool`
- 主窗口 Tab 名：`HTBlueprintToggleTool`
- 控制台命令：
  - `HTBlueprintToggleTool.Open`
  - `HTBlueprintToggleTool.OpenMaterialAnalysis`
  - `HTBlueprintToggleTool.OpenMaterialSlotMapper`

主面板实现：`Source/HTBlueprintToggleTool/Private/SHTBlueprintToggleToolPanel.cpp`

## 主要源码结构

### 模块和主 UI

- `HTBlueprintToggleToolModule.cpp/.h`
  - 注册菜单、Tab、控制台命令。
  - 持有当前主面板弱引用。

- `SHTBlueprintToggleToolPanel.cpp/.h`
  - 主工具面板。
  - Settings 窗口。
  - 材质显示/隐藏与贴图切换的表单。
  - Cooked Assets、Material Instance、Slot Materials 子工具入口。
  - 材质槽分析窗口。
  - 创建角色 AnimBP / SaveGame 并绑定骨骼网格体后期处理动画蓝图的逻辑。

### 蓝图节点生成

- `HTBlueprintToggleGenerator.cpp`
- `HTBlueprintToggleGenerator.h`

负责真正写动画蓝图和 SaveGame 蓝图节点。

关键能力：

- 创建/复用 AnimBP 变量。
- 创建/复用 SaveGame 变量。
- 生成初始化图和更新图。
- 支持材质显示/隐藏模式。
- 支持贴图切换模式。
- 支持单材质槽和多材质槽。
- 支持普通按键、符号按键、`ctrl`、`shift`、`alt` 组合键。
- `shift` 输入也兼容常见误输入 `shit`。

核心参数结构在 `FHTBlueprintToggleGeneratorParams`：

- `Mode`
- `AnimBlueprintPath`
- `SaveGameBlueprintPath`
- `ToggleVariableName`
- `SaveVariableName`
- `SlotName`
- `KeyName`
- `MaterialIDs`
- `MaterialElementIndices`
- `SourceMaterialPath`
- `TextureParameterName`
- `TexturePaths`
- `bGenerateInitializeGraph`
- `bGenerateUpdateGraph`
- `bSaveAssets`

### 材质实例创建

- `SHTMaterialInstanceCreator.cpp/.h`
- `HTMaterialInstanceBuilder.cpp/.h`

功能：

- 选择一个父级材质。
- 清空父级材质已有节点。
- 重新生成四贴图节点：
  - `BaseColor`
  - `ID_Tex`
  - `LightMap`
  - `NormalMap`
- 创建或更新同名材质实例。

注意：

- 这是破坏性操作，会删除所选材质原有节点。
- 执行前应确认该材质可以被重建。
- 依赖材质函数：`/InterchangeAssets/Functions/MF_PhongToMetalRoughness.MF_PhongToMetalRoughness`

### 材质槽分配

- `SHTMaterialSlotMapper.cpp/.h`

功能：

- 读取当前 AnimBP 预览骨骼网格体。
- 扫描 Settings 中的角色文件夹下的材质球和材质实例。
- 显示所有材质槽 ID、插槽名、当前材质。
- `Match Names`：材质球名称与插槽名完全一致时自动分配。
- 手动添加多条映射：选择材质球，输入一个或多个 Slot ID。
- `Use checked slots`：把左侧勾选的槽位填入当前映射，使用后左侧对应槽位变暗。
- `Apply Mappings`：写入骨骼网格体并保存资产。

### Cooked 资产导出

- `SHTCookedAssetExporter.cpp/.h`

功能：

- 选择 Cooked source。
- 选择 Output directory。
- 根据工程资产列表选择需要复制的 `.uasset`。
- 自动携带同名 `.uexp`、`.ubulk`、`.uptnl`。
- 保持 Cooked 文件夹结构输出。
- 记忆目录、勾选资产和导出选项。
- 可选导出后启动外部打包器。
- 可选导出后打开实际导出的角色目录。

外部打包器路径硬编码在源码中：

```text
D:/NTE Mod Packager/NTE Mod Packager.exe
```

如果打包器迁移，需要修改 `SHTCookedAssetExporter.cpp` 中的 `PackagerExecutable`。

## Settings 行为

Settings 目前管理三类路径：

- `Anim Blueprint`
- `SaveGame Blueprint`
- `Character Folder`

保存位置使用 `GEditorPerProjectIni`，配置节：

```text
HTBlueprintToggleTool.BlueprintSettings
```

配置键：

```text
AnimBlueprintPath
SaveGameBlueprintPath
CharacterFolderPath
```

Settings 中的 `Create AnimBP + SaveGame and bind Skeletal Mesh` 会执行：

1. 从 `Character Folder` 递归查找角色骨骼网格体，优先选择名称包含 `_skin` 的 `USkeletalMesh`。
2. 从角色文件夹名生成基础角色名。例如：
   - `/Game/Characters/Player/004_lacrimosa` -> `lacrimosa`
   - `/Game/Characters/Player/010_nanally` -> `nanally`
3. 创建或复用：
   - `lacrimosa_animbp`
   - `lacrimosa_save`
4. 设置 AnimBP 的 Skeleton 和 Preview Mesh。
5. 在 AnimGraph 中创建或复用 `Input Pose`，并连接到 `Output Pose`。
6. 编译 AnimBP 和 SaveGame BP。
7. 设置骨骼网格体的 `Post Process Anim Blueprint` 为新 AnimBP。
8. 保存 AnimBP、SaveGame BP、SkeletalMesh。
9. 把新路径写回 Settings。

## 命名规则

蓝图创建：

```text
Character folder: /Game/Characters/Player/004_lacrimosa
Base name:        lacrimosa
AnimBP:           lacrimosa_animbp
SaveGame BP:      lacrimosa_save
```

切换变量：

```text
Anim Variable = 用户输入
Save Variable = Anim Variable + Save
Save Slot     = Anim Variable
```

贴图切换：

- `Texture Parameter` 默认常用 `BaseColor`。
- 支持多张贴图，贴图数量决定 `Switch on Int` 的状态数量。
- 多个 Material Slot 可以共用同一变量和同一按键，生成时会用执行序列逐个设置每个 MID。

## 构建方式

推荐使用 UE 自带 UAT：

```powershell
$out = 'D:\ueproject\HT\Saved\BuildPlugin-v1.5.11'
if (Test-Path -LiteralPath $out) {
    Remove-Item -LiteralPath $out -Recurse -Force
}

& 'D:\ue\UE_5.6\Engine\Build\BatchFiles\RunUAT.bat' BuildPlugin `
    -Plugin='D:\ueproject\HT\Plugins\HTBlueprintToggleTool\HTBlueprintToggleTool.uplugin' `
    "-Package=$out" `
    -TargetPlatforms=Win64 `
    -Rocket
```

构建成功后，复制：

```text
Saved/BuildPlugin-vX.X.X/Binaries/Win64/UnrealEditor-HTBlueprintToggleTool.dll
Saved/BuildPlugin-vX.X.X/Binaries/Win64/UnrealEditor.modules
```

到：

```text
Plugins/HTBlueprintToggleTool/Binaries/Win64/
```

常见情况：

- UAT 可能提示 Visual Studio 编译器不是 preferred version，只要最终 `BUILD SUCCESSFUL` 就可用。
- 如果 Unreal Editor 正在运行，DLL 可能被锁住，复制会失败，需要先关闭编辑器。

## 发布包规则

Release zip 的根目录必须是：

```text
HTBlueprintToggleTool/
```

发布包应包含：

```text
HTBlueprintToggleTool/HTBlueprintToggleTool.uplugin
HTBlueprintToggleTool/README.md
HTBlueprintToggleTool/Binaries/Win64/UnrealEditor-HTBlueprintToggleTool.dll
HTBlueprintToggleTool/Binaries/Win64/UnrealEditor.modules
HTBlueprintToggleTool/Resources/
```

发布包不得包含：

```text
Source/
Intermediate/
Config/FilterPlugin.ini
*.pdb
*.obj
*.lib
*.exp
```

当前 zip 示例：

```text
D:\ueproject\HT\Saved\HTToggleTool-v1.5.11.zip
```

## GitHub 流程

因为本地插件目录可能 dirty，推荐用临时 worktree 从远端 main 开始提交：

```powershell
$repo = 'D:\ueproject\HT\Plugins\HTBlueprintToggleTool'
$wt = 'D:\ueproject\HT\Saved\repo-upload-vNEXT'

git -C $repo fetch --no-tags origin main
git -C $repo worktree add $wt origin/main
```

然后把需要提交的源码文件复制到 `$wt`，在 `$wt` 中提交并推送：

```powershell
git -C $wt add .
git -C $wt commit -m "Your change message"
git -C $wt push origin HEAD:main
git -C $wt tag -f vNEXT
git -C $wt push origin vNEXT --force
```

如果只是文档修改，不一定需要新 Release；如果修改了插件二进制或用户需要下载新版本，就需要新 tag 和 GitHub Release。

上传 Release 附件时，如果本机没有 `gh`，可以用 GitHub REST API 和 Git Credential Manager 中的 token。既有流程中已经这样发布过 `v1.5.11`。

## 测试清单

每次改动后建议至少检查：

1. UE5.6 `BuildPlugin` 是否成功。
2. 工程插件目录的 DLL 是否已更新。
3. `Tools > HT Blueprint Toggle Tool` 是否能打开。
4. Settings 是否记忆 AnimBP、SaveGame BP、Character Folder。
5. `Create AnimBP + SaveGame and bind Skeletal Mesh` 是否能：
   - 创建或复用两个蓝图。
   - AnimGraph 中存在 `Input Pose -> Output Pose`。
   - 骨骼网格体 `Post Process Anim Blueprint` 指向新 AnimBP。
6. 材质显示/隐藏模式生成节点后能在游戏中切换。
7. 贴图切换模式：
   - 两张贴图可切换。
   - 三张或更多贴图可循环切换。
   - 多个 Material Slot 共用变量时能同时切换。
8. 符号按键和组合按键：
   - `=`
   - `[`
   - `]`
   - `;`
   - `ctrl 6`
   - `shift 6`
   - `alt 6`
9. 材质槽分析窗口能显示材质球预览，不应显示成黑图或错误贴图预览。
10. Slot Materials 能自动匹配、手动映射并保存骨骼网格体。
11. Material Instance 工具能重建节点并创建实例。
12. Cooked Assets 能保持目录结构导出，并按选项启动打包器或打开输出目录。

## 已知风险和注意事项

- `HTMaterialInstanceBuilder` 会删除父级材质全部节点，这是设计行为，但风险很高。
- Cooked 导出器的外部打包器路径是硬编码路径，换机器需要改源码或后续改成设置项。
- 角色 AnimBP 创建逻辑默认选择角色文件夹下名称包含 `_skin` 的 Skeletal Mesh；如果一个角色文件夹里有多个候选骨骼网格体，可能需要后续增加手动选择。
- Settings 中的 Character Folder 是很多功能的共同上下文，路径错了会影响材质扫描、槽位映射和蓝图创建。
- Release 包不带源码，但 GitHub 仓库如果是公开的，源码仍然可见。
- 修改蓝图节点生成逻辑后，必须在 UE 里实测生成出来的蓝图接线；编译通过不等于节点行为正确。

## 下一步可优化方向

- 把外部打包器路径从硬编码改为 Settings 配置项。
- 创建角色蓝图时增加 Skeletal Mesh 选择器，避免多骨骼网格体时选错。
- 给蓝图生成结果增加更清晰的布局分组和注释框。
- 给 Release 自动化脚本加一个独立脚本文件，减少手动打包和上传步骤。
- 将用户可见文本统一检查一遍，避免中文乱码或中英混杂不一致。
