set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_executable(import-tidy
  ImportTidy.cpp
  ImportMatcher.cpp
  Import.cpp
  ImportCallbacks.cpp
  )

target_link_libraries(import-tidy
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangTooling
  )
