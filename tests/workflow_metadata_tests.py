"""Execute the real workflow metadata block with representative GitHub event inputs."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
workflow = (ROOT / ".github/workflows/native.yml").read_text(encoding="utf-8")
part = workflow.split("- name: Validate immutable metadata", 1)[1].split("        run: |\n", 1)[1]
lines = []
for line in part.splitlines():
    if line and not line.startswith("          "):
        break
    lines.append(line[10:] if line else "")
script = "\n".join(lines) + "\nif ($LASTEXITCODE) { exit $LASTEXITCODE }\n"
sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
base = re.search(r"project\(VelocityNetTools VERSION (\d+\.\d+\.\d+)", (ROOT/"CMakeLists.txt").read_text()).group(1)
cases = [
    ("refs/heads/main", f"{base}-dev.17+{sha[:7]}", "preview"),
    ("refs/pull/23/merge", f"{base}-ci.17+{sha[:7]}", "ci"),
    ("refs/tags/v2.3.4", "2.3.4", "stable"),
    ("refs/tags/v2.3.4-rc.2", "2.3.4-rc.2", "rc"),
]
with tempfile.TemporaryDirectory(prefix="veu-workflow-test-") as tmp:
    source = Path(tmp) / "metadata.ps1"
    source.write_text(script, encoding="utf-8")
    for ref, version, channel in cases:
        output = Path(tmp) / "output.txt"
        output.unlink(missing_ok=True)
        env = dict(os.environ, GITHUB_SHA=sha, GITHUB_RUN_NUMBER="17", GITHUB_REF=ref,
                   GITHUB_REPOSITORY="velocityeu/NetTools", GITHUB_OUTPUT=str(output))
        result = subprocess.run(["pwsh", "-NoProfile", "-File", str(source)], cwd=ROOT,
                                env=env, capture_output=True, text=True)
        if result.returncode:
            raise SystemExit(result.stdout + result.stderr)
        values = dict(line.split("=", 1) for line in output.read_text(encoding="utf-8-sig").splitlines())
        assert values["version"] == version, values
        assert values["channel"] == channel, values
        assert values["artifact"] == f"VelocityNetTools-{version}-windows-x64", values
print("Workflow metadata: 4 real event cases passed.")

# Execute the actual Pages job expressions against representative event contexts.
def pages_job_enabled(name, repository, event, ref):
    import ast
    pages = (ROOT / ".github/workflows/pages.yml").read_text(encoding="utf-8")
    match = re.search(r"^  " + name + r":\n    if: ([^\n]+)", pages, re.MULTILINE)
    if not match:
        return False
    expression = match.group(1).replace("&&", " and ").replace("||", " or ")
    for key, value in (("repository", repository), ("event_name", event), ("ref", ref)):
        expression = expression.replace("github." + key, repr(value))
    tree = ast.parse(expression, mode="eval")
    allowed = (ast.Expression, ast.BoolOp, ast.Compare, ast.Constant, ast.And, ast.Or, ast.Eq, ast.NotEq)
    assert all(isinstance(node, allowed) for node in ast.walk(tree)), expression
    return bool(eval(compile(tree, "<Pages job condition>", "eval"), {"__builtins__": {}}))

for repository, event, ref, expected in [
    ("velocityeu/NetTools", "release", "refs/tags/v0.1.0-dev.3+d9cc576", (True, False)),
    ("velocityeu/NetTools", "release", "refs/heads/main", (True, False)),
    ("velocityeu/NetTools", "push", "refs/heads/main", (False, True)),
    ("velocityeu/NetTools", "workflow_dispatch", "refs/heads/main", (False, True)),
    ("velocityeu/NetTools", "workflow_dispatch", "refs/tags/v1.0.0", (False, False)),
    ("velocityeu/NetTools", "push", "refs/heads/feature", (False, False)),
    ("velocityeu/NetTools", "pull_request", "refs/pull/23/merge", (False, False)),
    ("example/NetTools", "release", "refs/tags/v1.0.0", (False, False)),
    ("example/NetTools", "push", "refs/heads/main", (False, False)),
]:
    actual = tuple(pages_job_enabled(job, repository, event, ref) for job in ("release_refresh", "build"))
    assert actual == expected, (repository, event, ref, actual, expected)
print("Pages event routing: 9 actual condition cases passed.")
