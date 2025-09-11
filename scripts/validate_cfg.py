#!/usr/bin/env python3
import sys, json, yaml, pathlib
from jsonschema import validate, Draft202012Validator

schema = json.loads((pathlib.Path(__file__).parent.parent / "meta-iotgw/recipes-iotgw/iotgwd/files/iotgw.schema.json").read_text())



def main(cfg_path):
    data = yaml.safe_load(open(cfg_path))
    Draft202012Validator(schema).validate(data)
    print("✓ Fichier valide :", cfg_path)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: validate_cfg.py <iotgw.yaml>")
        sys.exit(1)
    main(sys.argv[1])
