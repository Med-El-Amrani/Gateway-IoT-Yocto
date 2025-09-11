import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
YOCTO = REPO_ROOT / "yocto"
SCRIPT = YOCTO / "scripts" / "validate_cfg.py"
YAML = YOCTO / "meta-iotgw" / "recipes-iotgw" / "iotgwd" / "files" / "config.example.yaml"
SCHEMA = YOCTO / "meta-iotgw" / "recipes-iotgw" / "iotgwd" / "files" / "iotgw.schema.json"

def run(cmd):
    try:
        return subprocess.run(cmd, cwd=REPO_ROOT, check=False, capture_output=True, text=True)
    except FileNotFoundError as e:
        # If python isn't found (or path wrong), mimic a failing result
        class R: pass
        r = R(); r.returncode = 127; r.stdout = ""; r.stderr = str(e)
        return r

def test_validate_example_config():
    assert SCRIPT.exists(), f"Missing validator at {SCRIPT}"
    assert YAML.exists(), f"Missing example config at {YAML}"
    assert SCHEMA.exists(), f"Missing schema at {SCHEMA}"

    candidates = [
        ["python", str(SCRIPT), str(YAML), str(SCHEMA)],
        ["python", str(SCRIPT), "--schema", str(SCHEMA), str(YAML)],
        ["python", str(SCRIPT), str(YAML)],
    ]

    errors = []
    for cmd in candidates:
        r = run(cmd)
        if r.returncode == 0:
            print(f"Validation passed with: {' '.join(cmd)}")
            print(r.stdout)
            return
        else:
            errors.append(
                f"\n--- Command: {' '.join(cmd)}\n"
                f"Exit: {r.returncode}\n"
                f"STDOUT:\n{r.stdout}\nSTDERR:\n{r.stderr}\n"
            )

    raise AssertionError(
        "All validator invocation patterns failed. Details:\n" + "\n".join(errors)
    )
