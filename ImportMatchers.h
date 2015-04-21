#ifndef __LLVM__ImportMatchers__
#define __LLVM__ImportMatchers__

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/FrontEnd/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/Refactoring.h"
#include "Import.h"
#include <string>
#include <map>
#include <set>

namespace import_tidy {
  class ImportMatcher;

  class FileCallbacks : public clang::tooling::SourceFileCallbacks {
  public:
    FileCallbacks(ImportMatcher &Matcher) : Matcher(Matcher) { };
    bool handleBeginSource(clang::CompilerInstance&, llvm::StringRef) override;
    void handleEndSource() override;
  private:
    ImportMatcher &Matcher;
    const clang::SourceManager *SourceMgr;
  };

  class CallExprCallback : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    CallExprCallback(ImportMatcher &Matcher) : Matcher(Matcher) { };
    void run(const clang::ast_matchers::MatchFinder::MatchResult&) override;
  private:
    ImportMatcher &Matcher;
  };

  class CastExprCallback : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    CastExprCallback(ImportMatcher &Matcher) : Matcher(Matcher) { };
    void run(const clang::ast_matchers::MatchFinder::MatchResult&) override;
  private:
    ImportMatcher &Matcher;
  };

  class InterfaceCallback : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    InterfaceCallback(ImportMatcher &Matcher) : Matcher(Matcher) { };
    void run(const clang::ast_matchers::MatchFinder::MatchResult&) override;
  private:
    ImportMatcher &Matcher;
  };

  class MessageExprCallback : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    MessageExprCallback(ImportMatcher &Matcher) : Matcher(Matcher) { };
    void run(const clang::ast_matchers::MatchFinder::MatchResult&) override;
  private:
    ImportMatcher &Matcher;
  };

  class MethodCallback : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    MethodCallback(ImportMatcher &Matcher) : Matcher(Matcher) { };
    void run(const clang::ast_matchers::MatchFinder::MatchResult&) override;
  private:
    void addType(const clang::FileID, clang::QualType, const clang::SourceManager&);
    ImportMatcher &Matcher;
  };

  class ProtocolCallback : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    ProtocolCallback(ImportMatcher &Matcher) : Matcher(Matcher) { };
    void run(const clang::ast_matchers::MatchFinder::MatchResult&) override;
  private:
    ImportMatcher &Matcher;
  };

  class StripCallback : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    StripCallback(ImportMatcher &Matcher) : Matcher(Matcher) { };
    void run(const clang::ast_matchers::MatchFinder::MatchResult&) override;
  private:
    ImportMatcher &Matcher;
  };

  class ImportMatcher {
  public:
    ImportMatcher(clang::tooling::Replacements &Replacements) :
      ImportRanges(), ImportMap(),
      CallCallback(*this), CastCallback(*this), InterfaceCallback(*this),
      MsgCallback(*this), MtdCallback(*this), ProtoCallback(*this),
      StripCallback(*this), FileCallbacks(*this), Replacements(Replacements) {};

    std::unique_ptr<clang::tooling::FrontendActionFactory>
      getActionFactory(clang::ast_matchers::MatchFinder&);
    llvm::StringRef getSysroot() { return llvm::StringRef(Sysroot); }
    void setSysroot(std::string SR) { Sysroot = SR; }
    void addImport(const clang::FileID InFile,
                   const clang::Decl*,
                   const clang::SourceManager&,
                   bool isForwardDeclare = false);
    void removeImport(const clang::SourceLocation, const clang::SourceManager&);
    void flush(const clang::SourceManager&);
    void printLibraryCounts(llvm::raw_ostream&);
  private:
    std::map<clang::FileID, std::vector<clang::tooling::Range>> ImportRanges;
    std::map<clang::FileID, std::vector<Import>> ImportMap;
    CallExprCallback CallCallback;
    CastExprCallback CastCallback;
    InterfaceCallback InterfaceCallback;
    MessageExprCallback MsgCallback;
    MethodCallback MtdCallback;
    ProtocolCallback ProtoCallback;
    StripCallback StripCallback;
    FileCallbacks FileCallbacks;
    clang::tooling::Replacements &Replacements;
    std::string Sysroot;
  };
};

#endif /* defined(__LLVM__ImportMatchers__) */
