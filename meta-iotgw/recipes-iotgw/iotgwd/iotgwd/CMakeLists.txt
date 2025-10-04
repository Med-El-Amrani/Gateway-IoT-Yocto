cmake_minimum_required(VERSION 3.16)
project(iotgwd C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Install dirs (/usr/bin, etc.) in a portable way
include(GNUInstallDirs)

# Threads (portable -pthread/-lpthread)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# libsystemd for sd_notify/watchdog
find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)

add_executable(iotgwd
  src/main.c           # <-- your service entry
  src/app.c
  src/bridge.c
  src/connector_registry.c
  src/config_loader.c
  src/adapters.c
  src/params_parsers.c
  src/print_config.c
  src/conn_mqtt.c
  src/conn_spi.c
  src/conn_uart.c
  src/conn_http_server.c
  src/log.c
  src/sdwrap.c
  # (keep demo_spi.c and main_gateway.c out of the service binary)
)

target_include_directories(iotgwd PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${SYSTEMD_INCLUDE_DIRS}
)

target_link_libraries(iotgwd PRIVATE
  Threads::Threads
  yaml
  mosquitto
  microhttpd
  ${SYSTEMD_LIBRARIES}
)

# This makes `cmake --install .` place the binary under /usr/bin inside the Yocto image
install(TARGETS iotgwd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
