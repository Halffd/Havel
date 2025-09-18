set(AUTOMATION_SOURCES
    AutoPresser.cpp
    AutoClicker.cpp
    AutoRunner.cpp
    AutoKeyPresser.cpp
    AutomationManager.cpp
    ChainedTask.cpp
)

add_library(automation STATIC ${AUTOMATION_SOURCES})

# Set include directories
set(AUTOMATION_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src
)

target_include_directories(automation
    PUBLIC ${AUTOMATION_INCLUDE_DIRS}
)

# Link against core libraries
target_link_libraries(automation
    PUBLIC
        core
    PRIVATE
        Threads::Threads
)

# Set C++ standard
target_compile_features(automation PRIVATE cxx_std_17)

# Add warnings
if(MSVC)
    target_compile_options(automation PRIVATE /W4 /WX)
else()
    target_compile_options(automation PRIVATE -Wall -Wextra -Werror)
endif()

# Install rules if needed
if(INSTALL_DEVELOPMENT_FILES)
    install(TARGETS automation
        EXPORT HavelTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/core/automation
        FILES_MATCHING PATTERN "*.hpp"
    )
endif()
