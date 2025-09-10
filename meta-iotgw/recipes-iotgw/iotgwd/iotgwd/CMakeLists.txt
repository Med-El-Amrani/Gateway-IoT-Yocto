cmake_minimum_required(VERSION 3.16)
project(iotgwd C)
add_executable(iotgwd src/main.c)
install(TARGETS iotgwd RUNTIME DESTINATION bin)
