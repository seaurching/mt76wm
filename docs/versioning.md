# 版本管理（versioning）

mtkwm 的版本号与构建号由 **`version.xcconfig`** 单点管理，**每次构建自动递增**。

## 策略

| 字段 | 规则 | 示例递增 | 暴露位置 |
|---|---|---|---|
| `MODULE_VERSION` | 次版本号 +1，补位归零 | 0.1.0 → **0.2.0** → 0.3.0 … | `CFBundleShortVersionString` |
| `BUILD_COUNT` | 每次构建 +1 | 1 → **2** → 3 … | `CFBundleVersion`（kext 的 `showloaded` 版本列） |

## 工作方式

```
version.xcconfig  ──(configFiles: base)──►  构建读取当前版本
                                              │
                                        构建成功
                                              ▼
scripts/bump_version.sh（post-build 阶段）──► 版本+1 写回 version.xcconfig
                                              ▼
                                     下一次构建使用新版本
```

- **唯一版本源**：`version.xcconfig`（`MODULE_VERSION` / `BUILD_COUNT`），不要在 project.yml 或代码里硬编码版本。
- **递增时机**：构建成功后的 post-build 阶段（project.yml 的 `postBuildScripts`），失败的构建不消耗版本号。
- **手动递增**（一般不需要）：`./scripts/bump_version.sh`
- **版本注入**：`project.yml` 通过 `configFiles: base: version.xcconfig` 引入；
  `CURRENT_PROJECT_VERSION = $(BUILD_COUNT)`，`mtkwm/Info.plist` 中
  `CFBundleShortVersionString = $(MODULE_VERSION)`、`CFBundleVersion = $(BUILD_COUNT)`。

## 为什么递增在构建后

版本由 xcconfig 在构建开始时解析。若在构建**前**递增，产物里的版本与递增动作会错位一次；
放在构建**后**，则"产物携带的版本"与"它被构建时的版本"永远一致，且每次构建都拿到唯一版本。

## 联调时怎么确认版本

```sh
kmutil showloaded --bundle-identifier com.mt76wm.mtkwm   # Version 列 = BUILD_COUNT
defaults read /path/to/mtkwm.kext/Contents/Info.plist CFBundleShortVersionString
```
