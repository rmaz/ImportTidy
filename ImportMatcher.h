#ifndef __LLVM__ImportMatchers__
#define __LLVM__ImportMatchers__

#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/Refactoring.h"
#include "Import.h"
#include "ImportCallbacks.h"
#include <map>
#include <set>

namespace import_tidy {

  class ImportMatcher {
  public:
    ImportMatcher(clang::tooling::Replacements &Replacements) :
      ImportRanges(), ImportMap(), LibraryCounts(),
      CallCallback(*this), CategoryCallback(*this),
      DeclRefCallback(*this),
      FuncDeclCallback(*this), InterfaceCallback(*this),
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
    void addHeaderFile(const clang::FileID);
    void addType(const clang::FileID, clang::QualType, const clang::SourceManager&);
    void flush(const clang::SourceManager&);
    void printLibraryCounts(llvm::raw_ostream&);
  private:
    std::set<clang::FileID> headerImportedFiles(const clang::SourceManager&);
    std::map<clang::FileID, std::vector<clang::tooling::Range>> ImportRanges;
    std::map<clang::FileID, std::vector<Import>> ImportMap;
    std::set<clang::FileID> HeaderFiles;
    std::map<llvm::StringRef, unsigned> LibraryCounts;
    CallExprCallback CallCallback;
    CategoryCallback CategoryCallback;
    DeclRefCallback DeclRefCallback;
    FuncDeclCallback FuncDeclCallback;
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
