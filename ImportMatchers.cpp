#include "ImportMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/ASTMatchers/ASTMatchersInternal.h"
#include "clang/AST/ExprObjC.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace import_tidy;

namespace clang {
namespace ast_matchers {
  static const internal::VariadicDynCastAllOfMatcher<Stmt, ObjCMessageExpr> message;
  static const internal::VariadicDynCastAllOfMatcher<Decl, ObjCInterfaceDecl> interface;
  static const internal::VariadicDynCastAllOfMatcher<Decl, ObjCMethodDecl> method;
  static const internal::VariadicDynCastAllOfMatcher<Stmt, ObjCProtocolExpr> protocol;
  static const internal::VariadicDynCastAllOfMatcher<Decl, ImportDecl> import;

  AST_MATCHER(ObjCInterfaceDecl, isImplementationInMainFile) {
    if (!Node.isThisDeclarationADefinition())
      return false;

    if (auto *ImpDecl = Node.getImplementation()) {
      auto &SourceManager = Finder->getASTContext().getSourceManager();
      return SourceManager.isInMainFile(SourceManager.getExpansionLoc(ImpDecl->getLocStart()));
    } else {
      return false;
    }
  }

  AST_MATCHER(ObjCInterfaceDecl, isForwardDeclare) {
    return !Node.hasDefinition();
  }

  AST_MATCHER(Decl, isNotInSystemHeader) {
    auto Loc = Node.getLocation();
    if (!Loc.isValid())
      return false;

    auto &SM = Finder->getASTContext().getSourceManager();
    return !SM.isInSystemHeader(Loc);
  }

  AST_MATCHER(ObjCMethodDecl, isDefinedInHeader) {
    auto *ID = Node.getClassInterface();
    if (!ID || !ID->getImplementation())
      return false;

    auto &SM = Finder->getASTContext().getSourceManager();
    if (SM.isInMainFile(SM.getExpansionLoc(Node.getLocStart())))
      return false;

    auto ImpLoc = SM.getExpansionLoc(ID->getImplementation()->getLocStart());
    return SM.isInMainFile(ImpLoc);
  }

} // end namespace ast_matchers
} // end namespace clang

namespace {

  static bool haveReplacementForFile(Replacements &Replacements, StringRef Path) {
    auto Found = std::find_if(Replacements.begin(), Replacements.end(),
                              [Path](const Replacement &R) {
                                return R.getFilePath() == Path;
                              });
    return Found != Replacements.end();
  }

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

} // end anonymous namespace

namespace import_tidy {
  static const StringRef nodeKey = "key";

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

      if (auto *ID = E->getReceiverInterface()) {
        Matcher.addImport(SM.getMainFileID(), ID, SM);
      } else if (auto *Ptr = E->getReceiverType()->getAs<ObjCObjectPointerType>()) {
        for (auto i = Ptr->qual_begin(); i != Ptr->qual_end(); i++) {
          Matcher.addImport(SM.getMainFileID(), *i, SM);
        }
      }
    }
  }

  void MethodCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *M = Result.Nodes.getNodeAs<ObjCMethodDecl>(nodeKey)) {
      auto FID = Result.SourceManager->getFileID(M->getLocation());
      addType(FID, M->getReturnType(), *Result.SourceManager);

      for (auto i = M->param_begin(); i != M->param_end(); i++) {
        addType(FID, (*i)->getType(), *Result.SourceManager);
      }
    }
  }

  void MethodCallback::addType(const FileID InFile, QualType T, const SourceManager &SM) {
    if (auto *PT = T->getAs<ObjCObjectPointerType>()) {
      // import or forward declare the class type
      if (auto *ID = PT->getInterfaceDecl()) {
        auto Filename = SM.getFilename(ID->getLocation());
        bool isSystemDecl = Filename.startswith(Matcher.getSysroot());
        Matcher.addImport(InFile, ID, SM, !isSystemDecl);
      }

      // import or forward declare any protocols being conformed to
      for (auto i = PT->qual_begin(); i != PT->qual_end(); i++) {
        auto Filename = SM.getFilename((*i)->getLocation());
        bool isSystemDecl = Filename.startswith(Matcher.getSysroot());
        Matcher.addImport(InFile, *i, SM, !isSystemDecl);
      }
    } else if (auto *TD = T->getAs<TypedefType>()) {
      // any typedefs need to be imported
      if (auto *TypeDecl = TD->getDecl()) {
        Matcher.addImport(InFile, TypeDecl, SM);
      }
    }
  }

  void ProtocolCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *PD = Result.Nodes.getNodeAs<ObjCProtocolExpr>(nodeKey)->getProtocol()) {
      auto &SM = *Result.SourceManager;
      Matcher.addImport(SM.getMainFileID(), PD, SM);
    }
  }

  void StripCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *D = Result.Nodes.getNodeAs<Decl>(nodeKey)) {
      Matcher.removeImport(D->getLocation(), *Result.SourceManager);
    }
  }

  std::unique_ptr<FrontendActionFactory>
  ImportMatcher::getActionFactory(MatchFinder& Finder) {
    auto CallMatcher = callExpr(isExpansionInMainFile()).bind(nodeKey);
    auto InterfaceMatcher = interface(isImplementationInMainFile()).bind(nodeKey);
    auto ForwardDeclareMatcher = interface(isNotInSystemHeader(), isForwardDeclare()).bind(nodeKey);
    auto ImportMatcher = import(isNotInSystemHeader()).bind(nodeKey);
    auto MsgMatcher = message(isExpansionInMainFile()).bind(nodeKey);
    auto MtdMatcher = method(isDefinedInHeader()).bind(nodeKey);
    auto ProtoMatcher = protocol(isExpansionInMainFile()).bind(nodeKey);
    Finder.addMatcher(CallMatcher, &CallCallback);
    Finder.addMatcher(InterfaceMatcher, &InterfaceCallback);
    Finder.addMatcher(ForwardDeclareMatcher, &StripCallback);
    Finder.addMatcher(ImportMatcher, &StripCallback);
    Finder.addMatcher(MsgMatcher, &MsgCallback);
    Finder.addMatcher(MtdMatcher, &MtdCallback);
    Finder.addMatcher(ProtoMatcher, &ProtoCallback);

    return newFrontendActionFactory(&Finder, &FileCallbacks);
  }

  void ImportMatcher::addImport(const FileID InFile,
                                const Decl *D,
                                const SourceManager &SM,
                                bool isForwardDeclare) {
    ImportMap[InFile].push_back(Import(SM, D, isForwardDeclare));
  }

  void ImportMatcher::removeImport(const SourceLocation Loc, const SourceManager &SM) {
    auto fid = SM.getFileID(Loc);
    auto *fileStart = SM.getBuffer(fid)->getBufferStart();
    unsigned start = SM.getFileOffset(Loc);
    unsigned end = strchr(fileStart + start, '\n') - fileStart + 1;

    // TODO: this is a dangerous assumption, should be modelled as a set of ranges
    ImportOffset[fid] = std::max(ImportOffset[fid], end);
  }

  void ImportMatcher::flush(const SourceManager &SM) {
    auto &out = llvm::outs();

    for (auto &Pair : ImportMap) {
      std::string import;
      llvm::raw_string_ostream ImportStr(import);
      auto Imports = Pair.second;
      sortedUniqueImports(Imports);

      for (auto &Import : Imports) {
        ImportStr << Import << '\n';
      }

      auto Fid = Pair.first;
      auto Start = SM.getLocForStartOfFile(Fid);
      auto Path = SM.getFilename(Start);
      if (haveReplacementForFile(Replacements, Path))
        continue;

      auto replacementLength = ImportOffset[Fid];
      if (replacementLength == 0) {
        // make sure there is at least a line of whitespace after the new imports
        ImportStr << '\n';
      }

      Replacements.insert(Replacement(SM, Start, replacementLength, ImportStr.str()));
      out << "File: " << SM.getFilename(Start) << "\n";
      out << ImportStr.str() << "\n";
    }
    ImportMap.clear();
    ImportOffset.clear();
  }

  void ImportMatcher::printLibraryCounts(llvm::raw_ostream &OS) {
//    using ImpPair = std::pair<Import, unsigned>;
//    std::vector<ImpPair> counts(LibraryImportCount.begin(), LibraryImportCount.end());
//    std::sort(counts.begin(), counts.end(), [](const ImpPair &L, const ImpPair &R) {
//      return L.second < R.second;
//    });
//
//    OS << "\n\n";
//    OS << "--------------------------------" << "\n";
//    OS << "Libraries sorted by import count" << "\n";
//    OS << "--------------------------------" << "\n";
//    for (auto I = counts.rbegin(); I != counts.rend(); I++) {
//      OS << I->first << " : " << I->second << " times\n";
//    }
  }

} // end namespace import_tidy
