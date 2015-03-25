#ifndef __LLVM__ImportMatchers__
#define __LLVM__ImportMatchers__

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

void registerMatchers(clang::ast_matchers::MatchFinder&);
std::vector<std::string> collectImports();

#endif /* defined(__LLVM__ImportMatchers__) */
