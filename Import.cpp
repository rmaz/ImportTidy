#include "Import.h"
#include "clang/AST/DeclObjC.h"
#include <set>
#include <algorithm>

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

  static StringRef moduleName(SourceLocation Loc, const SourceManager *SM) {
    return SM->getModuleImportLoc(Loc).second;
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

  static StringRef importName(ImportType Type,
                              const FileID File,
                              const Decl *D,
                              const SourceManager &SM) {
    switch (Type) {
      case ImportType::ForwardDeclareClass:
      case ImportType::ForwardDeclareProtocol:
        return dyn_cast<NamedDecl>(D)->getName();
      case ImportType::Module:
        return moduleName(getDeclLoc(D), &SM);
      case ImportType::File:
        return filename(SM.getFilename(SM.getLocForStartOfFile(File)));
      case ImportType::Library:
        return SM.getFilename(SM.getLocForStartOfFile(File));
    }
  }

  static bool isForwardDeclare(const Import &Import) {
    auto Type = Import.getType();
    return Type == ImportType::ForwardDeclareClass ||
           Type == ImportType::ForwardDeclareProtocol;
  }

#pragma mark - Import

  Import::Import(const SourceManager &SM,
                 const Decl *D,
                 bool isForwardDeclare) {
    File = topFileIncludingFile(SM.getFileID(getDeclLoc(D)), SM);
    Type = isForwardDeclare ? forwardType(D, SM) : calculateType(File, SM);
    Name = importName(Type, File, D, SM);
  }

  bool Import::operator==(const Import &RHS) const {
    return Type == RHS.Type && Name == RHS.Name;
  }

  bool Import::operator<(const Import &RHS) const {
    if (Type != RHS.Type)
      return Type < RHS.Type;
    return Name < RHS.Name;
  }

#pragma mark - Friends

  llvm::raw_ostream& operator<<(llvm::raw_ostream &OS, const Import &Import) {
    switch (Import.getType()) {
      case ImportType::Module:
        OS << "@import " << Import.getName() << ";";
        break;

      case ImportType::Library: {
        OS << "#import <";

        auto Path = Import.getName();
        if (isFramework(Path))
          OS << frameworkName(Path) << "/" << filename(Path);
        else if (isSystemLibrary(Path))
          OS << strippedLibraryPath(Path);
        else
          OS << twoLevelPath(Path);

        OS << ">";
        break;
      }

      case ImportType::File:
        OS << "#import \"" << Import.getName() << "\"";
        break;

      case ImportType::ForwardDeclareClass:
        OS << "@class " << Import.getName() << ";";
        break;

      case ImportType::ForwardDeclareProtocol:
        OS << "@protocol " << Import.getName() << ";";
        break;
    }
    return OS;
  }

  const std::vector<const Import*>
  sortedUniqueImports(const std::vector<Import> &Imports) {
    // get the set of imported files so we can remove unneeded forward declares
    std::set<const FileID> ImportedFiles;
    for (auto &I : Imports) {
      if (!isForwardDeclare(I))
        ImportedFiles.insert(I.getFile());
    }    

    std::vector<const Import*> SortedImports;
    for (auto &I : Imports) {
      if (!(isForwardDeclare(I) && ImportedFiles.count(I.getFile()) > 0))
        SortedImports.push_back(&I);
    }

    std::sort(SortedImports.begin(), SortedImports.end(),
              [](const Import *LHS, const Import *RHS) {
                return *LHS < *RHS;
              });
    auto End = std::unique(SortedImports.begin(), SortedImports.end(),
                           [](const Import *LHS, const Import *RHS) {
                             return *LHS == *RHS;
                           });
    SortedImports.erase(End, SortedImports.end());

    return SortedImports;
  }

  clang::SourceLocation getDeclLoc(const Decl *D) {
    return D->getLocation().isFileID() ? D->getLocation() : D->getLocStart();
  }
}
