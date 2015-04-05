#include "ImportMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/ASTMatchers/ASTMatchersInternal.h"
#include "clang/AST/ExprObjC.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace import_tidy;

namespace clang {
namespace ast_matchers {
  static const internal::VariadicDynCastAllOfMatcher<Stmt, ObjCMessageExpr> message;
  static const internal::VariadicDynCastAllOfMatcher<Decl, ObjCInterfaceDecl> interface;
  static const internal::VariadicDynCastAllOfMatcher<Decl, ObjCMethodDecl> method;

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

  static bool isInFramework(StringRef Path) {
    return Path.find(".framework/") != llvm::StringRef::npos;
  }

  static StringRef frameworkName(StringRef Path) {
    auto End = Path.find(".framework/");
    auto Start = Path.rfind('/', End) + 1;
    return Path.substr(Start, End - Start);
  }

  static std::string importForFrameworkName(StringRef Name) {
    std::string import;
    llvm::raw_string_ostream Import(import);

    Import << "#import <" << Name << '/' << Name << ".h>";
    return Import.str();
  }

  static std::string importForSystemLibrary(StringRef Path) {
    std::string import;
    llvm::raw_string_ostream Import(import);
    
    auto Start = Path.rfind("usr/include/") + strlen("usr/include/");
    auto Name = Path.substr(Start);
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
      if (SM.isInMainFile(HashLoc)) {
        auto *fileStart = SM.getBuffer(SM.getMainFileID())->getBufferStart();
        auto start = SM.getFileOffset(HashLoc);
        auto end = strchr(fileStart + start, '\n') - fileStart;
        Matcher.addReplacement(Replacement(SM, HashLoc, end - start + 1, ""));
      } else if (SM.isInSystemHeader(HashLoc)) {
        auto InFile = SM.getFilename(HashLoc);
        if (isInFramework(InFile) && !isInFramework(File->getName())) {
          Matcher.addLibraryInclude(frameworkName(InFile), File->getName());
        }
      }

      // TODO: make this work for header files too
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
    PP.addPPCallbacks(std::unique_ptr<ImportCallbacks>(new ImportCallbacks(SM, matcher)));
    return true;
  }

  void FileCallbacks::handleEndSource() {
    matcher.flush();
  }

  void CallExprCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *FD = Result.Nodes.getNodeAs<CallExpr>(nodeKey)->getDirectCallee()) {
      auto &SM = *Result.SourceManager;
      matcher.addImport(SM.getMainFileID(), FD->getLocation(), SM);
    }
  }

  void InterfaceCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *ID = Result.Nodes.getNodeAs<ObjCInterfaceDecl>(nodeKey)) {
      auto &SM = *Result.SourceManager;
      auto InFile = SM.getFileID(ID->getLocation());

      if (auto *SC = ID->getSuperClass()) {
        matcher.addImport(InFile, SC->getLocation(), SM);
      }

      if (InFile != SM.getMainFileID()) {
        matcher.addImport(SM.getMainFileID(), ID->getLocation(), SM);
      }

      for (auto P = ID->protocol_begin(); P != ID->protocol_end(); P++) {
        matcher.addImport(InFile, (*P)->getLocation(), SM);
      }
    }
  }

  void MessageExprCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *E = Result.Nodes.getNodeAs<ObjCMessageExpr>(nodeKey)) {
      auto &SM = *Result.SourceManager;

      if (auto *ID = E->getReceiverInterface()) {
        matcher.addImport(SM.getMainFileID(), ID->getLocation(), SM);
      } else if (auto *Ptr = E->getReceiverType()->getAs<ObjCObjectPointerType>()) {
        assert(Ptr->isObjCQualifiedIdType());
        for (auto i = Ptr->qual_begin(); i != Ptr->qual_end(); i++) {
          matcher.addImport(SM.getMainFileID(), (*i)->getLocation(), SM);
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
        matcher.addForwardDeclare(InFile, ID->getName());
      }
    }
  }

  std::unique_ptr<FrontendActionFactory>
  ImportMatcher::getActionFactory(MatchFinder& Finder) {
    auto CallMatcher = callExpr(isExpansionInMainFile()).bind(nodeKey);
    auto InterfaceMatcher = interface(isImplementationInMainFile()).bind(nodeKey);
    auto MsgMatcher = message(isExpansionInMainFile()).bind(nodeKey);
    auto MtdMatcher = method(isDefinedInHeader()).bind(nodeKey);
    Finder.addMatcher(CallMatcher, &callCallback);
    Finder.addMatcher(InterfaceMatcher, &interfaceCallback);
    Finder.addMatcher(MsgMatcher, &msgCallback);
    Finder.addMatcher(MtdMatcher, &mtdCallback);

    return newFrontendActionFactory(&Finder, &fileCallbacks);
  }

  void ImportMatcher::addLibraryInclude(StringRef Framework, StringRef ForFile) {
    libraryIncludes[Framework].insert(ForFile);
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
    imports[InFile].insert(Import);
  }

  void ImportMatcher::addReplacement(Replacement R) {
    //replacements.insert(R);
  }

  void ImportMatcher::flush() {
    auto &out = llvm::outs();

    for (auto i = imports.cbegin(); i != imports.cend(); i++) {
      out << "File ID: " << i->first.getHashValue() << "\n";

      for (auto j = i->second.cbegin(); j != i->second.cend(); j++) {
        out << (*j) << "\n";
      }

      out << "\n";
    }
  }

  std::string ImportMatcher::importForLocation(const SourceLocation Loc,
                                               const SourceManager &SM) {
    auto Path = SM.getFilename(Loc);

    // TODO: handle modules

    if (SM.isInSystemHeader(Loc)) {
      // if this is a framework, get the catchall header
      // otherwise just import the include verbatim
      if (isInFramework(Path)) {
        return importForFrameworkName(frameworkName(Path));
      } else {
        for (auto i = libraryIncludes.cbegin(); i != libraryIncludes.cend(); i++) {
          if (i->second.count(Path)) {
            return importForFrameworkName(i->first);
          }
        }
        return importForSystemLibrary(Path);
      }
    } else {
      // TODO: work out somehow if this import could go in the triangle brackets
      // probably if it has already been imported via an umbrella header or something
      return importForFile(Path);
    }
  }
} // end namespace import_tidy
