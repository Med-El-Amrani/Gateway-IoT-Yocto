# tests/test_example_config.py
import os
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
FILES_DIR = REPO_ROOT / "meta-iotgw" / "recipes-iotgw" / "iotgwd" / "files"
SCHEMA = FILES_DIR / "iotgw.schema.json"
VALIDATOR = REPO_ROOT / "scripts" / "validate_cfg.py"

def _run(cmd):
    return subprocess.run(
        cmd,
        cwd=REPO_ROOT,
        text=True,
        capture_output=True,
        env={**os.environ, "PYTHONPATH": str(REPO_ROOT)},
        check=False,
    )

def test_validator_exists_and_schema_present():
    assert VALIDATOR.exists(), f"Missing validator: {VALIDATOR}"
    assert SCHEMA.exists(), f"Missing schema: {SCHEMA}"

def test_config_example_validates():
    cfg = FILES_DIR / "config.example.yaml"
    assert cfg.exists(), f"Missing example config: {cfg}"
    r = _run(["python", str(VALIDATOR), str(cfg), "--schema", str(SCHEMA)])
    if r.returncode != 0:
        raise AssertionError(
            f"Validation failed for config.example.yaml\n"
            f"STDOUT:\n{r.stdout}\nSTDERR:\n{r.stderr}"
        )

def test_protocol_fragments_validate_individually():
    protos = {
        "mqtt":       FILES_DIR / "protocols" / "mqtt.yaml",
        "modbus-rtu": FILES_DIR / "protocols" / "modbus_rtu.yaml",
        "modbus-tcp": FILES_DIR / "protocols" / "modbus_tcp.yaml",
        "socketcan":  FILES_DIR / "protocols" / "socketcan.yaml",
        "opcua":      FILES_DIR / "protocols" / "opcua.yaml",
        "http-server":FILES_DIR / "protocols" / "http_server.yaml",
        "coap":       FILES_DIR / "protocols" / "coap.yaml",
        "ble":        FILES_DIR / "protocols" / "ble.yaml",
        "lorawan":    FILES_DIR / "protocols" / "lorawan.yaml",
        "i2c":        FILES_DIR / "protocols" / "i2c.yaml",
        "spi":        FILES_DIR / "protocols" / "spi.yaml",
        "uart":       FILES_DIR / "protocols" / "uart.yaml",
        "onewire":    FILES_DIR / "protocols" / "onewire.yaml",
        "zigbee":     FILES_DIR / "protocols" / "zigbee.yaml",
    }

    missing = [p for p in protos.values() if not p.exists()]
    assert not missing, f"Missing protocol templates: {missing}"

    for proto_type, path in protos.items():
        r = _run([
            "python", str(VALIDATOR), str(path),
            "--schema", str(SCHEMA),
            "--fragment", proto_type
        ])
        if r.returncode != 0:
            raise AssertionError(
                f"[{proto_type}] fragment validation failed for {path}\n"
                f"STDOUT:\n{r.stdout}\nSTDERR:\n{r.stderr}"
            )
