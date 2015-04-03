#ifndef __LLVM__ImportMatchers__
#define __LLVM__ImportMatchers__

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/FrontEnd/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/Refactoring.h"
#include <string>
#include <unordered_set>

namespace import_tidy {
  class ImportMatcher;

  class FileCallbacks : public clang::tooling::SourceFileCallbacks {
  public:
    FileCallbacks(ImportMatcher &Matcher) : matcher(Matcher) { };
    bool handleBeginSource(clang::CompilerInstance&, llvm::StringRef) override;
    void handleEndSource() override;
  private:
    ImportMatcher &matcher;
  };

  class CallExprCallback : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    CallExprCallback(ImportMatcher &Matcher) : matcher(Matcher) { };
    void run(const clang::ast_matchers::MatchFinder::MatchResult&) override;
  private:
    ImportMatcher &matcher;
  };

  class InterfaceCallback : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    InterfaceCallback(ImportMatcher &Matcher) : matcher(Matcher) { };
    void run(const clang::ast_matchers::MatchFinder::MatchResult&) override;
  private:
    ImportMatcher &matcher;
  };

  class MessageExprCallback : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    MessageExprCallback(ImportMatcher &Matcher) : matcher(Matcher) { };
    void run(const clang::ast_matchers::MatchFinder::MatchResult&) override;
  private:
    ImportMatcher &matcher;
  };

  class MethodCallback : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    MethodCallback(ImportMatcher &Matcher) : matcher(Matcher) { };
    void run(const clang::ast_matchers::MatchFinder::MatchResult&) override;
  private:
    void addType(clang::QualType);
    ImportMatcher &matcher;
  };

  class ImportMatcher {
  public:
    ImportMatcher(clang::tooling::Replacements &Replacements) :
      headerImports(), headerClasses(), impImports(), callCallback(*this),
      interfaceCallback(*this), msgCallback(*this), mtdCallback(*this),
      fileCallbacks(*this), replacements(Replacements) { };

    std::unique_ptr<clang::tooling::FrontendActionFactory>
      getActionFactory(clang::ast_matchers::MatchFinder&);
    void dumpImports(llvm::raw_ostream&);
    void addHeaderForwardDeclare(llvm::StringRef Name);
    void addImportFile(std::string Path, bool InImplementation);
    void removeImport(clang::tooling::Replacement R);
  private:
    std::unordered_set<std::string> headerImports, headerClasses, impImports;
    CallExprCallback callCallback;
    InterfaceCallback interfaceCallback;
    MessageExprCallback msgCallback;
    MethodCallback mtdCallback;
    FileCallbacks fileCallbacks;
    clang::tooling::Replacements &replacements;
  };
};

#endif /* defined(__LLVM__ImportMatchers__) */
