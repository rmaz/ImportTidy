#include "Import.h"
#include "clang/AST/DeclObjC.h"

using namespace llvm;
using namespace clang;

namespace import_tidy {

#pragma mark - Helpers

  static StringRef twoLevelPath(StringRef Path) {
    auto Div = Path.rfind('/');
    Div = Path.rfind('/', Div);
    return Path.drop_front(Div + 1);
  }

  static StringRef strippedLibraryPath(StringRef Path) {
    auto Start = Path.rfind("/usr/include/") + strlen("/usr/include/");
    return Path.drop_front(Start);
  }

  static StringRef frameworkName(StringRef Path) {
    auto End = Path.rfind(".framework/Headers");
    auto Start = Path.rfind('/', End) + 1;
    return Path.substr(Start, End - Start);
  }

  static StringRef filename(StringRef Path) {
    return Path.drop_front(Path.rfind('/') + 1);
  }

  static bool isFramework(StringRef Path) {
    return Path.rfind(".framework/Headers/") != StringRef::npos;
  }

  static bool isSystemLibrary(StringRef Path) {
    return Path.rfind("/usr/include/") != StringRef::npos;
  }

  static StringRef moduleName(const FileID File, const SourceManager *SM) {
    return SM->getModuleImportLoc(SM->getLocForStartOfFile(File)).second;
  }

  static FileID
  topFileIncludingFile(FileID File, const SourceManager &SM) {
    if (!SM.isInSystemHeader(SM.getLocForStartOfFile(File)))
      return File;

    auto TopFile = File;
    auto *Dir = SM.getFileEntryForID(File)->getDir();
    while (SM.getIncludeLoc(TopFile).isValid()) {
      auto NextFile = SM.getFileID(SM.getIncludeLoc(TopFile));
      if (!SM.getFilename(SM.getLocForStartOfFile(NextFile)).endswith(".h"))
        break;

      auto *NextDir = SM.getFileEntryForID(NextFile)->getDir();
      if (NextDir != Dir)
        break;

      TopFile = NextFile;
    }
    return TopFile;
  }

  static ImportType
  forwardType(const Decl *D, const SourceManager &SM) {
    if (isa<ObjCProtocolDecl>(D)) {
      return ImportType::ForwardDeclareProtocol;
    } else {
      assert(isa<ObjCInterfaceDecl>(D));
      return ImportType::ForwardDeclareClass;
    }
  }

  static ImportType calculateType(FileID File, const SourceManager &SM) {
    if (SM.isLoadedFileID(File))
      return ImportType::Module;
    else if (SM.isInSystemHeader(SM.getLocForStartOfFile(File)))
      return ImportType::Library;
    else
      return ImportType::File;
  }

#pragma mark - Import

  Import::Import(const SourceManager &SM,
                 const Decl *D,
                 bool isForwardDeclare) :
    SM(&SM), ImportedDecl(D),
    File(topFileIncludingFile(SM.getFileID(D->getLocStart()), SM)),
    Type(isForwardDeclare ? forwardType(D, SM) : calculateType(File, SM)) {}

  bool Import::operator==(const Import &RHS) const {
    if (Type != RHS.Type)
      return false;

    switch (Type) {
      case ImportType::ForwardDeclareClass:
      case ImportType::ForwardDeclareProtocol:
        return ImportedDecl == RHS.ImportedDecl;
      case ImportType::Module:
        return moduleName(File, SM) == moduleName(RHS.File, RHS.SM);
      default:
        return File == RHS.File;
    }
  }

  bool Import::operator<(const Import &RHS) const {
    if (Type != RHS.Type)
      return Type < RHS.Type;

    if (File != RHS.File)
      return File < RHS.File;

    return ImportedDecl < RHS.ImportedDecl;
  }

#pragma mark - Friends

  llvm::raw_ostream& operator<<(llvm::raw_ostream &OS, const Import &Import) {
    auto Loc = Import.SM->getLocForStartOfFile(Import.File);
    auto Path = Import.SM->getFilename(Loc);

    switch (Import.Type) {
      case ImportType::Module:
        OS << "@import " << moduleName(Import.File, Import.SM) << ";";
        break;

      case ImportType::Library: {
        OS << "#import <";

        if (isFramework(Path)) {
          OS << frameworkName(Path) << "/" << filename(Path);
        } else if (isSystemLibrary(Path)) {
          OS << strippedLibraryPath(Path);
        } else {
          OS << twoLevelPath(Path);
        }

        OS << ">";
        break;
      }

      case ImportType::File:
        OS << "#import \"" << filename(Path) << "\"";
        break;

      case ImportType::ForwardDeclareClass: {
        auto *ID = dyn_cast<ObjCInterfaceDecl>(Import.ImportedDecl);
        OS << "@class " << ID->getName() << ";";
        break;
      }

      case ImportType::ForwardDeclareProtocol: {
        auto *PD = dyn_cast<ObjCProtocolDecl>(Import.ImportedDecl);
        OS << "@protocol " << PD->getName() << ";";
        break;
      }
    }
    return OS;
  }

  void sortedUniqueImports(std::vector<Import> &Imports) {
    std::sort(Imports.begin(), Imports.end());
    auto last = std::unique(Imports.begin(), Imports.end());
    Imports.erase(last, Imports.end());
  }
}
