"""Check proposed commits and PRs without printing credential values."""

import argparse
import json
import re
import shutil
import subprocess
from pathlib import Path


COMMIT_TITLE = re.compile(r"(?:feat|fix|doc|refactor|test|build|ci|chore|perf|style|revert): \S.*")
CHINESE = re.compile(r"[\u3400-\u9fff]")
PLACEHOLDER = re.compile(r"(?:PR[\s#_-]*\d*|TODO|TBD|WIP)", re.IGNORECASE)
PRIVATE_DIRECTORIES = {".vs", "build", "config", "output", "private_build", "release", "x64"}
PRIVATE_SUFFIXES = (".dpapi", ".key", ".p12", ".pem", ".pfx", ".private.json")


def git(*args):
    return subprocess.check_output(["git", *args], encoding="utf-8", errors="surrogateescape")


def check_titles(subjects):
    if any(not COMMIT_TITLE.fullmatch(subject) or not CHINESE.search(subject) for subject in subjects):
        raise SystemExit("Commit titles must start with a supported type and contain a Chinese summary.")


def check_paths(paths):
    rejected = []
    for original in filter(None, paths.split("\0")):
        path = original.lower()
        relative = Path(path)
        if path == "config/first-run-settings.ini":
            continue
        if (relative.parts[0] in PRIVATE_DIRECTORIES or path == "qtdevelopment.local.props"
                or relative.name == ".env" or relative.name.startswith(".env.")
                or path.endswith(PRIVATE_SUFFIXES)):
            rejected.append(original)
    if rejected:
        raise SystemExit("Local-only files cannot be committed:\n" + "\n".join(rejected))


def scan(*options):
    configured = subprocess.run(["git", "config", "--get", "cxwl.gitleaks"],
                                capture_output=True, text=True, check=False).stdout.strip()
    executable = configured or shutil.which("gitleaks")
    if not executable:
        raise SystemExit("Install Gitleaks or set the local cxwl.gitleaks Git config before committing.")
    return subprocess.call([executable, "git", "--redact=100", "--no-banner", "--ignore-gitleaks-allow", *options])


def check_pr(pr):
    title = pr["title"].strip()
    body = re.sub(r"<!--.*?-->", "", pr.get("body") or "", flags=re.DOTALL)
    summary = [line.strip(" -#*`\t") for line in body.splitlines() if not line.lstrip().startswith("#")]
    if (not CHINESE.search(title) or PLACEHOLDER.fullmatch(title)
            or not any(line and not PLACEHOLDER.fullmatch(line) for line in summary)):
        raise SystemExit("Use a descriptive Chinese PR title and explain the changes in the PR body.")


def check_event(event):
    pr = event.get("pull_request")
    if pr:
        check_pr(pr)
        before, after = pr["base"]["sha"], pr["head"]["sha"]
    else:
        before, after = event["before"], event["after"]

    if before == "0" * 40:
        check_paths(git("ls-tree", "-r", "--name-only", "-z", after))
        commits = after
    else:
        comparison = f"{before}...{after}" if pr else f"{before}..{after}"
        check_paths(git("diff", "--name-only", "--diff-filter=ACMR", "-z", comparison))
        commits = f"{before}..{after}"
    check_titles(git("log", "--format=%s", commits).splitlines())
    return scan(f"--log-opts={commits}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    checks = parser.add_mutually_exclusive_group(required=True)
    checks.add_argument("--staged", action="store_true")
    checks.add_argument("--commit-msg", type=Path)
    checks.add_argument("--github-event", type=Path)
    args = parser.parse_args()

    if args.staged:
        check_paths(git("diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z"))
        return scan("--staged")
    if args.commit_msg:
        check_titles(args.commit_msg.read_text(encoding="utf-8-sig").splitlines()[:1] or [""])
        return 0
    return check_event(json.loads(args.github_event.read_text(encoding="utf-8")))


if __name__ == "__main__":
    raise SystemExit(main())
