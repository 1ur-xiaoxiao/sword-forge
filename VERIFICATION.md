# 实际验证记录 · 2D 秘境版 3.1

日期：2026-10-08。测试使用独立临时项目或专用安卓模拟器，没有操作玩家电脑存档。

| 验收 | 结果 | 覆盖内容 |
|---|---:|---|
| C核心 | 23组通过 | 5000次真实锻造、数值、强化、首领、批量回收原子性、三层秘境、祝福、战败、V1/V2/V3迁移和V4战斗恢复 |
| 单文件EXE接口 | 10组通过 | 只复制EXE启动、8份内嵌资源、真实操作、四存档隔离与重启、损坏文件保护和批量回收 |
| Edge基础界面 | 12组通过 | 五页实际操作、巡逻/首领、采集、药水、刷新、1440/1024/768/390宽度与零脚本错误 |
| 2D场景 | 12组通过 | 实际C生成史诗/传说、紫/金动画、强化台与保护符、排序后批量选择、取消回收、四存档、手机血条和按钮同屏 |
| 随机秘境 | 6组通过 | 150种子6房间全部连通、A*行走、视野扩展、单次宝箱、地图/位置刷新、精确战斗冷启动、祝福、下楼和手机撤离 |
| Android环境 | 14组通过 | 真实WebView触摸、JNI核心、四存档、强化/回收/战斗、返回键、后台、横竖屏、秘境寻路、精英、祝福、下楼与精确战斗恢复 |

共77组验收通过。Android使用独立Android16/API36 x86_64模拟器，视口412×684。ARM64/ARM32/x86_64三个CPU架构完成原生C编译与APK打包；实体手机未测试。

正式签名APK 3.1.0 / versionCode30100已覆盖安装，保留测试旅程的351金币。检查确认无调试标记、无INTERNET权限、无JNI链接失败或应用崩溃。签名v2/v3、ZIP对齐通过；原生库采用16KB装载段对齐。

Windows用MinGW-w64 GCC16.1.0构建，开启Wall/Wextra/Wpedantic/Werror/Wformat。单文件EXE为Windows GUI子系统，只依赖KERNEL32、msvcrt、SHELL32、USER32、WS2_32系统DLL，无额外MinGW DLL。窗口使用本机Edge或默认浏览器。本版实测构建入口是build.ps1/package.ps1；CMake来源也已加入秘境模块。

完整六域养成测试从初始角色和装备出发，未注入额外资源，31场胜利清除6首领，达到18级、22次锻造和锻造师7级。安卓秘境流程使用正常开局装备与实际积累的资源完成第一层精英和第二层入口。

## 证据

- [浏览器](docs/verification/browser-verification.json)、[2D场景](docs/verification/2d-verification.json)、[随机秘境](docs/verification/dungeon-verification.json)、[安卓](docs/verification/android-verification.json)验证记录。
- [传说品质动画](docs/images/legendary-forge.png)、[手机同屏战斗](docs/images/mobile-combat.png)、[随机地图](docs/images/dungeon.png)、[工坊界面](docs/images/workshop.png)。
- 完整本地验收截图在 `outputs/`，重新执行测试可生成；该目录不纳入 Git 源码。

## 验证边界

使用Canvas2D绘制人物和地图；锻造以透视、遮挡、粒子、光束与台座表现空间感。秘境中可以自由行走，战斗房间采用回合制；没有宣称实时动作战斗或3D引擎。

秘境战斗完整保存。普通巡逻与六域首领遭遇沿用服务内状态，服务重启后结束遭遇，已扣体力与生命保留。Windows与安卓存档独立，没有自动跨设备同步。

rot.js原始发行文件及BSD-3-Clause许可均内置，两平台不用联网。固定版本与实际融合位置见docs/OPEN_SOURCE.md。
