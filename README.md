# HT Blueprint Toggle Tool

这是一个 Unreal Engine 5 编辑器插件，用来一键生成「动画蓝图 + SaveGame 蓝图」里的材质显示/隐藏切换节点。

当前公开仓库只发布插件说明和二进制安装包，不公开插件 C++ 源码。

## 功能

- 在编辑器菜单 `Tools > HT Blueprint Toggle Tool` 打开工具面板。
- 右上角 `Settings` 设置窗口可以直接从工程里选择蓝图资产：
  - Anim Blueprint
  - SaveGame Blueprint
- 自动生成初始化逻辑：读取保存状态，并应用材质显示/隐藏。
- 自动生成更新逻辑：按键触发切换，并保存当前状态。
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

Release 包里包含 UE 5.6 Win64 的预编译插件 DLL。如果你的 UE 版本或工具链不同，Unreal 可能会提示插件需要重新编译。

## 安装方法

1. 打开本仓库的 `Releases` 页面。
2. 下载插件 zip 附件：

   ```text
   HTBlueprintToggleTool-v1.0.0-UE5.6-Win64-Binary.zip
   ```

3. 不要下载 GitHub 自动生成的 `Source code (zip)` 或 `Source code (tar.gz)`。
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
   - `Material ID`
5. 通常保持下面三个选项开启：
   - `Initialize graph`
   - `Update graph`
   - `Save assets`
6. 点击 `Generate Toggle Nodes`。

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
Anim Variable: Aa
Save Variable: AaSave
Save Slot: Aa
```

## 生成逻辑

初始化逻辑：

- 判断 Save Slot 是否存在。
- 如果存在，读取 SaveGame 并 Cast。
- 把 SaveGame 变量值同步到 AnimBP 变量。
- 根据变量值执行 `Show Material Section` 显示/隐藏。
- 如果不存在，创建默认 SaveGame，并保存当前默认状态。

更新逻辑：

- 检测按键是否按下。
- 检测组件最近是否渲染。
- 把 AnimBP 变量在 `0` 和 `1` 之间切换。
- 把切换后的状态写入 SaveGame 变量。
- 保存到 Save Slot。
- 根据变量值执行材质显示/隐藏：
  - `0`：显示
  - `1`：隐藏

## 注意事项

- 插件会追加生成新节点，不会自动删除旧节点。
- 如果之前生成过错误或很乱的节点组，建议先手动删除旧节点，再重新生成。
- 当前公开版本不包含 C++ 源码，主要用于 UE 5.6 Win64 编辑器环境。
- GitHub Release 页面会自动显示 `Source code (zip)` 和 `Source code (tar.gz)`，这是 GitHub 默认生成的仓库快照。正常安装插件时，请下载插件 zip 附件。

