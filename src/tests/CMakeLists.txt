cmake_minimum_required(VERSION 3.10)

# Add the hotkey test executable
add_executable(hotkey_test
    hotkey_test.cpp
)

# Add the IO test executable
add_executable(io_test
    io_test.cpp
)

# Add the IO modifier test executable
add_executable(io_modifier_test
    io_modifier_test.cpp
)

# Add the IO race condition test executable
add_executable(io_race_condition_test
    io_race_condition_test.cpp
)

# Link with the necessary libraries
target_link_libraries(hotkey_test
    core
    window
    ${X11_LIBRARIES}
    ${X11_Xext_LIB}
    ${X11_XTest_LIB}
    ${X11_Xtst_LIB}
    pthread
)

# Link the IO test with necessary libraries
target_link_libraries(io_test
    core
    window
    ${X11_LIBRARIES}
    ${X11_Xext_LIB}
    ${X11_XTest_LIB}
    ${X11_Xtst_LIB}
    pthread
)

# Link the IO modifier test with necessary libraries
target_link_libraries(io_modifier_test
    core
    window
    ${X11_LIBRARIES}
    ${X11_Xext_LIB}
    ${X11_XTest_LIB}
    ${X11_Xtst_LIB}
    pthread
)

# Link the IO race condition test with necessary libraries
target_link_libraries(io_race_condition_test
    core
    window
    ${X11_LIBRARIES}
    ${X11_Xext_LIB}
    ${X11_XTest_LIB}
    ${X11_Xtst_LIB}
    pthread
)

# Install the test executables
install(TARGETS 
    hotkey_test
    io_test
    io_modifier_test
    io_race_condition_test
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)