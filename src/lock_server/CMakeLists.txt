add_executable(lock_server
    lock_server.cpp
)

target_link_libraries(lock_server PRIVATE arbitrade_core ws2_32)

set_target_properties(lock_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)