## ImportTidy
A clang tool to automatically optimize imports in Objective C files. 
Implementation files will import any necessary headers, header files
will forward declare as much as possible. Very much a WIP.

## Getting Started
1. Download llvm 3.6.0 with clang and clang extra [here] (http://llvm.org/releases/download.html#3.6.0)
2. Unpack llvm, unpack clang to `llvm/tools/clang`, unpack clang extra to `llvm/tools/clang/tools/extra'
3. Checkout the repo to `llvm/tools/clang/tools/extra/import-tidy`
4. Add the line `add_subdirectory(import-tidy)` to the `llvm/tools/clang/tools/extra/CMakeLists.txt` file
5. Build llvm using CMake as ususal, this should generate the `import-tidy` binary
