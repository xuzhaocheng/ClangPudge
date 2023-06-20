set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_executable(clang-pudge ClangPudge.cpp)

target_link_libraries(clang-pudge
  PRIVATE
  clangAST
  clangBasic
  clangTooling
  )