#include "ImportCallbacks.h"
#include "ImportMatcher.h"
#include "clang/AST/ExprObjC.h"
#include "clang/FrontEnd/CompilerInstance.h"
#include "clang/Lex/Preprocessor.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace import_tidy;

namespace {
  class ImportCallbacks : public clang::PPCallbacks {
  public:
    ImportCallbacks(const SourceManager &SM, ImportMatcher &M) :
    SM(SM), Matcher(M) { };

    void InclusionDirective(SourceLocation HashLoc,
                            const Token &IncludeTok,
                            StringRef FileName,
                            bool IsAngled,
                            CharSourceRange FilenameRange,
                            const FileEntry *File,
                            StringRef SearchPath,
                            StringRef RelativePath,
                            const Module *Imported) override {
      if (!SM.isInSystemHeader(HashLoc)) {
        Matcher.removeImport(HashLoc, SM);
      }
    }

  private:
    const SourceManager &SM;
    ImportMatcher &Matcher;
  };
}

namespace import_tidy {
  bool FileCallbacks::handleBeginSource(CompilerInstance &CI, StringRef Filename) {
    llvm::outs() << "Compiling " << Filename << "\n";
    auto &SM = CI.getSourceManager();
    auto &PP = CI.getPreprocessor();
    PP.addPPCallbacks(std::unique_ptr<ImportCallbacks>(new ImportCallbacks(SM, Matcher)));
    SourceMgr = &SM;
    Matcher.setSysroot(CI.getHeaderSearchOpts().Sysroot);

    return true;
  }

  void FileCallbacks::handleEndSource() {
    Matcher.flush(*SourceMgr);
  }

  void CallExprCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *FD = Result.Nodes.getNodeAs<CallExpr>(nodeKey)->getDirectCallee()) {
      auto &SM = *Result.SourceManager;
      if (SM.isInMainFile(FD->getLocStart())) {
        Matcher.addImport(SM.getMainFileID(), FD, SM);
      }
    }
  }

  void CastExprCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *CE = Result.Nodes.getNodeAs<CStyleCastExpr>(nodeKey)) {
      auto &SM = *Result.SourceManager;
      Matcher.addType(SM.getMainFileID(), CE->getType(), SM);
    }
  }

  void CategoryCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *CD = Result.Nodes.getNodeAs<ObjCCategoryDecl>(nodeKey)) {
      auto &SM = *Result.SourceManager;
      auto InFile = SM.getFileID(CD->getLocation());

      // import categorized class
      Matcher.addImport(InFile, CD->getClassInterface(), SM);

      // import this file, it is a header
      if (InFile != SM.getMainFileID()) {
        Matcher.addHeaderFile(InFile);
        Matcher.addImport(SM.getMainFileID(), CD, SM);
      }
    }
  }

  void DeclRefCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *DRE = Result.Nodes.getNodeAs<DeclRefExpr>(nodeKey)) {
      auto &SM = *Result.SourceManager;
      auto InFile = SM.getFileID(DRE->getLocation());

      // some Decls are located in the main file but have an external type (eg ParmVarDecl)
      // some Decls are located externally but have an already imported type (eg Global constants)
      Matcher.addImport(InFile, DRE->getDecl(), SM);
      Matcher.addType(InFile, DRE->getType(), SM);
    }
  }

  void FuncDeclCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *FD = Result.Nodes.getNodeAs<FunctionDecl>(nodeKey)) {
      // treat files with function prototypes as headers
      for (auto *RD : FD->redecls()) {
        if (RD != FD && RD->isExternC())
          Matcher.addHeaderFile(Result.SourceManager->getFileID(RD->getLocation()));
      }
    }
  }

  void InterfaceCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *ID = Result.Nodes.getNodeAs<ObjCInterfaceDecl>(nodeKey)) {
      auto &SM = *Result.SourceManager;
      auto InFile = SM.getFileID(ID->getLocation());

      // import superclasses
      if (auto *SC = ID->getSuperClass()) {
        Matcher.addImport(InFile, SC, SM);
      }

      // import this file, it is a header
      if (InFile != SM.getMainFileID()) {
        Matcher.addHeaderFile(InFile);
        Matcher.addImport(SM.getMainFileID(), ID, SM);
      }

      // import all protocol definitions
      for (auto *P : ID->protocols()) {
        Matcher.addImport(InFile, P, SM);
      }

      // import all protocol definitions of categories, most likely
      // class extensions in the implementation
      for (auto *Category : ID->visible_categories()) {
        auto CategoryFile = SM.getFileID(Category->getLocation());

        for (auto *Protocol : Category->protocols()) {
          Matcher.addImport(CategoryFile, Protocol, SM);
        }
      }

      // import any categories extending this class or its superclasses
      // that were included from this file
      auto *D = ID;
      do {
        for (auto *Cat : D->visible_categories()) {
          for (auto *Ctx : Cat->noload_decls()) {
            auto Loc = Ctx->getLocStart();
            auto IncludedIn = SM.getFileID(SM.getIncludeLoc(SM.getFileID(Loc)));
            if (IncludedIn == InFile) {
              Matcher.addImport(InFile, Ctx, SM);
            }
          }
        }
      } while ((D = D->getSuperClass()));
    }
  }

  void MessageExprCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *E = Result.Nodes.getNodeAs<ObjCMessageExpr>(nodeKey)) {
      auto &SM = *Result.SourceManager;
      Matcher.addImport(SM.getMainFileID(), E->getMethodDecl(), SM);
      Matcher.addType(SM.getMainFileID(), E->getType(), SM);

      if (auto *ID = E->getReceiverInterface()) {
        Matcher.addImport(SM.getMainFileID(), ID, SM);
      } else {
        Matcher.addType(SM.getMainFileID(), E->getReceiverType(), SM);
      }
    }
  }

  void MethodCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *M = Result.Nodes.getNodeAs<ObjCMethodDecl>(nodeKey)) {
      auto FID = Result.SourceManager->getFileID(M->getLocation());
      Matcher.addType(FID, M->getReturnType(), *Result.SourceManager);

      for (auto i = M->param_begin(); i != M->param_end(); i++) {
        Matcher.addType(FID, (*i)->getType(), *Result.SourceManager);
      }
    }
  }

  void ProtocolCallback::run(const MatchFinder::MatchResult &Result) {
    auto &SM = *Result.SourceManager;

    if (auto *PE = Result.Nodes.getNodeAs<ObjCProtocolExpr>(nodeKey)) {
      Matcher.addImport(SM.getMainFileID(), PE->getProtocol(), SM);
    } else if (auto *PD = Result.Nodes.getNodeAs<ObjCProtocolDecl>(nodeKey)) {
      if (PD->isThisDeclarationADefinition()) {
        for (auto *P : PD->protocols()) {
          Matcher.addImport(SM.getFileID(PD->getLocStart()), P, SM);
        }
      }
    }
  }

  void StripCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *D = Result.Nodes.getNodeAs<Decl>(nodeKey)) {
      // implicit imports will already be stripped by the preprocessor callbacks
      if (!D->isImplicit())
        Matcher.removeImport(D->getLocStart(), *Result.SourceManager);
    }
  }
}
