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

  static StringRef frameworkName(StringRef Path) {
    auto End = Path.rfind(".framework/");
    auto Start = Path.rfind('/', End) + 1;
    return Path.substr(Start, End - Start);
  }

  static StringRef libraryName(StringRef Path) {
    auto LastSplit = Path.rfind('/');
    auto PrevSplit = Path.rfind('/', LastSplit);
    if (PrevSplit == llvm::StringRef::npos) {
      return "";
    }

    return Path.substr(PrevSplit + 1, LastSplit - PrevSplit - 1);
  }

  static bool isInFramework(StringRef Path) {
    return Path.rfind(".framework/") != llvm::StringRef::npos;
  }

  static bool isInSystemLibrary(StringRef Path) {
    return Path.rfind("/usr/include/") != llvm::StringRef::npos;
  }

  static bool isUmbrellaHeader(StringRef Path) {
    auto LastSplit = Path.rfind('/');
    auto FileName = Path.substr(LastSplit + 1);
    return FileName.startswith(libraryName(Path));
  }

  static std::string importForLibraryName(StringRef Name) {
    std::string import;
    llvm::raw_string_ostream Import(import);

    Import << "#import <" << Name << '/' << Name << ".h>";
    return Import.str();
  }

  static std::string importForSystemLibrary(StringRef Path) {
    std::string import;
    llvm::raw_string_ostream Import(import);
    
    auto Start = Path.rfind("/usr/include/") + strlen("/usr/include/");
    auto Name = Path.substr(Start);
    Import << "#import <" << Name << ">";
    return Import.str();
  }

  static std::string importForUserLibrary(StringRef Path) {
    std::string import;
    llvm::raw_string_ostream Import(import);

    auto LastPath = Path.rfind('/');
    auto PreviousPath = Path.rfind('/', LastPath);
    auto Name = Path.substr(PreviousPath + 1);
    Import << "#import <" << Name << ">";
    return Import.str();
  }

  static std::string importForFile(StringRef Path) {
    std::string import;
    llvm::raw_string_ostream Import(import);

    auto Name = Path.drop_front(Path.find_last_of('/') + 1);
    Import << "#import " << '"' << Name << '"';
    return Import.str();
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
      if (SM.isInSystemHeader(HashLoc)) {
        auto InFile = SM.getFilename(HashLoc);
        if (isInFramework(InFile) && isInSystemLibrary(File->getName())) {
          Matcher.addLibraryInclude(frameworkName(InFile), File->getName());
        } else if (!isInFramework(InFile) && !isInSystemLibrary(InFile) && isUmbrellaHeader(InFile)) {
          Matcher.addLibraryInclude(libraryName(InFile), File->getName());
        }
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
      addType(FID, M->getReturnType());

      for (auto i = M->param_begin(); i != M->param_end(); i++) {
        addType(FID, (*i)->getType());
      }
    }
  }

  void MethodCallback::addType(const FileID InFile, QualType T) {
    if (auto *PT = T->getAs<ObjCObjectPointerType>()) {
      if (auto *ID = PT->getInterfaceDecl()) {
        Matcher.addForwardDeclare(InFile, ID->getName());
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

  void ImportMatcher::addLibraryInclude(StringRef LibraryName, StringRef ForFile) {
    LibraryImportMap[LibraryName].insert(ForFile);
  }

  void ImportMatcher::addForwardDeclare(const FileID InFile, llvm::StringRef Name) {
    std::string import;
    llvm::raw_string_ostream Import(import);

    Import << "@class " << Name << ";";
    addImport(InFile, Import.str());
  }

  void ImportMatcher::addImport(const FileID InFile, const SourceLocation ImportLoc, const SourceManager &SM) {
    addImport(InFile, importForLocation(ImportLoc, SM));
  }

  void ImportMatcher::addImport(const FileID InFile, std::string Import) {
    ImportMap[InFile].insert(Import);
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

    for (auto &I : ImportMap) {
      std::string import;
      llvm::raw_string_ostream ImportStr(import);
      for (auto &Import : I.second) {
        ImportStr << Import << '\n';
      }

      auto fid = I.first;
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

  std::string ImportMatcher::importForLocation(const SourceLocation Loc,
                                               const SourceManager &SM) {
    auto Path = SM.getFilename(Loc);

    // TODO: handle modules

    if (SM.isInSystemHeader(Loc)) {
      if (isInFramework(Path)) {
        return importForLibraryName(frameworkName(Path));
      }
      for (auto I : LibraryImportMap) {
        if (I.second.count(Path)) {
          return importForLibraryName(I.first);
        }
      }
      return isInSystemLibrary(Path) ? importForSystemLibrary(Path) :
                                       importForUserLibrary(Path);
    } else {
      return importForFile(Path);
    }
  }

} // end namespace import_tidy
