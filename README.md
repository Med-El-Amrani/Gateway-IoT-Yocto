# Yocto Industrial IoT Gateway (RPi4-64)

Ce dépôt versionne **meta-iotgw** (couches et recettes spécifiques) + docs/outils.
Les layers lourds (poky, meta-openembedded, meta-raspberrypi) ne sont **pas** suivis.

## Protocoles pris en charge (par couche)

| Couche | Protocole | Cas d’usage clé | Matériel requis | Stack / recette Yocto (scarthgap) |
|--------|-----------|-----------------|-----------------|----------------------------------|
| **1. Bus & IO** | **I²C / SPI / UART / GPIO / PWM** | Sensors, HATs, actionneurs | Natif RPi 4 | drivers kernel + `i2c-tools`, `spidev-test`, `pigpio` |
| | **One-Wire** | Sondes DS18B20, EEPROM HAT | 1 résistance + module `w1-gpio` | Module kernel + `owfs` (meta-oe) |
| | **CAN (SocketCAN)** | Automates, BMS | HAT MCP2515 (SPI) | `can-utils` (meta-oe) + overlay dtbo |
| **2. Fieldbus / Industriel** | **Modbus RTU** | Équipements RS-485 | Dongle USB-RS485 | `libmodbus` + démon `mbusd` |
| | **Modbus TCP** | PLC Ethernet | Aucun | `libmodbus`, `mbusd -t tcp` |
| | **CANopen** | Variateurs moteurs | Même HAT CAN | `canopen-node` ou `canfestival` |
| | **OPC UA** | Agrégation IT/OT | Aucun | `open62541` |
| **3. Réseau & IoT** | **MQTT / MQTTS** | Pub/Sub local & cloud | Aucun | `mosquitto` + `libmosquitto` |
| | **HTTP / REST + WebSocket** | API & UI | Aucun | `libmicrohttpd`, `nginx` (meta-webserver) |
| | **CoAP / LwM2M** | Objets low-power | Aucun | `libcoap` ou `wakaama` (meta-oe) |
| | **BLE (GATT)** | Beacons, tags | BT intégré | `bluez5`, `python-bluezero` |
| | **Zigbee** | Domotique / capteurs | Dongle CC2531/CC2652 | `zigbee2mqtt` (layer externe) |
| | **Thread / Matter** | Smart-building | Dongle nRF52840 | `ot-daemon` (meta-thread) |
| | **LoRa / LoRaWAN** | Capteurs longue portée | HAT RFM95 / concentrateur | `chirpstack-gateway-bridge` (meta-oe) |

## Cloner & préparer
```bash
git clone git@github.com:Med-El-Amrani/Gateway-IoT-Yocto.git yocto-iotgw
cd yocto-iotgw
./scripts/fetch_layers.sh
source poky/oe-init-build-env build-rpi4
bitbake iotgw-image
```
## Configuration rapide


1. Récupérer le YAML courant depuis la passerelle :
```bash
scp root@gateway:/etc/iotgw.yaml ./iotgw.yaml
```

2. Éditer les sections connectors: et bridges:
(exemple détaillé : meta-iotgw/recipes-iotgw/iotgwd/files/config.example.yaml)

3. Recharger le service :
```bash
scp iotgw.yaml root@gateway:/etc/iotgw.yaml
ssh root@gateway \
    "systemctl restart iotgwd && journalctl -u iotgwd -n 20 --no-pager"
```

## Wi-Fi à la compilation

Dans build-rpi4/conf/local.conf :

WIFI_SSID = "MonAP"
WIFI_PSK  = "MonPass"

(Astuce : pour éviter la PSK en clair, utilise la valeur hexadécimale
générée par wpa_passphrase.)


## Configuration d'un protocole de communication à utiliser

```bash
# list templates shipped read-only
ls /usr/share/iotgwd/protocols/

# enable MQTT by adding a fragment
sudo cp /usr/share/iotgwd/protocols/mqtt.yaml /etc/iotgwd/mqtt.yaml
sudo vi /etc/iotgwd/mqtt.yaml

# reload without full restart
sudo systemctl reload iotgwd
journalctl -u iotgwd -n 50 --no-pager
```

## Livraison MQTT hors ligne

Les bridges vers MQTT peuvent utiliser une file bornée en mémoire. Quand le
broker est indisponible, les messages sont conservés puis envoyés dans l'ordre
après reconnexion automatique :

```yaml
bridges:
  sensor_to_cloud:
    from: spi_dev0
    to: mqtt_local
    mapping:
      topic: "iotgw/spi/read"
    buffer:
      size: 256
      policy: drop_oldest  # ou drop_new
```

La file est volatile : son contenu n'est pas conservé après un redémarrage.

## Santé et métriques

Quand `gateway.metrics_port` est défini, `iotgwd` expose :

```bash
curl -i http://gateway:9100/health
curl http://gateway:9100/metrics
```

`/health` renvoie HTTP 200 lorsque toutes les destinations MQTT actives sont
connectées, sinon HTTP 503. `/metrics` expose notamment l'état MQTT, la
profondeur de file, les messages supprimés, les publications acceptées et les
échecs de publication au format Prometheus.

Exemple de cible Prometheus :

```yaml
scrape_configs:
  - job_name: iotgw
    static_configs:
      - targets: ["gateway:9100"]
```

## Bridge UART vers MQTT

Un connecteur UART peut publier des trames binaires vers MQTT. `packet.end`
définit ici le délimiteur `LF` (`0A`), qui n'est pas inclus dans le payload :

```yaml
connectors:
  serial_sensor:
    type: uart
    params:
      port: /dev/ttyUSB0
      baudrate: 115200
      bytesize: 8
      parity: N
      stopbits: 1
      timeout_ms: 500
      packet:
        end: "0A"

  mqtt_local:
    type: mqtt
    params:
      host: 127.0.0.1
      port: 1883

bridges:
  uart_to_mqtt:
    from: serial_sensor
    to: mqtt_local
    mapping:
      topic: "iotgw/uart/raw"
      format: raw
    buffer:
      size: 256
      policy: drop_oldest
```

## Bridges Modbus vers MQTT

Les connecteurs `modbus-rtu` et `modbus-tcp` utilisent les templates installés
dans `/usr/share/iotgwd/protocols/`. Chaque point lu produit un payload JSON :

```json
{"name":"temperature","unit_id":1,"value":21.5}
```

Le bridge se configure comme les sources SPI et UART :

```yaml
bridges:
  modbus_to_mqtt:
    from: modbus_rtu_1  # ou un connecteur modbus-tcp
    to: mqtt_local
    mapping:
      topic: "iotgw/modbus/measurements"
      format: json
    buffer:
      size: 512
      policy: drop_oldest
```

Les registres multi-mots sont actuellement décodés avec le mot de poids fort
en premier. Le poller se reconnecte automatiquement après une erreur de
connexion ou de lecture. Les équipements utilisant un autre ordre de mots
nécessiteront une option de conversion supplémentaire.
