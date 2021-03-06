#include "ImportMatcher.h"
#include "clang/Basic/SourceManager.h"
#include "clang/ASTMatchers/ASTMatchersInternal.h"
#include "clang/AST/ExprObjC.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace import_tidy;

namespace clang {
namespace ast_matchers {
  static const internal::VariadicDynCastAllOfMatcher<Stmt, ObjCMessageExpr> messageExpr;
  static const internal::VariadicDynCastAllOfMatcher<Decl, ObjCInterfaceDecl> interfaceDecl;
  static const internal::VariadicDynCastAllOfMatcher<Decl, ObjCContainerDecl> containerDecl;
  static const internal::VariadicDynCastAllOfMatcher<Decl, ObjCCategoryDecl> objcCategoryDecl;
  static const internal::VariadicDynCastAllOfMatcher<Decl, ObjCMethodDecl> objcMethodDecl;
  static const internal::VariadicDynCastAllOfMatcher<Decl, ObjCProtocolDecl> protocolDecl;
  static const internal::VariadicDynCastAllOfMatcher<Stmt, ObjCProtocolExpr> protocolExpr;
  static const internal::VariadicDynCastAllOfMatcher<Decl, ImportDecl> importDecl;

  AST_POLYMORPHIC_MATCHER(isImplementationInMainFile,
                          AST_POLYMORPHIC_SUPPORTED_TYPES_2(ObjCInterfaceDecl,
                                                            ObjCCategoryDecl)) {
    if (auto *ImpDecl = Node.getImplementation()) {
      auto &SourceManager = Finder->getASTContext().getSourceManager();
      return SourceManager.isInMainFile(SourceManager.getExpansionLoc(ImpDecl->getLocStart()));
    } else {
      return false;
    }
  }

  AST_MATCHER(ObjCInterfaceDecl, isADefinition) {
    return Node.isThisDeclarationADefinition();
  }

  AST_MATCHER(FunctionDecl, hasPrototype) {
    return Node.hasPrototype();
  }

  AST_POLYMORPHIC_MATCHER(isNotInSystemHeader,
                          AST_POLYMORPHIC_SUPPORTED_TYPES_2(Decl, Stmt)) {
    auto Loc = Node.getLocation();
    if (!Loc.isValid())
      return false;

    auto &SM = Finder->getASTContext().getSourceManager();
    return !SM.isInSystemHeader(Loc);
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

  unsigned RangeEnd(const Range &R) {
    return R.getOffset() + R.getLength();
  }

  static std::vector<Range> collapsedRanges(const std::vector<Range> &Ranges) {
    std::vector<Range> sorted(Ranges.begin(), Ranges.end());
    if (Ranges.size() == 0)
      return sorted;

    std::sort(sorted.begin(), sorted.end(), [](Range &lhs, Range &rhs) {
      return lhs.getOffset() < rhs.getOffset();
    });

    auto I = sorted.begin();
    while (I < sorted.end() - 1) {
      if (RangeEnd(*I) >= (*(I + 1)).getOffset()) {
        *I = Range((*I).getOffset(), RangeEnd(*(I+1)) - (*I).getOffset());
        sorted.erase(I + 1);
      } else {
        I++;
      }
    }

    return sorted;
  }

} // end anonymous namespace

namespace import_tidy {

#pragma mark - ImportMatcher

  std::unique_ptr<FrontendActionFactory>
  ImportMatcher::getActionFactory(MatchFinder& Finder) {
    auto CallMatcher = callExpr(isExpansionInMainFile()).bind(nodeKey);
    auto CastMatcher = cStyleCastExpr(isExpansionInMainFile()).bind(nodeKey);
    auto CategoryMatcher = objcCategoryDecl(isImplementationInMainFile()).bind(nodeKey);
    auto DeclRefMatcher = declRefExpr(isNotInSystemHeader()).bind(nodeKey);
    auto InterfaceMatcher = interfaceDecl(isADefinition(),
                                          isImplementationInMainFile()).bind(nodeKey);
    auto ForwardDeclareMatcher = interfaceDecl(isNotInSystemHeader(),
                                               unless(isADefinition())).bind(nodeKey);
    auto ImportMatcher = importDecl(isNotInSystemHeader()).bind(nodeKey);
    auto MsgMatcher = messageExpr(isExpansionInMainFile()).bind(nodeKey);
    auto MtdMatcher = containerDecl(isNotInSystemHeader(),
                                    forEachDescendant(objcMethodDecl().bind(nodeKey)));
    auto ProtoDeclMatcher = protocolDecl(isNotInSystemHeader()).bind(nodeKey);
    auto ProtoExprMatcher = protocolExpr(isExpansionInMainFile()).bind(nodeKey);
    auto FuncDecl = functionDecl(isDefinition(), hasPrototype(),
                                 isNotInSystemHeader()).bind(nodeKey);

    Finder.addMatcher(CallMatcher, &CallCallback);
    Finder.addMatcher(CastMatcher, &CastCallback);
    Finder.addMatcher(CategoryMatcher, &CategoryCallback);
    Finder.addMatcher(DeclRefMatcher, &DeclRefCallback);
    Finder.addMatcher(InterfaceMatcher, &InterfaceCallback);
    Finder.addMatcher(ForwardDeclareMatcher, &StripCallback);
    Finder.addMatcher(ImportMatcher, &StripCallback);
    Finder.addMatcher(MsgMatcher, &MsgCallback);
    Finder.addMatcher(MtdMatcher, &MtdCallback);
    Finder.addMatcher(ProtoExprMatcher, &ProtoCallback);
    Finder.addMatcher(ProtoDeclMatcher, &ProtoCallback);
    Finder.addMatcher(FuncDecl, &FuncDeclCallback);

    return newFrontendActionFactory(&Finder, &FileCallbacks);
  }

  void ImportMatcher::addImport(const FileID InFile,
                                const Decl *D,
                                const SourceManager &SM,
                                bool isForwardDeclare) {
    // only allow file locations
    auto Loc = getDeclLoc(D);
    if (!SM.getFileEntryForID(InFile) || SM.getFilename(Loc).size() == 0)
      return;

    // don't include files in themselves
    if (!isForwardDeclare && SM.getFileID(Loc) == InFile)
      return;

    ImportMap[InFile].push_back(Import(SM, D, isForwardDeclare));
  }

  void ImportMatcher::removeImport(const SourceLocation Loc, const SourceManager &SM) {
    auto fid = SM.getFileID(Loc);
    auto *buffer = SM.getBuffer(fid);
    auto *fileStart = buffer->getBufferStart();
    unsigned start = SM.getFileOffset(Loc);
    unsigned length = strchr(fileStart + start, '\n') - fileStart - start + 1;

    // strip any empty lines after this import
    auto *c = fileStart + start + length;
    while (isWhitespace(*c) && c++ < buffer->getBufferEnd())
      length++;

    ImportRanges[fid].push_back(Range(start, length));
  }

  // TODO: move this into ImportCallbacks as a helper function
  void ImportMatcher::addType(const FileID InFile, QualType T, const SourceManager &SM) {
    bool isMainFile = SM.getMainFileID() == InFile;

    if (auto *PT = T->getAs<ObjCObjectPointerType>()) {
      // import or forward declare the class type
      if (auto *ID = PT->getInterfaceDecl()) {
        auto Filename = SM.getFilename(ID->getLocation());
        bool isSystemDecl = Filename.startswith(getSysroot());
        addImport(InFile, ID, SM, !isSystemDecl && !isMainFile);
      }

      // import or forward declare any protocols being conformed to
      for (auto i = PT->qual_begin(); i != PT->qual_end(); i++) {
        auto Filename = SM.getFilename((*i)->getLocation());
        bool isSystemDecl = Filename.startswith(getSysroot());
        addImport(InFile, *i, SM, !isSystemDecl && !isMainFile);
      }
    } else if (auto *TD = T->getAs<TypedefType>()) {
      // any typedefs need to be imported
      if (auto *TypeDecl = TD->getDecl()) {
        addImport(InFile, TypeDecl, SM);
      }
    } else if (auto *BP = T->getAs<BlockPointerType>()) {
      // any types used in blocks need to be imported
      if (auto *FT = BP->getPointeeType()->getAs<FunctionProtoType>()) {
        addType(InFile, FT->getReturnType(), SM);
        for (auto PT : FT->param_types()) {
          addType(InFile, PT, SM);
        }
      }
    }
  }

  void ImportMatcher::addHeaderFile(const FileID FID) {
    HeaderFiles.insert(FID);
  }

  void ImportMatcher::flush(const SourceManager &SM) {
    auto &out = llvm::outs();
    auto HeaderImports = headerImportedFiles(SM);
    std::set<FileID> EmptyImports;
    HeaderFiles.insert(SM.getMainFileID());

    for (auto &Pair : ImportMap) {
      // only tidy the main file and files with interfaces
      // implemented in the main file
      if (HeaderFiles.count(Pair.first) == 0)
        continue;

      std::string import;
      llvm::raw_string_ostream ImportStr(import);
      auto &Excluded = SM.getMainFileID() == Pair.first ? HeaderImports : EmptyImports;
      auto Imports = sortedUniqueImports(SM, Pair.second, Excluded);
      for (auto *Import : Imports) {
        ImportStr << *Import << '\n';
        if (Import->getType() == ImportType::Library) {
          LibraryCounts[Import->getName()]++;
        }
      }
      ImportStr << '\n';

      auto Fid = Pair.first;
      auto StartLoc = SM.getLocForStartOfFile(Fid);
      auto Path = SM.getFilename(StartLoc);
      if (haveReplacementForFile(Replacements, Path))
        continue;

      auto ReplacementRanges = collapsedRanges(ImportRanges[Fid]);
      if (ReplacementRanges.size() > 0) {
        for (auto I = ReplacementRanges.cbegin(); I != ReplacementRanges.cend(); I++) {
          auto Text = I == ReplacementRanges.cbegin() ? ImportStr.str() : "";
          auto Start = StartLoc.getLocWithOffset(I->getOffset());
          Replacements.insert(Replacement(SM, Start, I->getLength(), Text));
        }
      } else {
        Replacements.insert(Replacement(SM, StartLoc, 0, ImportStr.str()));
      }

      out << "File: " << SM.getFilename(StartLoc) << "\n";
      out << ImportStr.str() << "\n";
    }
    ImportMap.clear();
    ImportRanges.clear();
    HeaderFiles.clear();
  }

  void ImportMatcher::printLibraryCounts(llvm::raw_ostream &OS) {
    if (LibraryCounts.size() == 0)
      return;

    using ImpPair = std::pair<StringRef, unsigned>;
    std::vector<ImpPair> counts(LibraryCounts.begin(), LibraryCounts.end());
    std::sort(counts.begin(), counts.end(), [](const ImpPair &L, const ImpPair &R) {
      return L.second < R.second;
    });

    OS << "\n\n";
    OS << "--------------------------------" << "\n";
    OS << "Libraries sorted by import count" << "\n";
    OS << "--------------------------------" << "\n";
    const unsigned kThreshold = 5;
    for (auto I = counts.rbegin(); I != counts.rend() && I->second >= kThreshold; I++) {
      OS << I->first << " : " << I->second << " times\n";
    }
  }

  std::set<FileID> ImportMatcher::headerImportedFiles(const SourceManager &SM) {
    std::set<FileID> AllFiles;

    for (auto &Pair : ImportMap) {
      if (Pair.first == SM.getMainFileID() ||
          HeaderFiles.count(Pair.first) == 0)
        continue;

      for (auto &Import : Pair.second)
        if (!Import.isForwardDeclare())
          AllFiles.insert(Import.getFile());
    }
    return AllFiles;
  }


} // end namespace import_tidy
