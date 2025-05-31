# HvC Build Issues and Solutions

## Automatic Build Fix Script

For convenience, an automated build fix script has been created to address most of the common build issues. You can run it using:

```bash
# Make the script executable (if needed)
chmod +x fix_build.sh

# Run the script
./fix_build.sh
```

Alternatively, you can use the Makefile target:

```bash
make fix-build
```

The script performs the following fixes:
1. Installs the Sol2 library (if missing)
2. Fixes type definition conflicts by creating a common types.hpp
3. Adds missing static initializations in DisplayManager
4. Adds missing method declarations in IO.hpp
5. Creates a unified X11 headers include file

After running the fix script, you can rebuild the project with:

```bash
make rebuild
```

If the automatic fixes don't resolve all issues, please refer to the detailed solutions below.

## Fixed Issues

1. **CMakeLists.txt Inconsistency**: 
   - Issue: Inconsistent use of target_link_libraries statements
   - Solution: Ensure all target_link_libraries calls use the PRIVATE keyword.

2. **Missing Dependencies**:
   - Issue: Missing links for X11 libraries and other dependencies
   - Solution: Added dependencies for xkbcommon, XF86 keysym, pthread, and math libraries in both CMakeLists.txt and Makefile.

3. **Directory Structure**:
   - Issue: Files were not organized in a logical directory structure
   - Solution: Implemented configuration and log directories, and updated code to use these directories properly.

## Remaining Issues

1. **Header File Conflicts**:
   - Issue: Multiple definitions of types and enums due to the types.hpp file being included in multiple locations.
   - Solution: The fix script creates a common types.hpp file in src/common/ and updates other files to use it.

2. **Sol2 Lua Library**:
   - Issue: The project requires the Sol2 Lua library, which is not included in the repository.
   - Solution: The fix script automatically installs Sol2 if missing. Alternatively, you can:
     ```bash
     git clone https://github.com/ThePhD/sol2.git -b v3.3.0 include/sol2
     ```
     And update includes to use:
     ```cpp
     #include <sol2/sol.hpp>
     ```

3. **DisplayManager Missing**:
   - Issue: Incomplete implementation of havel::DisplayManager, with several references in the codebase.
   - Solution: The fix script adds missing static initializations. For a full implementation, you might need to manually implement missing methods.

4. **Function Declarations Mismatch**:
   - Issue: Missing or mismatched declarations for several functions, including IO::PressKey.
   - Solution: The fix script adds missing method declarations to IO.hpp.

## Manual Build Process

If you prefer to build manually after fixing the issues:

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

## Debug Flags

To enable debug information during build, set the following flags:

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

Or add to your Makefile:

```make
CXXFLAGS += -g -DDEBUG
``` 