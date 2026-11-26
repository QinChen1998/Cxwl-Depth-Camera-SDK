# 提交、PR 与发布规则

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

发布工作流只能从 `main` 运行。`Releases` 环境要求仓库所有者或任意一位现任管理员审批，审批后复核草稿、提交和资产摘要，再发布到 GitHub。GitHub Releases 同时允许有写权限的账号直接操作，因此普通贡献者使用 fork，仓库写权限仅授予所有者和管理员。仓库发布和更正均走审批流程。
