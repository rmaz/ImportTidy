#ifndef __LLVM__ImportCallbacks__
#define __LLVM__ImportCallbacks__

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Tooling.h"

namespace import_tidy {
  class ImportMatcher;
  static const llvm::StringRef nodeKey = "key";

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

  class DeclRefCallback : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    DeclRefCallback(ImportMatcher &Matcher) : Matcher(Matcher) { };
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
}

#endif /* defined(__LLVM__ImportCallbacks__) */
