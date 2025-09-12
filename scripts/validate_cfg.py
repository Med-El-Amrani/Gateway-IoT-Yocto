#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
IoT Gateway config validator

Features:
- Loads global schema (with $ref to files/schemas/*.schema.json)
- Optional --schema to override schema path
- Merges YAML 'includes:' before validation
- Optional --fragment TYPE to validate a per-protocol fragment alone
- Pretty error reporting with JSON Pointer-ish paths
- Post-validation cross checks: bridges.from/to ↔ connectors keys
"""

import sys
import json
import yaml
import argparse
import pathlib
from typing import Dict, Any, Tuple, List

from jsonschema import Draft202012Validator, RefResolver




REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_SCHEMA = (
    REPO_ROOT
    / "meta-iotgw"
    / "recipes-iotgw"
    / "iotgwd"
    / "files"
    / "iotgw.schema.json"
)


def build_ref_store(schema_path: pathlib.Path) -> dict:
    """
    Preload a local store for jsonschema so that any $ref/$id resolves to local files.
    It also maps 'https://example/schemas/<name>.schema.json' to the local file.
    """
    files_dir = schema_path.parent  # .../files
    schemas_dir = files_dir / "schemas"
    store = {}

    # Map the root schema by multiple keys (file:// URI and filename)
    root_schema = load_json(schema_path)
    store[schema_path.as_uri()] = root_schema
    store["iotgw.schema.json"] = root_schema  # in case an id is relative later

    # Map all sub-schemas
    if schemas_dir.is_dir():
        for p in schemas_dir.glob("*.schema.json"):
            doc = load_json(p)
            uri = p.as_uri()
            store[uri] = doc
            store[p.name] = doc  # e.g., 'mqtt.schema.json'
            # also map the old absolute example ids if present:
            store[f"https://example/schemas/{p.name}"] = doc

    return store

def load_yaml(path: pathlib.Path) -> Any:
    try:
        return yaml.safe_load(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        sys.exit(f"ERR: YAML not found: {path}")
    except Exception as e:
        sys.exit(f"ERR: Cannot parse YAML {path}: {e}")


def load_json(path: pathlib.Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        sys.exit(f"ERR: JSON schema not found: {path}")
    except Exception as e:
        sys.exit(f"ERR: Cannot parse JSON {path}: {e}")


def pretty_path(err) -> str:
    # Convert jsonschema error path deque → a/b/c string
    try:
        return "/" + "/".join(str(p) for p in err.path)
    except Exception:
        return "<root>"


def merge_config(base: Dict[str, Any], overlay: Dict[str, Any], src: pathlib.Path) -> None:
    """
    Merge overlay into base.
    - For 'connectors' and 'bridges' (dicts keyed by id), we forbid duplicate keys.
    - For other top-level keys, shallow-merge (dict update) and lists overwrite.
    """
    for k, v in (overlay or {}).items():
        if k in ("connectors", "bridges"):
            if v is None:
                continue
            if base.get(k) is None:
                base[k] = {}
            if not isinstance(base[k], dict) or not isinstance(v, dict):
                sys.exit(f"ERR: '{k}' must be an object in {src}")
            dup = set(base[k].keys()).intersection(v.keys())
            if dup:
                sys.exit(f"ERR: duplicate {k} ids {sorted(dup)} found while merging {src}")
            base[k].update(v)
        elif k == "includes":
            # We'll process includes separately; ignore here if present in overlay
            continue
        else:
            base[k] = v


def load_with_includes(cfg_path: pathlib.Path) -> Dict[str, Any]:
    """
    Load a master config and merge any 'includes:' fragments (paths may be relative to cfg dir).
    Fragments may contain connectors/bridges (or gateway overrides if you allow).
    """
    cfg_dir = cfg_path.parent
    cfg = load_yaml(cfg_path) or {}

    includes = cfg.get("includes", [])
    if includes:
        if not isinstance(includes, list):
            sys.exit("ERR: 'includes' must be an array of file paths")
        # Start with a copy that does NOT include 'includes' to avoid re-validation of that field
        merged = {k: v for k, v in cfg.items() if k != "includes"}
        for inc in includes:
            inc_path = (cfg_dir / inc).resolve() if not pathlib.Path(inc).is_absolute() else pathlib.Path(inc)
            frag = load_yaml(inc_path)
            if not isinstance(frag, dict):
                sys.exit(f"ERR: include '{inc}' did not contain a YAML object")
            merge_config(merged, frag, inc_path)
        return merged

    return cfg


def build_validator(schema_path: pathlib.Path) -> Draft202012Validator:
    schema = load_json(schema_path)
    base_uri = schema_path.parent.as_uri() + "/"
    store = build_ref_store(schema_path)
    resolver = RefResolver(base_uri=base_uri, referrer=schema, store=store)
    return Draft202012Validator(schema, resolver=resolver)


def validate_global(cfg: Dict[str, Any], validator: Draft202012Validator) -> List[str]:
    errors = sorted(validator.iter_errors(cfg), key=lambda e: (list(e.path), e.message))
    msgs = []
    for e in errors:
        msgs.append(f"[schema] {pretty_path(e)}: {e.message}")
    return msgs


def cross_checks(cfg: Dict[str, Any]) -> List[str]:
    """
    Additional checks that JSON Schema cannot/should not express:
    - bridges.from / bridges.to must reference existing connectors keys
    """
    msgs: List[str] = []
    connectors = cfg.get("connectors", {}) or {}
    bridges = cfg.get("bridges", {}) or {}

    if not isinstance(connectors, dict):
        msgs.append("connectors must be an object (map of id → connector)")
        return msgs
    if not isinstance(bridges, dict):
        msgs.append("bridges must be an object (map of id → bridge)")
        return msgs

    known_ids = set(connectors.keys())
    for bid, b in bridges.items():
        if not isinstance(b, dict):
            msgs.append(f"[bridges/{bid}] each bridge must be an object")
            continue
        src = b.get("from")
        dst = b.get("to")
        if src not in known_ids:
            msgs.append(f"[bridges/{bid}/from] unknown connector id '{src}'")
        if dst not in known_ids:
            msgs.append(f"[bridges/{bid}/to] unknown connector id '{dst}'")

    return msgs


def validate_fragment(fragment_path: pathlib.Path, schema_path: pathlib.Path, proto_type: str) -> List[str]:
    """
    Validate a single fragment YAML (e.g. protocols/mqtt.yaml) as if it were a single connector of given type.
    We wrap it minimally to reuse the global schema's per-protocol rules.
    """
    frag = load_yaml(fragment_path)
    if frag is None:
        frag = {}
    if "connectors" in frag:
        # If user provided a connectors{} object directly, validate as part of a minimal doc
        doc = {"gateway": {"name": "frag", "loglevel": "info"}, "connectors": frag.get("connectors"), "bridges": {"_placeholder": {"from": "x", "to": "x"}}}
    else:
        # Wrap the fragment as a single connector with the specified type
        doc = {
            "gateway": {"name": "frag", "loglevel": "info"},
            "connectors": {"_frag": {"type": proto_type, "params": frag.get("params", frag)}},
            "bridges": {"_placeholder": {"from": "_frag", "to": "_frag"}}
        }

    validator = build_validator(schema_path)
    msgs = validate_global(doc, validator)
    # No cross-checks needed beyond structure for fragments
    return msgs


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description="Validate IoT Gateway configuration")
    ap.add_argument("config", help="Path to iotgw.yaml (or fragment with --fragment)")
    ap.add_argument("--schema", help="Path to iotgw.schema.json (default: autodetect)", default=None)
    ap.add_argument("--fragment", help="Validate a fragment as given protocol type (e.g. mqtt, modbus-rtu, i2c, ...)", default=None)
    args = ap.parse_args(argv)

    cfg_path = pathlib.Path(args.config).resolve()
    schema_path = pathlib.Path(args.schema).resolve() if args.schema else DEFAULT_SCHEMA

    if args.fragment:
        errs = validate_fragment(cfg_path, schema_path, args.fragment)
        if errs:
            print("\n".join(errs))
            return 1
        print(f"✓ Fragment valid ({args.fragment}): {cfg_path}")
        return 0

    # Full-document mode: load + merge includes + validate + cross-checks
    cfg = load_with_includes(cfg_path)
    validator = build_validator(schema_path)

    errs = validate_global(cfg, validator)
    errs += cross_checks(cfg)

    if errs:
        print("Validation FAILED:")
        print("\n".join(errs))
        return 1

    print(f"✓ Fichier valide : {cfg_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
