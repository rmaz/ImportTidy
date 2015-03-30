#ifndef __LLVM__ImportMatchers__
#define __LLVM__ImportMatchers__

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <string>
#include <unordered_set>

using namespace clang::ast_matchers;

namespace import_tidy {
  class ImportMatcher;

  class CallExprCallback : public MatchFinder::MatchCallback {
  public:
    CallExprCallback(ImportMatcher &Matcher) : matcher(Matcher) { };
    void run(const MatchFinder::MatchResult &Result) override;
  private:
    ImportMatcher &matcher;
  };

  class InterfaceCallback : public MatchFinder::MatchCallback {
  public:
    InterfaceCallback(ImportMatcher &Matcher) : matcher(Matcher) { };
    void run(const MatchFinder::MatchResult &Result) override;
  private:
    ImportMatcher &matcher;
  };

  class MessageExprCallback : public MatchFinder::MatchCallback {
  public:
    MessageExprCallback(ImportMatcher &Matcher) :
      matcher(Matcher), classNames() { };
    void run(const MatchFinder::MatchResult &Result) override;
  private:
    ImportMatcher &matcher;
    std::unordered_set<std::string> classNames;
  };

  class ImportMatcher {
  public:
    ImportMatcher() : headerImports(), headerClasses(), impImports(),
      callCallback(*this), interfaceCallback(*this), msgCallback(*this) { };
    void registerMatchers(MatchFinder&);
    void dumpImports(llvm::raw_ostream&);
    void addHeaderForwardDeclare(llvm::StringRef Name) { };
    void addImportFile(std::string Path, bool InImplementation);
  private:
    std::unordered_set<std::string> headerImports, headerClasses, impImports;
    CallExprCallback callCallback;
    InterfaceCallback interfaceCallback;
    MessageExprCallback msgCallback;
  };
};

#endif /* defined(__LLVM__ImportMatchers__) */
