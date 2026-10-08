# 剑炉物语 · 安卓版

## 安装和游玩

安装包：项目 `outputs/剑炉物语-安卓版.apk`。将APK发到安卓手机（USB、网盘或聊天文件均可），点击文件，按手机提示允许该来源安装，再打开「剑炉物语」。不需要运行电脑服务。

- 最低系统设为 **Android 8.0 / API 26**；使用手机的系统WebView，建议保持WebView更新。
- 包含ARM64、ARM32以及模拟器x86_64三种原生C核心。原生库按16KB页面对齐，兼容相应页面大小设备的装载要求。
- 六域地图、首领、事件、锻造、成长与Windows版使用同一份C源码和JSON操作层。
- APK内包含全部页面、脚本和插画，不申请联网、文件存储或账户权限，离线游玩。
- 手机和电脑存档独立，当前没有云同步或跨设备导入界面。

## 手机适配

扩大主要按钮和地标触控区，采用底部五页导航，配方双列、背包双列和易读的事件卡片。系统状态栏与手势栏由原生布局留出空间。支持横竖屏切换，当前进度和战斗保留。

返回键先关闭弹窗，再返回工坊，在工坊按返回会询问是否保存退出；设置也提供保存退出。切换到后台时保存进度。强制结束进程后重开读取磁盘存档，未选完的探索事件继续保留；秘境战斗恢复敌人血量、回合和技能冷却，普通巡逻与六域首领遭遇结束，已消耗生命、体力保留。

## 存档和更新

四个存档位位于应用私有目录 `files/saves/`，旅程1为 `game.sav`，旅程2～4为 `game.slot2.sav`～`game.slot4.sav`，无需访问手机公共存储。每次有效操作自动保存，V4格式兼容V1/V2/V3。新旅程只重置当前槽位并先备份。

新版本用相同签名覆盖安装，可保留进度；卸载应用会删除本机存档，当前未启用云备份。项目的 `android/.private/` 保存本机生成的签名密钥，后续重新打包请保留这个文件夹。公开源码包不包含签名密钥，也不包含任何玩家存档。

## 结构和构建

```text
android/app/src/main/AndroidManifest.xml     包名、版本和最低系统
android/app/src/main/java/.../MainActivity.java
                                            原生WebView、触控、返回、生命周期
android/app/src/main/cpp/android_bridge.c    JNI调用共用C接口
android/app/src/main/cpp/CMakeLists.txt      原生库构建定义
android/app/src/main/res/                   原创图标和主题
android/tools/build_apk.py                  APK完整构建、签名和校验
build-android.ps1                           一键构建入口
src/game_api.c + include/game_api.h         Windows/安卓共用JSON接口
src/game.c/data.c/forge.c/adventure.c/delve.c/save.c 原来的C游戏核心
web/                                       两个平台共用的前端
```

此项目以直接调用SDK工具的方式构建，不需要Gradle网络下载。执行：

```powershell
powershell -ExecutionPolicy Bypass -File build-android.ps1
```

本机已验证环境：Android SDK的Platform35、Build Tools36.1.0、NDK26.1.10909125，以及Android Studio1内的JDK21。脚本会自动在英文路径创建编译快照，解决Windows版aapt2无法处理中文目录的问题，产物回写项目outputs。

SDK默认从 `%LOCALAPPDATA%/Android/Sdk` 获取。其他电脑可设置 `ANDROID_SDK_ROOT`、`FORGE_JAVA_HOME`、`FORGE_ANDROID_BUILD_DIR` 后构建。`FORGE_JAVA_HOME` 要指向含java/javac/keytool的完整JDK目录。安卓工具只用于编译，手机游玩无需安装开发工具。

测试包可用 `build-android.ps1 -DebugApp` 生成，只用于本地WebView检查；交付的中文名APK是正式签名包，未开启调试。测试脚本 `tests/android_check.cjs` 在专用模拟器里执行，需要ADB和支持WebSocket的Node，不影响电脑玩家存档。

实现参考：[Android官方本地WebView内容文档](https://developer.android.com/develop/ui/views/layout/webapps/load-local-content)。本项目拦截白名单HTTPS本地资源并阻止其他地址，前端经JNI调用C核心，手机端不启动HTTP服务器。

## 3.1 版的玩法更新

手机上具备可行走的 2D 工坊、史诗紫光/传说金光、独立强化台、默认开启音效、四段旅程、批量回收、战斗旁的血条和三层随机秘境。触碰解锁音效，扬声器按钮可记住静音。点选秘境房间自动行走，也提供屏幕方向按钮。

在设置中选择独立存档位，原来的手机角色保留为旅程 1。请用新版 APK 覆盖安装，保留原应用的数据；卸载/清除数据会删除手机存档。秘境战斗支持关闭后恢复敌人血量、回合与技能冷却，其他普通遭遇沿用原规则。

秘境里的地图、寻路、迷雾使用 BSD-3-Clause 的 rot.js 库；完整许可随 APK 内置，见旅程设置和 `docs/OPEN_SOURCE.md`。

## 已执行验证

独立 Android16/API36 模拟器中执行了触控、锻造、装备、采集与事件、技能战斗、多存档切换与冷启动、升级台、批量回收、横竖屏、后台保存、返回键，以及实际秘境行走、宝箱、精英祝福与下楼检查。记录见 [`android-verification.json`](../docs/verification/android-verification.json)。

正式签名包覆盖测试包后，再检查保存的金币与界面；签名 v2/v3、ZIP 对齐和三个架构原生库均验收。实际运行覆盖 Android16 x86_64 模拟器；ARM32/ARM64 已编译打包，实体手机尚未验证。

截图包括 `outputs/android-2d-combat.png`、`android-2d-upgrade.png`、`android-random-dungeon.png` 和 `android-release-phone.png`。
