# 提交、PR 与发布规则

## Issues

欢迎通过 [GitHub Issues](https://github.com/QinChen1998/Cxwl-Depth-Camera-SDK/issues) 反馈 SDK 使用问题、文档错误或功能建议。下面的格式供参考，按实际情况填写即可。

- **标题** 用一句话概括问题，例如「[Linux SDK] 3.1.0 包内 USB 接入说明链接失效」。
- **使用环境** 可注明 SDK 版本、操作系统和架构。
- **问题描述** 简述操作步骤、实际结果和预期结果。文档问题可附页面链接或文件路径，功能建议可说明使用场景和希望实现的效果。
- **补充材料** 如有相关报错、截图或脱敏日志，可一并附上。

## 提交前检查

- 每次提交使用 `类型: 中文说明`。类型可用 `feat`、`fix`、`doc`、`refactor`、`test`、`build`、`ci`、`chore`、`perf`、`style`、`revert`。合并时生成的 squash 提交也使用此格式。
- 源码、配置、文档和测试禁止存放明文密码、访问令牌、私钥及其他凭证。发现凭证时拒绝提交和推送。测试需要认证时，通过运行环境注入或临时生成所需材料。
- 相机登记档案、主机授权、实测原始数据和板端私有材料留在本地。公开分发范围见 [构建与分发](docs/SOURCE_DISTRIBUTION.md)。

本地需要 Python 3 和 [Gitleaks](https://github.com/gitleaks/gitleaks/releases)。从项目根目录启用随仓库提供的 Git hooks。Gitleaks 可从 `PATH` 运行，也可通过仓库级 Git 配置 `cxwl.gitleaks` 指定可执行文件。

```powershell
git config --local core.hooksPath .githooks
```

提交前检查暂存文件和凭证，提交时检查标题。GitHub 推送保护会另外拦截它能识别的凭证。发现疑似凭证时，先核实并清理，再重新运行检查。

## Pull Request

- 每个 PR 使用具体中文标题，在正文说明改了什么以及如何验证。`PR1`、`PR2` 等占位标题不合格。
- `main` 必须通过 PR 合并。贡献检查通过后，PR 需要仓库所有者或任意一位现任管理员批准。提交发生变化后需重新取得审批。其他分支合并无此审批要求。
- `main` 的审批人维护在 `.github/CODEOWNERS`。管理员名单变化时，同时更新该文件和 GitHub 的 `Releases` 环境审批人。贡献者可通过 fork 提交 PR。

## GitHub Releases

先按 [Windows SDK 本地交付](docs/SOURCE_DISTRIBUTION.md#windows-sdk-本地交付) 验收 ZIP，再创建草稿，并附上 ZIP 和同名 `.sha256` 文件。草稿的目标提交必须是用于制作包的 `main` 提交。通过 **发布 Windows SDK** 工作流提交版本标签和 ZIP 的 SHA-256。

Linux 的 x64 和 ARM64 按 [Linux SDK 本地交付](docs/SOURCE_DISTRIBUTION.md#linux-sdk-本地交付) 各自完成离线、RV1103 实机和成品校验后，再创建同一 SDK 版本的 Linux 草稿。Linux 标签使用 `v<版本号>-linux`，例如 `v3.0.0-linux`。草稿指向用于制包的 `main` 提交，并附上两个 `.tar.gz` 及各自的 `.sha256`。通过 **发布 Linux SDK** 工作流指定该提交和两个归档的 SHA-256。

发布工作流只能从 `main` 运行。`Releases` 环境要求仓库所有者或任意一位现任管理员审批，审批后复核草稿、提交和资产摘要，再发布到 GitHub。GitHub Releases 同时允许有写权限的账号直接操作，因此普通贡献者使用 fork，仓库写权限仅授予所有者和管理员。仓库发布和更正均走审批流程。
