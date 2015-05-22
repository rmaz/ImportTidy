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

#define IMPORTCALLBACK(NAME) \
  class NAME : public clang::ast_matchers::MatchFinder::MatchCallback { \
  public: \
    NAME(ImportMatcher &Matcher) : Matcher(Matcher) { }; \
    void run(const clang::ast_matchers::MatchFinder::MatchResult&) override; \
  private: \
    ImportMatcher &Matcher; \
  };

  IMPORTCALLBACK(CallExprCallback)
  IMPORTCALLBACK(DeclRefCallback)
  IMPORTCALLBACK(FuncDeclCallback)
  IMPORTCALLBACK(InterfaceCallback)
  IMPORTCALLBACK(MessageExprCallback)
  IMPORTCALLBACK(MethodCallback)
  IMPORTCALLBACK(ProtocolCallback)
  IMPORTCALLBACK(StripCallback)
}

#endif /* defined(__LLVM__ImportCallbacks__) */
