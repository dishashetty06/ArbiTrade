add_executable(agent
    agent.cpp
)

target_link_libraries(agent PRIVATE ws2_32)

set_target_properties(agent PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)