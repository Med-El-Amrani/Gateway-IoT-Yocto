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

