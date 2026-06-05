# tests/test_example_config.py
import os
import subprocess
import yaml
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
    r = _run(["python3", str(VALIDATOR), str(cfg), "--schema", str(SCHEMA)])
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
            "python3", str(VALIDATOR), str(path),
            "--schema", str(SCHEMA),
            "--fragment", proto_type
        ])
        if r.returncode != 0:
            raise AssertionError(
                f"[{proto_type}] fragment validation failed for {path}\n"
                f"STDOUT:\n{r.stdout}\nSTDERR:\n{r.stderr}"
            )


def test_mqtt_accepts_url_or_host(tmp_path):
    for params in (
        {"url": "mqtts://broker.example:8883", "client_id": "gateway-test"},
        {"host": "127.0.0.1", "port": 1883},
    ):
        cfg = {
            "version": 1,
            "gateway": {"name": "test", "loglevel": "info"},
            "connectors": {
                "source": {"type": "spi", "params": {
                    "device": "/dev/spidev0.0",
                    "transactions": [{"op": "read", "len": 1}],
                }},
                "sink": {"type": "mqtt", "params": params},
            },
            "bridges": {"route": {"from": "source", "to": "sink"}},
        }
        path = tmp_path / ("url.yaml" if "url" in params else "host.yaml")
        path.write_text(yaml.safe_dump(cfg), encoding="utf-8")
        result = _run(["python3", str(VALIDATOR), str(path), "--schema", str(SCHEMA)])
        assert result.returncode == 0, result.stdout + result.stderr


def test_wifi_template_contains_no_committed_credentials():
    template = REPO_ROOT / "meta-iotgw/recipes-core/iotgw/iotgw-wifi-ssh/wpa_supplicant-wlan0.conf"
    content = template.read_text(encoding="utf-8")
    assert 'ssid="@WIFI_SSID@"' in content
    assert 'psk="@WIFI_PSK@"' in content


def test_message_queue_c_unit_tests(tmp_path):
    src = REPO_ROOT / "meta-iotgw/recipes-iotgw/iotgwd/iotgwd/src"
    binary = tmp_path / "test_msg_queue"
    build = _run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread",
        "-I", str(src), str(src / "msg_queue.c"),
        str(REPO_ROOT / "tests/c/test_msg_queue.c"), "-o", str(binary),
    ])
    assert build.returncode == 0, build.stdout + build.stderr
    result = _run([str(binary)])
    assert result.returncode == 0, result.stdout + result.stderr


def test_metrics_c_unit_tests(tmp_path):
    src = REPO_ROOT / "meta-iotgw/recipes-iotgw/iotgwd/iotgwd/src"
    binary = tmp_path / "test_metrics"
    build = _run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread",
        "-I", str(src), str(src / "metrics.c"),
        str(REPO_ROOT / "tests/c/test_metrics.c"), "-o", str(binary),
    ])
    assert build.returncode == 0, build.stdout + build.stderr
    result = _run([str(binary)])
    assert result.returncode == 0, result.stdout + result.stderr


def test_uart_runtime_with_pseudo_terminal(tmp_path):
    src = REPO_ROOT / "meta-iotgw/recipes-iotgw/iotgwd/iotgwd/src"
    binary = tmp_path / "test_uart_runtime"
    build = _run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread",
        "-I", str(src), str(src / "conn_uart.c"),
        str(REPO_ROOT / "tests/c/test_uart_runtime.c"),
        "-lutil", "-o", str(binary),
    ])
    assert build.returncode == 0, build.stdout + build.stderr
    result = _run([str(binary)])
    assert result.returncode == 0, result.stdout + result.stderr


def test_modbus_codec_c_unit_tests(tmp_path):
    src = REPO_ROOT / "meta-iotgw/recipes-iotgw/iotgwd/iotgwd/src"
    binary = tmp_path / "test_modbus_codec"
    build = _run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(src), str(src / "modbus_codec.c"),
        str(REPO_ROOT / "tests/c/test_modbus_codec.c"),
        "-lm", "-o", str(binary),
    ])
    assert build.returncode == 0, build.stdout + build.stderr
    result = _run([str(binary)])
    assert result.returncode == 0, result.stdout + result.stderr


def test_modbus_runtime_with_fake_transport(tmp_path):
    src = REPO_ROOT / "meta-iotgw/recipes-iotgw/iotgwd/iotgwd/src"
    c_tests = REPO_ROOT / "tests/c"
    binary = tmp_path / "test_modbus_runtime"
    build = _run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread",
        "-I", str(c_tests / "stubs"), "-I", str(src),
        str(src / "conn_modbus.c"), str(src / "modbus_codec.c"),
        str(c_tests / "test_modbus_runtime.c"), "-lm", "-o", str(binary),
    ])
    assert build.returncode == 0, build.stdout + build.stderr
    result = _run([str(binary)])
    assert result.returncode == 0, result.stdout + result.stderr
