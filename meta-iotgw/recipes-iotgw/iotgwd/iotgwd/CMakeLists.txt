cmake_minimum_required(VERSION 3.16)
project(iotgwd C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Prefer -pthread over -lpthread when available
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(gateway_main
  src/main_gateway.c
  src/bridge.c
  src/conn_http_server.c
  src/connector_registry.c
  src/config_loader.c
  src/adapters.c
  src/params_parsers.c
  src/print_config.c
  src/conn_mqtt.c
  src/conn_spi.c
  src/conn_uart.c
)

target_include_directories(gateway_main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Link order doesn’t really matter in CMake; Threads::Threads adds the right flags (-pthread)
target_link_libraries(gateway_main
  PRIVATE
    Threads::Threads
    yaml
    mosquitto
    microhttpd
)
