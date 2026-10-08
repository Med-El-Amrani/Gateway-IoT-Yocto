# Firmware creates the platform device before the module loads.
RPI_EXTRA_CONFIG:append:raspberrypi4-64 = "\n[all]\ndtoverlay=led-button\n"
