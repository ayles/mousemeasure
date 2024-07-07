cmake_minimum_required(VERSION 3.22)
project(mousemeasure)

find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)

add_executable(mousemeasure main.cpp)
target_link_libraries(mousemeasure glfw GLEW::GLEW)
set_property(TARGET mousemeasure PROPERTY CXX_STANDARD 20)

install(TARGETS mousemeasure RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
