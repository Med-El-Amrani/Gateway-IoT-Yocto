cmake_minimum_required(VERSION 3.16)
project(iotgwd C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)

add_library(iotgwd_core
    src/app.c
    src/runtime.c
    src/config_loader.c
    src/fs.c
    src/sdwrap.c
    src/log.c
)
target_include_directories(iotgwd_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${SYSTEMD_INCLUDE_DIRS})
target_link_libraries(iotgwd_core PUBLIC ${SYSTEMD_LIBRARIES})

add_executable(iotgwd src/main.c)
target_link_libraries(iotgwd PRIVATE iotgwd_core)

install(TARGETS iotgwd RUNTIME DESTINATION bin)
install(FILES src/../systemd/iotgwd.service DESTINATION lib/systemd/system)
