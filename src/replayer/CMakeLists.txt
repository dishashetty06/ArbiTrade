add_executable(replayer
    replayer.cpp
)

target_link_libraries(replayer PRIVATE arbitrade_core ws2_32)

set_target_properties(replayer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)