#ifndef __LLVM__ImportMatchers__
#define __LLVM__ImportMatchers__

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <unordered_set>

using namespace clang::ast_matchers;

namespace import_tidy {
  class MessageExprCallback : public MatchFinder::MatchCallback {
  public:
    MessageExprCallback(std::unordered_set<std::string> &imports) :
      imports(imports), class_names() { };
    void run(const MatchFinder::MatchResult &Result) override;
  private:
    std::unordered_set<std::string> &imports, class_names;
  };

  class CallExprCallback : public MatchFinder::MatchCallback {
  public:
    CallExprCallback(std::unordered_set<std::string> &imports) :
      imports(imports), function_names() { };
    void run(const MatchFinder::MatchResult &Result) override;
  private:
    std::unordered_set<std::string> &imports, function_names;
  };

  class ImportMatcher {
  public:
    ImportMatcher() :
      imports(), msgCallback(imports), callCallback(imports) { };
    void registerMatchers(MatchFinder&);
    std::vector<std::string> collectImports();
  private:
    std::unordered_set<std::string> imports;
    MessageExprCallback msgCallback;
    CallExprCallback callCallback;
  };
};

#endif /* defined(__LLVM__ImportMatchers__) */
