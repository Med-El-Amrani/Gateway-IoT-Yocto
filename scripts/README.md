# 📜 Scripts – validation et outils annexes

Ce dossier héberge les utilitaires du projet ; le plus important pour
l’utilisateur final est **`validate_cfg.py`**.



## 1. validate_cfg.py : valider un fichier `iotgw.yaml`

```bash
# dépendances (déjà présentes dans la nativesdk Yocto) :
pip3 install pyyaml jsonschema

# usage :
python3 scripts/validate_cfg.py <chemin/vers/iotgw.yaml>

Le script :

    charge le fichier YAML ;

    charge le schéma JSON
    (meta-iotgw/…/iotgw.schema.json) ;

    vérifie la conformité et affiche :

    ✓ Fichier valide : ./iotgw.yaml en cas de succès ;

    un traceback explicite sinon, pointant la partie fautive.
```

## 2. Structure minimale du fichier iotgw.yaml

version: 1                # n° de version du format

gateway:
  name:      "gw01"
  timezone:  "UTC"
  loglevel:  "info"

connectors:               # 1.  déclaration des protocoles
  mqtt_cloud:
    type: mqtt
    url:  "tcp://broker.example.com:1883"
    # … autres champs propres au protocole …

bridges:                  # 2.  flux de traduction
  sensor_to_cloud:        # <clé unique>
    from: modbus_rtu_sensor
    to:   mqtt_cloud
    mode: poll
    period: "2s"

Champ	Type	Obl.	Commentaire
version	int	✔	Pour gérer les évolutions de format.
gateway	object	✔	Métadonnées (nom, TZ, niveau de log…).
connectors	object	✔	Clé = identifiant ; valeur = config protocole.
bridges	object	✔	Décrit un flux source → destination.
## 3. Spécificité : le champ mapping

Le schéma accepte une seule des deux représentations :
A. Représentation liste (schéma d’origine)

bridges:
  sensor_to_cloud:
    …
    mapping:
      - [ temp_c , factory/temperature ]
      - [ pressure, factory/pressure ]

B. Représentation objet (clé → valeur)

bridges:
  sensor_to_cloud:
    …
    mapping:
      temp_c:   factory/temperature
      pressure: factory/pressure

    ⚠️ Si vous choisissez l’objet, modifiez le schéma :

--- a/iotgw.schema.json
@@
-      "mapping": { "type": "array", … }
+      "mapping": {
+        "type": "object",
+        "additionalProperties": { "type": "string" },
+        "minProperties": 1
+      }

## 4. Erreurs courantes
Message du validateur	Cause probable	Correctif
is not of type 'object' sur bridges	bridges est une liste [- -]	Utiliser une map bridge_id: {…}
Additional properties ('logfile' was unexpected)	Champ non défini dans le schéma	Retirer le champ ou étendre le schéma
is not of type 'array' sur mapping	Objet utilisé, schéma attend array	Passer au format liste ou éditer schéma
## 5. Bonnes pratiques

    Versionner un exemple : config.example.yaml.

    Incrémenter version: dès que le schéma change.

    Intégrer validate_cfg.py dans la CI (GitHub Actions).

Happy hacking !


Ajoute-le au dépôt :

```bash
git add scripts/README.md
git commit -m "docs(scripts): notice de validation du fichier iotgw.yaml"
git push
```
