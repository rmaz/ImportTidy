#ifndef __LLVM__ImportMatchers__
#define __LLVM__ImportMatchers__

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <unordered_set>

using namespace clang::ast_matchers;

namespace import_tidy {
  class CallExprCallback : public MatchFinder::MatchCallback {
  public:
    CallExprCallback(std::unordered_set<std::string> &imports) :
      imports(imports), function_names() { };
    void run(const MatchFinder::MatchResult &Result) override;
  private:
    std::unordered_set<std::string> &imports, function_names;
  };

  class InterfaceCallback : public MatchFinder::MatchCallback {
  public:
    InterfaceCallback(std::unordered_set<std::string> &imports) :
      imports(imports) { };
    void run(const MatchFinder::MatchResult &Result) override;
  private:
    std::unordered_set<std::string> &imports;
  };

  class MessageExprCallback : public MatchFinder::MatchCallback {
  public:
    MessageExprCallback(std::unordered_set<std::string> &imports) :
    imports(imports), class_names() { };
    void run(const MatchFinder::MatchResult &Result) override;
  private:
    std::unordered_set<std::string> &imports, class_names;
  };

  class ImportMatcher {
  public:
    ImportMatcher() :
      imports(), msgCallback(imports), callCallback(imports),
      interfaceCallback(imports) { };
    void registerMatchers(MatchFinder&);
    std::vector<std::string> collectImports();
  private:
    std::unordered_set<std::string> imports;
    CallExprCallback callCallback;
    InterfaceCallback interfaceCallback;
    MessageExprCallback msgCallback;
  };
};

#endif /* defined(__LLVM__ImportMatchers__) */
