# LED/button driver: LED-only stage

Target: Raspberry Pi 4 Model B, Yocto kernel 6.6.63-v8.
The driver uses the Linux 6.6 `.remove_new` callback. No button IRQ or
character device is implemented yet.

Power off before wiring: BCM GPIO17 (header pin 11) -> 330 ohm resistor ->
LED anode; LED cathode -> GND (header pin 6).

## Build on Ubuntu

From the project root:

```bash
source poky/oe-init-build-env build-gateway
bitbake led-button led-button-overlay
```

The module package is named `kernel-module-led-button` (the filename may
include the kernel version). The overlay is deployed as
`tmp/deploy/images/raspberrypi4-64/led-button.dtbo` and also packaged under
`/lib/firmware/overlays`. Installing that overlay package alone does not
activate the firmware overlay.

Locate the build outputs:

```bash
find tmp/work -path '*/led-button/1.0/led_button.ko'
ls tmp/deploy/images/raspberrypi4-64/led-button.dtbo
```

## Build directly into the image

The layer integrates this example into `iotgw-image` for `raspberrypi4-64`:

```bash
bitbake iotgw-image
```

Flash the resulting image from `tmp/deploy/images/raspberrypi4-64/` to
your SD card. It includes the module, boot overlay, firmware configuration,
and module autoload configuration. With the wiring connected, the LED
should light at boot. Check `lsmod | grep led_button` and
`dmesg | grep -i 'led'`; `rmmod led_button` turns it off and
`modprobe led_button` turns it back on.

## Alternative: manual deployment to an existing image

Before deploying, inspect GPIO ownership on the Pi:

```bash
uname -r
mountpoint /sys/kernel/debug || mount -t debugfs debugfs /sys/kernel/debug
cat /sys/kernel/debug/gpio
findmnt /boot
ls /boot/config.txt /boot/overlays
```

Ensure GPIO17 is free. If the firmware boot partition is mounted somewhere
else, substitute its actual mountpoint below. If it is read-only, enable
writes using your system's normal boot-partition maintenance procedure.

From Ubuntu, copy the module path found above and the overlay:

```bash
scp <path-to-led_button.ko> root@172.20.10.4:/tmp/
scp tmp/deploy/images/raspberrypi4-64/led-button.dtbo root@172.20.10.4:/tmp/
```

On the Pi, back up and edit the firmware configuration:

```bash
cp -n /boot/config.txt /boot/config.txt.before-led-button
cp /tmp/led-button.dtbo /boot/overlays/led-button.dtbo
```

Add `dtoverlay=led-button` under a section applicable to the Pi 4
(for example `[all]`) in `/boot/config.txt`, then reboot when convenient.
Copying only the `.ko` file does not install the package's autoload configuration.
Because `/tmp` is cleared on reboot, copy the module to `/tmp` again afterward.

## Hardware test

```bash
tr '\0' '\n' </proc/device-tree/led_button/compatible
insmod /tmp/led_button.ko
dmesg | tail -20
readlink /sys/bus/platform/devices/led_button/driver
rmmod led_button
dmesg | tail -20
```

Expected: the compatible string is `custom,led-button`, the driver link
points to `led-button`, loading lights the LED and logs `LED on`, and
unloading extinguishes it and logs `LED off`. A successful `insmod` alone
does not prove the device matched or that probe succeeded. GPIO request
failures appear in the kernel log.

Remove the `dtoverlay=led-button` line and reboot to disable the platform
device. Button support follows the successful LED test.
