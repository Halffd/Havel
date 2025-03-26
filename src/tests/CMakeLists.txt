cmake_minimum_required(VERSION 3.10)

# Add the hotkey test executable
add_executable(hotkey_test
    hotkey_test.cpp
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

# Install the test executable
install(TARGETS hotkey_test
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
) 