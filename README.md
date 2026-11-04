# HT Blueprint Toggle Tool

An Unreal Engine 5.6 editor plugin that generates character material toggle Blueprints, assigns skeletal mesh material slots in bulk, creates four-texture material instances, and exports selected cooked assets.

## Features

- Generate show/hide Blueprints for one or several material sections.
- Cycle between material groups of different sizes with `5+6;8+10+12`; a semicolon cycle does not add a hide-all state.
- Generate multi-texture cycle Blueprints for one or several material slots.
- Generate runtime RGB channel gain tuners for the `LightMap` and `ID_Tex` of existing materials, without packaging new materials.
- Plain keys, symbol keys, and `ctrl`, `shift`, `alt` combinations.
- Create the character's AnimBP and SaveGame Blueprint in the character folder and bind the AnimBP as the skeletal mesh's post-process Anim Blueprint.
- Create the Anim Blueprint variable, the SaveGame variable, and the load/save logic from the Anim Variable name.
- In Texture switch mode, analyze the material slots of the AnimBP's preview skeletal mesh and fill Material Slot(s) grouped by material.
- Scan the materials in the character folder and assign them to skeletal mesh slots by slot name or in bulk.
- Create a four-texture material instance from a selected material.
- Pick cooked files to export from the project asset list, keeping the original folder structure.
- Optionally start an external packager after exporting.
- Optionally open the exported character folder after exporting.
- Toggle Tree: a node graph for nested toggles, with Texture Switch and Material Switch nodes (see below).

## Installation

1. Download from GitHub Releases:

   ```text
   HTToggleTool-v<version>.zip
   ```

2. Close Unreal Editor.
3. Put the `HTBlueprintToggleTool` folder from the archive into:

   ```text
   YourProject/Plugins/HTBlueprintToggleTool
   ```

4. Open the project and start the plugin from `Tools > HT Blueprint Toggle Tool`.

The release archive only contains the files the UE 5.6 Win64 editor needs to run the plugin; it has no `Source` and no PDB. Building from source requires the Visual Studio 2022 C++ toolchain.

## Blueprint toggles

In `Settings`, choose the Animation Blueprint, the SaveGame Blueprint and the character folder. The choices are saved in the project configuration and are still there after closing and reopening the tool. When the character folder changes, the tool looks for the AnimBP and SaveGame Blueprint in that folder and fills in the paths above. The character folder is also where the tool looks for the character's materials.
`Create AnimBP + SaveGame and bind Skeletal Mesh` creates an Animation Blueprint and a SaveGame Blueprint named after the character folder, connects `Input Pose` to `Output Pose` in the AnimGraph, and sets the Animation Blueprint as the character skeletal mesh's post-process Anim Blueprint.

`Function Switch` selects the function:

- `Material visibility`: show/hide material sections and cycle material groups. `+` joins Material IDs that show together in one state; a semicolon cycles only the listed groups, a comma adds a final "hide all" state. `Initial State` sets the state used before any save exists; it does not change the state order.
- `Texture switch`: cycle the textures of a material.
- `Material switch`: cycle one or several material slots through chosen materials; the material list is limited to the current Character Folder.
- `RGB tuner`: generate runtime RGB gain nodes for the `LightMap`, the `ID_Tex`, or both, of an existing material.
- `Material Instance`: rebuild the material nodes and create a material instance.
- `Slot Materials`: assign materials to the skeletal mesh's material slots.

In `Texture switch` mode, `Analyze` next to `Material Slot(s)` analyzes the current AnimBP's preview skeletal mesh and groups the slots that use the same material. Picking a group fills in all of its slot IDs and the `Source Material`. The group list shows each material's thumbnail.
When picking `Texture 1/2/...`, the texture list is limited to the character folder chosen in `Settings` and its subfolders, so textures of other characters are not picked by mistake.

### Material group cycles (v1.5.24)

Type them into the existing `Material ID(s)` box of `Material visibility`; no extra group panel is needed.

| Input | Cycle |
| --- | --- |
| `5+6;8+10` | show 5/6 -> show 8/10 -> back to 5/6 |
| `5+6;8+10+12` | show 5/6 -> show 8/10/12 -> back to 5/6 |
| `5+6,8+10+12` | show 5/6 -> show 8/10/12 -> hide all -> back to 5/6 |
| `16` or `13+20` | the original show/hide toggle |

Full-width semicolons, commas and plus signs typed with a Chinese input method work too, and spaces around separators are allowed; groups may have different sizes. Entering a group shows all of its IDs and hides the other groups of the input; IDs that are not listed are not touched. Semicolons and commas cannot be mixed, IDs cannot repeat, and groups cannot be empty.

`Initial State` starts at 0: a two-group semicolon cycle allows 0/1, a two-group comma cycle allows 0/1/2 (2 = hide all). The key and the SaveGame still share one state variable; an existing save wins over the default. After changing an old comma cycle to a semicolon cycle, regenerate and cook the AnimBP; with NTE Panel System, also rescan and regenerate the panel so it knows the new state count.

### RGB channel tuner

`RGB tuner` is for temporarily previewing how the three channels of `LightMap` and `ID_Tex` affect the character's look; once the values are settled, edit the source textures in an external image tool. It writes no SaveGame and does not change the state order of material or texture switches.

1. Choose the AnimBP and click `Analyze` next to `Material Slot(s)`.
2. Pick the target material group; the tool fills in every slot that uses the material and reads its `LightMap` and `ID_Tex` parameter textures.
3. Tick the textures to tune and click generate.
4. Scan and generate the in-game slider panel with NTE Panel System.

v1.5.28 restores the earlier gain implementation at the user's request (including PNG export and the single-mip fix) and withdraws the black/white blend algorithm and the later loading gate and diagnostics changes. The other material grouping, toggle and export tools are not rolled back.

Each channel uses a gain of `0` to `2`; the default and Reset value is `1`. The result is `clamp(source pixel x gain, 0, 255)`: 160 x 2 clamps to 255, 60 x 2 gives 120, and pixels that were 0 stay 0. A gain does not guarantee that every source value can reach the full 0 to 255 range.

Initialization creates the slot MIDs, reads the source textures and allocates the preview render targets, as in the earlier implementation; every Apply redraws from the cached source texture and never accumulates the previous result. Reset restores the source textures and a `(1,1,1,1)` gain. Restoring the earlier implementation also drops the lazy initialization, full material reference restore, and streaming wait and checks of v1.5.25 to 1.5.27; this release does not claim to have found the cause of the earlier in-game problem.

`Export PNG` on the panel refreshes the render target with the current gains, then exports `<source texture name>_RGB.png` to the runtime `Saved` folder, currently `%LOCALAPPDATA%\HT\Saved` for the game. An export with the same name overwrites the old file; the uncompressed source PNG is never modified. In-game texture compression affects the exported pixels.

The preview LightMap and ID render targets have automatic mips turned off and only use the full-size level that was drawn, so turning the camera or zooming out never samples a stale smaller level. Distant previews may alias more; after Reset the source texture and its own mips are used again. Values near an ID boundary may still change the material type.

Rolling back requires regenerating the RGB tuner with the same `Tuner Name`, then rescanning and regenerating the panel with NTE Panel System v0.1.17 or later, and cooking the AnimBP, BPI and WBP again. The gain variables are named `_Gain` again and the metadata has no `Mode`; the panel refuses tuners that still carry a mode field, so the two algorithms are never mixed. Replacing only the plugin DLL does not change Blueprints that were already generated or cooked.

### Toggle Tree (v1.5.29 to v1.6.0)

LoyalTools-style nested toggles: any option of an outer switch can hold further switches, and a part is visible when every "variable == option" condition on its path holds (AND). The feature is independent of `Material visibility` and the other modes and does not change their generation, their `5+6;8+10` syntax, or their panel scan. All nodes and UI of this feature are in English.

Usage:

1. In `Settings`, choose the Animation Blueprint, the SaveGame Blueprint and the character folder, then click `Open Blueprint` next to `Slot Materials`. The plugin creates (or opens) a `TG_<Anim Blueprint name>` asset next to the Animation Blueprint and opens the graph editor. Clicking empty graph space shows the asset properties in `Details`: `Anim Blueprint`, `SaveGame Blueprint`, `Character Name` (save slot suffix), `Tree Name` (managed region name) and `Save Blueprints After Compile`.
2. Right-click the graph to add nodes: `Part`, `Switch`, `Texture Switch`, `Material Switch`, `Group`, `Generate`. Their settings are edited on the nodes:
   - `Switch`: `Comment`, `Variable` and `Key` text boxes (`Comment` is a note for the graph only and is not shown in game; the panel row is named after the state variable). `Option count` and `Initial option` change with `+` / `-`; changing the option count adds or removes `Option N` input pins at the bottom and keeps the links on the remaining ones. The small text box next to each `Option N` pin is that option's label for the panel; empty options show as `Hidden`.
   - `Part`: `Comment` and `Material IDs` (for example `1, 2, 12`, typed by hand). A part made from a material shows `From material <material>`; on compile its slots are looked up again on the current mesh, so it follows changes to the mesh's material assignment, and typing IDs by hand stops following the material. With the node selected, `Details` still lists the preview mesh's slots as checkboxes. A part's `Out` can have several links: one visibility link (a switch option, a group or `Generate`) plus any number of links to Texture Switch `Target` and Material Switch `Slots` inputs; dragging it to another visibility input moves only the visibility link.
   - `Texture Switch`: the same header as `Switch` (`Comment`, `Variable`, `Key`, `Option count`, `Initial option`, a label per option), plus `Add part from material...` and `Parameter`. `Add part from material...` lists the materials of the preview mesh with thumbnails and slot numbers; picking one places a `Part` holding every slot that uses the material to the left of the node (its comment is the material name) and connects it to the next free `Target`. Materials already connected are greyed out and never added twice, and the step can be undone. `Parameter` is the texture parameter to set; the list next to it shows the texture parameters every target material has, and a name can also be typed, for example `BaseColor`. The `Target N` inputs on the left take parts only, one part per input, and always end with one free input. Each `Target` row shows the part's slots and material and one texture picker per option; an empty option keeps the material's own texture. This corresponds to the "material groups" of the main panel's Texture mode.
   - `Material Switch`: the same header as `Switch`, also with `Add part from material...` (the new part connects to the next free `Slots` input). Each option has a label and a material picker (texture and material pickers show thumbnails); an empty option restores the slot's original material (the material on the preview mesh). The `Slots N` inputs on the left take parts only; the slots of all parts are combined.
   - The `Out` of a `Texture Switch` or `Material Switch` connects to `Generate`, a `Group`, or an option of a `Switch` (on an option it only acts while that option is active, and the panel dims it with the parent). They have no option pins, so nothing can hang on them. Like `Switch`, they share state variables by key or variable name, also across kinds (the option count and initial option must match).
   - `Group`: `Comment`. Its inputs grow and shrink by themselves: there is always exactly one free `In N` at the end; connecting the last input adds a new free one, and disconnecting trims surplus free inputs at the end down to one. Free inputs in the middle stay, so the order of the other links never changes.
   - `Generate`: the root of the tree; its inputs follow the same rules as `Group`.
   Every edit on a node can be undone and redone, and the selected node's settings can also be changed in `Details`.
   - Collapse: `Part`, `Switch`, `Texture Switch` and `Material Switch` have a collapse / expand arrow at the right of the title bar. A collapsed node shows only its title, a one-line summary (for example `Key alt 6 · 2 options` or `IDs 10, 11`; a Texture Switch also shows its parameter) and all of its pins (a `Switch` shows its option labels next to the option pins). Links stay as they are, and collapsed nodes can still be connected. The state is saved per node, can be undone and redone, and is kept by copy and paste. Right-clicking a node offers `Collapse` / `Expand` and `Collapse all` / `Expand all`; right-clicking empty space offers `Collapse all` / `Expand all` under `View`.
   - Zoom: the mouse wheel zooms past 1:1 without holding Ctrl, up to 4x; the default is still 1:1.
   - Copy, cut, paste and duplicate (`Ctrl+C` / `Ctrl+X` / `Ctrl+V` / `Ctrl+D`): links between nodes copied together are kept, links to nodes that were not copied are dropped, and the free inputs of `Group` / `Generate` shrink accordingly. Nodes can also be pasted into another `TG_` asset, and pasting can be undone and redone. The `Generate` node cannot be copied. v1.5.34 fixed an editor crash (`Assertion failed: OtherPin`) when pasting several linked nodes, or a linked node into another asset.
3. Links: the `Out` of a part or switch connects to a switch option, a group or `Generate`. A switch or group output has one link; connecting it again replaces the old link. Loops are not allowed. Nodes that do not reach `Generate` are ignored.
4. Click `Compile` on the toolbar. The graph is turned into a condition model and checked (switches with the same key share a variable and must have the same option count and initial option, a material ID can only be in one visible part, keys must be recognized, and so on). Errors are listed in the `Result` panel and the nodes are selected; otherwise the generator writes the Animation Blueprint and, by default, saves both Blueprints.
5. Rescan and regenerate the panel with NTE Panel System; every state variable of the tree becomes one row.

The generator entry point `FHTBlueprintToggleGenerator::GenerateNestedVisibility` (condition model `FHTNestedToggleGraph`: switches with a `Kind` of visibility, texture or material, parts, and each slot's original material in `SlotMaterialPaths`) can also be called directly.

- Switches with the same key share one state variable (typing the same variable name does the same) and change together; their option count and initial option must match, otherwise compile fails and names the nodes. Without a variable name, the variable is named after the key as `<Tree Name>_<Key>` (for example `Typhoeus_Alt_Six`); naming one node of a group is enough.
- A material ID can only be shown through one path / one part; otherwise compile fails and names both parts. Empty options can be anywhere and need no special marking.
- Keys use the existing key parser (plain keys, symbol keys, `ctrl/shift/alt` combinations); hold is not supported.
- What is generated: one AnimBP int variable and one SaveGame variable per state variable (save slot `<variable> <character name>`); a `HTN_Apply_<name>` custom event that calls `ShowMaterialSection` with each part's AND condition and leaves IDs outside the graph alone; in `BlueprintUpdateAnimation`, one block per variable: `WasInputKeyJustPressed && WasRecentlyRendered -> (var+1)%N -> save -> Apply`, without checking parent conditions. Initialization reads every save (writes the initial option when there is none, clamps out-of-range values) and finally runs Apply once.
- The whole tree is one managed region, replaced by name as a whole and never appended. Its comment box reads `HT Toggle Tree - <name>;Apply=HTN_Apply_<name>;Switch=<variable>,<key>,<option count>,<parent variable>,<parent option>,<label>,<option labels|...>,<empty options>`. Since v1.5.35 `<label>` is the state variable name, no longer the `Comment`. Without a parent the parent variable is empty and the parent option is -1. Unnamed empty options have empty labels, which the panel shows as `Hidden`. `<empty options>` has one character per option, `1` meaning nothing hangs on that option; the panel uses it to show a switch with two options, exactly one of them empty, as an `ON/OFF` switch.
- Graphs created with v1.5.30 stored Chinese pin names; they are refreshed to English when the graph is opened or loaded, and the links stay.
- Texture and Material switches (v1.5.36): the same `HTN_Apply_<name>` computes materials first, then textures, then visibility, so one material ID can be shown or hidden, get another texture and get another material at the same time. Only slots that a Texture or Material switch acts on are set with `SetMaterial`; other slots are not touched. For each such slot: start from the original material (read from the preview mesh at compile time); an active `Material Switch` replaces it with its current option's material; then, for each texture material (the material the `Part` was made from, or the slot's original material for a hand-typed part), only while the slot currently shows that material, the slot's own MID (created with `CreateDynamicMaterialInstance` the first time and reused afterwards) first gets the parameter set back to the material's own texture, then the current option's texture, and the slot is set to the MID; otherwise the slot is set to its current material. The working variables are `HTN_<name>_Base<slot>` and `HTN_<name>_MID<slot>_<index>`; unused ones are removed when the tree is compiled again.
- Conflicts: compile fails and selects the nodes when one slot is acted on by two `Material Switch` nodes that can be active at the same time, or one parameter of one slot (of the same material) by two `Texture Switch` nodes that can be active at the same time. Two switches hanging on different options of the same variable never act together and are allowed. A parameter missing from a target material, a texture or material that cannot be loaded, or a slot without a material on the preview mesh also fails compile. IDs that are also controlled by the non-graph toggles are not checked.
- Panel: every option of a Texture or Material switch counts as used (`<empty options>` is all zeros), so the panel always shows a cycle button for them; the row is named after the variable, and unnamed options show their number.

Examples:

```text
Material ID(s): 13,20
Material ID(s): 13+20
Material ID(s): 1+2,3+4
Material Slot(s): 12,13
Key: ctrl 6
Key: shift 6
Key: alt 6
```

SaveGame names are derived from the `Anim Variable`:

```text
Save Variable = AnimVariable + Save
Save Slot = AnimVariable + character name
```

## Slot Materials

Click `Slot Materials` in the `Function Switch` row of the main panel.

The tool reads the current AnimBP's preview skeletal mesh and lists every material slot with its ID, slot name and current material. It also scans the character folder from `Settings` recursively and lists its materials and material instances.
Slot names are shown in a larger bold font; when picking a material, the drop-down list and the current selection show material previews, so similarly named materials are easy to tell apart.
`Use checked slots` dims the matching slots on the left to mark them as assigned and clears the current ticks, ready for the next group. The slot list on the left supports Shift range ticking; `Refresh` on the right regenerates the material previews.

Actions:

- `Match Names`: assign each material to the slot with exactly the same name.
- `Add`: add a mapping row, pick a material, and type one or more slot IDs.
- `Use checked slots`: fill the ticked slot IDs on the left into the current mapping row.
- `Apply Mappings`: write all mappings to the skeletal mesh at once.

Example:

```text
Slot IDs: 1,5,15,16,17,18,19,23
Material: MI_player_010_female_cloth_b_Inst2
```

## Material Instance tool

Click `Material Instance` in the `Function Switch` row of the main panel.

1. Choose the `Material` to modify.
2. `Instance Name` defaults to the material name plus `_Inst` and can be changed.
3. Choose the four textures:

   ```text
   BaseColor
   ID_Tex
   LightMap
   NormalMap
   ```

4. Click `Rebuild Material and Create Instance`.

This first deletes every node of the selected material, then creates the required nodes and links again. Do not run it on a material whose existing nodes must be kept.

The tool recreates these material parameter nodes:

| Parameter | Sampler type | Connection |
| --- | --- | --- |
| `BaseColor` | Color | `RGB -> Lerp B` |
| `ID_Tex` | Linear Color | `RGB -> MF_PhongToMetalRoughness.SpecularColor` |
| `LightMap` | Linear Color | `RGB -> MF_PhongToMetalRoughness.AmbientColor` |
| `NormalMap` | Normal | `RGB -> Material Normal` |

It also recreates:

- a black constant into `Lerp A`
- the `DiffuseColorMapWeight` scalar parameter into `Lerp Alpha`
- `Lerp` into `MF_PhongToMetalRoughness.DiffuseColor`
- the value `25` into `MF_PhongToMetalRoughness.Shininess`
- the function's `BaseColor / Metallic / Specular / Roughness` outputs into the matching material inputs

An existing material instance with the same name is updated instead of created again.

## Cooked asset export

Click `Cooked Assets` at the top right of the main panel.

1. `Cooked source`: the character's cooked folder.
2. `Output directory`: the character's parent folder in the external packager.
3. Tick the project assets to export.
4. Click `Export selected assets`.

Options:

- `Overwrite existing files`: overwrite files that already exist.
- `Export and package`: start the external packager after a successful export.
- `Open output directory after export`: open the exported character folder after copying.

Folders, ticked assets and export options are saved in the project's editor configuration.

When a `.uasset` is picked, the plugin also takes the files with the same name that exist:

```text
.uasset
.uexp
.ubulk
.uptnl
```
