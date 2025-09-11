# tests/test_config_validation.py
import os
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

def find_one(glob):
    matches = list(REPO_ROOT.rglob(glob))
    assert matches, f"Cannot find any file matching '{glob}' under {REPO_ROOT}"
    # Prefer shortest path (closest to root) if multiple
    matches.sort(key=lambda p: len(p.as_posix()))
    return matches[0]

def test_validate_example_config():
    # Try common names/locations first, but fall back to rglob
    preferred_script_paths = [
        REPO_ROOT / "yocto" / "scripts" / "validate_cfg.py",
        REPO_ROOT / "scripts" / "validate_cfg.py",
    ]
    script = next((p for p in preferred_script_paths if p.exists()), None)
    if script is None:
        script = find_one("validate_cfg.py")

    preferred_yaml_paths = [
        REPO_ROOT / "yocto" / "meta-iotgw" / "recipes-iotgw" / "iotgwd" / "files" / "config.example.yaml",
    ]
    yaml_file = next((p for p in preferred_yaml_paths if p.exists()), None)
    if yaml_file is None:
        yaml_file = find_one("config.example.yaml")

    preferred_schema_paths = [
        REPO_ROOT / "yocto" / "meta-iotgw" / "recipes-iotgw" / "iotgwd" / "files" / "iotgw.schema.json",
    ]
    schema_file = next((p for p in preferred_schema_paths if p.exists()), None)
    if schema_file is None:
        schema_file = find_one("iotgw.schema.json")

    # Sanity prints to help debugging on CI
    print(f"Using validator: {script}")
    print(f"Using YAML:      {yaml_file}")
    print(f"Using schema:    {schema_file}")

    assert script.exists(), f"Missing validator at {script}"
    assert yaml_file.exists(), f"Missing example config at {yaml_file}"
    assert schema_file.exists(), f"Missing schema at {schema_file}"

    def run(cmd):
        return subprocess.run(
            cmd,
            cwd=REPO_ROOT,
            check=False,
            capture_output=True,
            text=True,
            env={**os.environ, "PYTHONPATH": str(REPO_ROOT)},
        )

    candidates = [
        ["python", str(script), str(yaml_file), str(schema_file)],
        ["python", str(script), "--schema", str(schema_file), str(yaml_file)],
        ["python", str(script), str(yaml_file)],
    ]

    errors = []
    for cmd in candidates:
        r = run(cmd)
        print(f"\n> Trying: {' '.join(cmd)}")
        print("STDOUT:\n", r.stdout)
        print("STDERR:\n", r.stderr)
        if r.returncode == 0:
            print("Validation PASSED with that invocation.")
            return
        errors.append(
            f"\n--- Command: {' '.join(cmd)}\nExit: {r.returncode}\nSTDOUT:\n{r.stdout}\nSTDERR:\n{r.stderr}\n"
        )

    raise AssertionError("All validator invocations failed:\n" + "\n".join(errors))
