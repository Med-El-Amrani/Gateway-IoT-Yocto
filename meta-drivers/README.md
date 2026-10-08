# meta-drivers

Yocto Scarthgap layer for custom device drivers and their Device Tree overlays.
Application and image recipes live in the separate `meta-iotgw` layer.

Requires OE-Core (`core`). The current LED recipes also require the
`raspberrypi4-64` machine supplied by `meta-raspberrypi`.

Both repository build configurations include this layer. To enable it in
another initialized build, run:

```bash
bitbake-layers add-layer ../meta-drivers
```

Build the LED-only example:

```bash
bitbake led-button led-button-overlay
```

See [LED wiring, deployment and test instructions](recipes-kernel/led-button/README.md).
For `raspberrypi4-64`, the layer includes both recipes in `iotgw-image`,
copies the overlay to the firmware boot partition, and enables it in
`config.txt`. The module loads automatically at boot. Build the complete
image with `bitbake iotgw-image`. This LED-only example claims GPIO17 and
turns the LED on when the driver probes.

Layer metadata is MIT licensed (see `COPYING.MIT`). Driver and overlay source
licenses are specified by their SPDX headers and recipes.
