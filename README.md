# HT Blueprint Toggle Tool

这是一个 Unreal Engine 5 编辑器插件，用来一键生成「动画蓝图 + SaveGame 蓝图」里的材质显示/隐藏切换节点。

## 功能

- 在编辑器菜单 `Tools > HT Blueprint Toggle Tool` 打开工具面板。
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
- 如果变量不存在，会自动创建需要的 `int` 变量。
- 只需要输入 `Anim Variable`，SaveGame 相关名称自动生成：
  - Save Variable：`AnimVariable + Save`
  - Save Slot：`AnimVariable`
- `Section Index` 和 `LOD Index` 默认固定为 `0`。
- 生成的节点会按区域排布，并放进注释框，方便后续查看和移动。

## 环境要求

- Unreal Engine 5.6
- Windows Win64 编辑器
- Visual Studio 2022 C++ 工具链
- .NET Framework Developer Pack / SDK 4.6 或更高版本

Release 包里包含 UE 5.6 Win64 的预编译插件 DLL。自定义 Binary zip 不包含 C++ 源码。

## 安装方法

1. 打开本仓库的 `Releases` 页面。
2. 下载插件 zip 附件：

   ```text
   HTBlueprintToggleTool-v1.1.3-UE5.6-Win64-Binary.zip
   ```

3. 安装插件时不要下载 GitHub 自动生成的 `Source code (zip)` 或 `Source code (tar.gz)`，请下载上面的 Binary zip 附件。
4. 关闭 Unreal Editor。
5. 解压后，把 `HTBlueprintToggleTool` 文件夹放到工程目录：

   ```text
   YourProject/Plugins/HTBlueprintToggleTool
   ```

6. 打开 `.uproject`。
7. 在编辑器中打开 `Tools > HT Blueprint Toggle Tool`。

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
- 仓库中的 `Source/` 是插件源码，主要用于二次开发或自行编译。
- GitHub Release 页面会自动显示 `Source code (zip)` 和 `Source code (tar.gz)`，这是 GitHub 默认生成的仓库快照。正常安装插件时，请下载插件 Binary zip 附件。
