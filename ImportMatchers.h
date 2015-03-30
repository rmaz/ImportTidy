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
    void run(const MatchFinder::MatchResult&) override;
  private:
    ImportMatcher &matcher;
  };

  class InterfaceCallback : public MatchFinder::MatchCallback {
  public:
    InterfaceCallback(ImportMatcher &Matcher) : matcher(Matcher) { };
    void run(const MatchFinder::MatchResult&) override;
  private:
    ImportMatcher &matcher;
  };

  class MessageExprCallback : public MatchFinder::MatchCallback {
  public:
    MessageExprCallback(ImportMatcher &Matcher) : matcher(Matcher) { };
    void run(const MatchFinder::MatchResult&) override;
  private:
    ImportMatcher &matcher;
  };

  class MethodCallback : public MatchFinder::MatchCallback {
  public:
    MethodCallback(ImportMatcher &Matcher) : matcher(Matcher) { };
    void run(const MatchFinder::MatchResult&) override;
  private:
    void addType(clang::QualType);
    ImportMatcher &matcher;
  };

  class ImportMatcher {
  public:
    ImportMatcher() :
      headerImports(), headerClasses(), impImports(), callCallback(*this),
      interfaceCallback(*this), msgCallback(*this), mtdCallback(*this) { };
    void registerMatchers(MatchFinder&);
    void dumpImports(llvm::raw_ostream&);
    void addHeaderForwardDeclare(llvm::StringRef Name);
    void addImportFile(std::string Path, bool InImplementation);
  private:
    std::unordered_set<std::string> headerImports, headerClasses, impImports;
    CallExprCallback callCallback;
    InterfaceCallback interfaceCallback;
    MessageExprCallback msgCallback;
    MethodCallback mtdCallback;
  };
};

#endif /* defined(__LLVM__ImportMatchers__) */
