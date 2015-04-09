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

  AST_MATCHER(ObjCInterfaceDecl, isImplementationInMainFile) {
    if (auto *ImpDecl = Node.getImplementation()) {
      auto &SourceManager = Finder->getASTContext().getSourceManager();
      return SourceManager.isInMainFile(SourceManager.getExpansionLoc(ImpDecl->getLocStart()));
    } else {
      return false;
    }
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

  static Import importForFileLoc(const SourceLocation L, const SourceManager &SM) {
    if (SM.isLoadedSourceLocation(L)) {
      return Import(ImportType::Module, SM.getModuleImportLoc(L).second);
    }

    auto Path = SM.getFilename(L);
    if (SM.isInSystemHeader(L)) {
      return Import(ImportType::Library, Path);
    }

    auto Filename = Path.drop_front(Path.find_last_of('/') + 1);
    return Import(ImportType::File, Filename);
  }

  static const clang::FileEntry*
  fileIncludingFile(std::map<const FileEntry*,
                    std::set<const FileEntry*>> &Map,
                    const FileEntry *F) {
    for (auto I : Map) {
      if (I.second.count(F)) {
        return I.first;
      }
    }
    return F;
  }

  static FileID
  topFileIncludingFile(std::map<const FileEntry*,
                       std::set<const FileEntry*>> &Map,
                       FileID File,
                       const SourceManager &SM) {
    auto *FE = SM.getFileEntryForID(File);
    const FileEntry *PE;
    do {
      PE = FE;
      FE = fileIncludingFile(Map, PE);
    } while (PE != FE);

    return SM.translateFile(FE);
  }

  // TODO: strip out forward declares too
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
      // TODO: use the imported module

      if (File && SM.isInSystemHeader(HashLoc)) {
        Matcher.addLibraryInclude(SM.getFileEntryForID(SM.getFileID(HashLoc)), File);
      } else {
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
    auto &SM = CI.getSourceManager();
    auto &PP = CI.getPreprocessor();
    PP.addPPCallbacks(std::unique_ptr<ImportCallbacks>(new ImportCallbacks(SM, Matcher)));
    SourceMgr = &SM;

    return true;
  }

  void FileCallbacks::handleEndSource() {
    Matcher.flush(*SourceMgr);
  }

  void CallExprCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *FD = Result.Nodes.getNodeAs<CallExpr>(nodeKey)->getDirectCallee()) {
      auto &SM = *Result.SourceManager;
      Matcher.addImport(SM.getMainFileID(), FD->getLocation(), SM);
    }
  }

  void InterfaceCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *ID = Result.Nodes.getNodeAs<ObjCInterfaceDecl>(nodeKey)) {
      auto &SM = *Result.SourceManager;
      auto InFile = SM.getFileID(ID->getLocation());

      if (auto *SC = ID->getSuperClass()) {
        Matcher.addImport(InFile, SC->getLocation(), SM);
      }

      if (InFile != SM.getMainFileID()) {
        Matcher.addImport(SM.getMainFileID(), ID->getLocation(), SM);
      }

      for (auto *P : ID->protocols()) {
        Matcher.addImport(InFile, P->getLocation(), SM);
      }

      for (auto *C : ID->visible_categories()) {
        auto CategoryFile = SM.getFileID(C->getLocation());

        for (auto *P : C->protocols()) {
          Matcher.addImport(CategoryFile, P->getLocation(), SM);
        }
      }
    }
  }

  void MessageExprCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *E = Result.Nodes.getNodeAs<ObjCMessageExpr>(nodeKey)) {
      auto &SM = *Result.SourceManager;

      if (auto *ID = E->getReceiverInterface()) {
        Matcher.addImport(SM.getMainFileID(), ID->getLocation(), SM);
      } else if (auto *Ptr = E->getReceiverType()->getAs<ObjCObjectPointerType>()) {
        assert(Ptr->isObjCQualifiedIdType());
        for (auto i = Ptr->qual_begin(); i != Ptr->qual_end(); i++) {
          Matcher.addImport(SM.getMainFileID(), (*i)->getLocation(), SM);
        }
      } else {
        llvm::outs() << "failed to unpack expr of kind " << E->getReceiverKind();
        assert(0);
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
      if (auto *ID = PT->getInterfaceDecl()) {
        // TODO: forward declare if it is a system library too
        // but check first if it is already imported
        if (SM.isInSystemHeader(ID->getLocation())) {
          Matcher.addImport(InFile, ID->getLocation(), SM);
        } else {
          Matcher.addForwardDeclare(InFile, ID->getName());
        }
      }
    }
  }

  void ProtocolCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *PD = Result.Nodes.getNodeAs<ObjCProtocolExpr>(nodeKey)->getProtocol()) {
      auto &SM = *Result.SourceManager;
      Matcher.addImport(SM.getMainFileID(), PD->getLocation(), SM);
    }
  }

  std::unique_ptr<FrontendActionFactory>
  ImportMatcher::getActionFactory(MatchFinder& Finder) {
    auto CallMatcher = callExpr(isExpansionInMainFile()).bind(nodeKey);
    auto InterfaceMatcher = interface(isImplementationInMainFile()).bind(nodeKey);
    auto MsgMatcher = message(isExpansionInMainFile()).bind(nodeKey);
    auto MtdMatcher = method(isDefinedInHeader()).bind(nodeKey);
    auto ProtoMatcher = protocol(isExpansionInMainFile()).bind(nodeKey);
    Finder.addMatcher(CallMatcher, &CallCallback);
    Finder.addMatcher(InterfaceMatcher, &InterfaceCallback);
    Finder.addMatcher(MsgMatcher, &MsgCallback);
    Finder.addMatcher(MtdMatcher, &MtdCallback);
    Finder.addMatcher(ProtoMatcher, &ProtoCallback);

    return newFrontendActionFactory(&Finder, &FileCallbacks);
  }

  void ImportMatcher::addLibraryInclude(const FileEntry *HE, const FileEntry *FE) {
    // some framework headers import themselves
    if (HE == FE)
      return;

    // don't allow circular imports
    if (LibraryImportMap.count(FE) > 0)
      return;

    // don't track includes across libraries
    if (HE->getDir() != FE->getDir())
      return;

    LibraryImportMap[HE].insert(FE);
  }

  void ImportMatcher::addForwardDeclare(const FileID InFile, llvm::StringRef Name) {
    ImportMap[InFile].insert(Import(ImportType::ForwardDeclare, Name));
  }

  void ImportMatcher::addImport(const FileID InFile,
                                const SourceLocation Loc,
                                const SourceManager &SM) {
    auto OfFile = SM.getFileID(Loc);
    auto ImportedFile = topFileIncludingFile(LibraryImportMap, OfFile, SM);
    auto ImportedLoc = SM.getLocForStartOfFile(ImportedFile);
    ImportMap[InFile].insert(importForFileLoc(ImportedLoc, SM));
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
      for (auto &Import : Pair.second) {
        ImportStr << Import << '\n';
      }

      auto fid = Pair.first;
      auto start = SM.getLocForStartOfFile(fid);
      auto replacementLength = ImportOffset[fid];
      if (replacementLength == 0) {
        // make sure there is at least a line of whitespace after the new imports
        ImportStr << '\n';
      }

      Replacements.insert(Replacement(SM, start, replacementLength, ImportStr.str()));
      out << "File ID: " << fid.getHashValue() << "\n" << ImportStr.str() << "\n";
    }
    ImportMap.clear();
    ImportOffset.clear();
  }

} // end namespace import_tidy
