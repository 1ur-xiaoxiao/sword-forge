# 更新与版本发布

仓库：https://github.com/1ur-xiaoxiao/sword-forge

`main` 保存持续开发的源码；提交记录回答“改了什么”，`CHANGELOG.md` 回答“这一版给玩家带来什么”，版本标签和 Releases 保存可下载的正式版。

## 日常记录

在项目目录修改并验证后：

```powershell
git status
git add .
git commit -m "feat: 增加新的秘境事件"
git push
```

每次提交尽量只表达一个目的，例如 `feat:` 新玩法、`fix:` 修复、`docs:` 文档。`saves/`、安装包、编译输出、临时测试目录与安卓签名密钥已忽略；不要用 `git add -f` 强行加入它们。

## 发布一个正式版

1. 更新 Windows 的 `src/assets.rc`、Android Manifest 与 `CMakeLists.txt` 中的版本；Android 的 `versionCode` 必须递增。
2. 在 `CHANGELOG.md` 写新增、修复及已知限制；运行 `build.ps1 -Test`、`package.ps1 -Test`，重新构建并验证 APK。
3. 将这版源码提交并推送，再创建标签，例如：

```powershell
git tag -a v3.1.1 -m "v3.1.1: 修复秘境事件"
git push origin v3.1.1
```

4. 在 GitHub 的 **Releases → Draft a new release** 选择该标签，粘贴简短更新说明，上传下列文件，最后发布。

| 文件名保持固定 | 来源 |
|---|---|
| `SwordForge-Windows.exe` | `outputs/SwordForge.exe` |
| `SwordForge-Android.apk` | `outputs/剑炉物语-安卓版.apk` |
| `SwordForge-Source.zip` | 对正式版本执行 `git archive --format=zip --prefix=sword-forge/ -o outputs/SwordForge-Source.zip v3.1.1` |
| `SHA256SUMS.txt` | 用 `Get-FileHash -Algorithm SHA256` 记录三个文件的校验值 |

这些固定文件名让主页的“最新版下载”链接自动指向下一版。旧版留在 Releases；不要移动已发布的标签。v2.0.0 的归档没有 Android 安装包，因此该版本只发布 Windows 与源码。

安卓覆盖更新需要本机 `android/.private/` 中的原签名密钥。它不上传 GitHub，应另外保存在你自己的安全备份中；其他人从源码构建会生成自己的签名，不能直接覆盖你发布的 APK。

## 发布前检查

每次发布先执行核心、打包接口及对应平台的检查。Windows 图形界面和 Android 的完整检查入口在 `tests/`，需要对应平台和环境。

`tools/core-checks.yml` 提供可选的 GitHub Actions 模板，放入 `.github/workflows/` 后可自动编译 C 核心并测试；本次发布未启用工作流，现有登录凭据只有仓库权限。核心工作流也不能替代界面与手机验收。
