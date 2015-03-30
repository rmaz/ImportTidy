#include "ImportMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/ASTMatchers/ASTMatchersInternal.h"
#include "clang/AST/ExprObjC.h"
#include <unordered_set>

using namespace clang;
using namespace clang::ast_matchers;
using namespace import_tidy;

namespace clang {
namespace ast_matchers {
  static const internal::VariadicDynCastAllOfMatcher<Stmt, ObjCMessageExpr> message;
  static const internal::VariadicDynCastAllOfMatcher<Decl, ObjCInterfaceDecl> interface;

  AST_MATCHER(ObjCInterfaceDecl, isImplementationInMainFile) {
    if (auto *ImpDecl = Node.getImplementation()) {
      auto &SourceManager = Finder->getASTContext().getSourceManager();
      return SourceManager.isInMainFile(SourceManager.getExpansionLoc(ImpDecl->getLocStart()));
    } else {
      return false;
    }
  }
} // end namespace ast_matchers
} // end namespace clang

namespace import_tidy {
  static const StringRef nodeKey = "key";

  void CallExprCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *E = Result.Nodes.getNodeAs<CallExpr>(nodeKey)) {
      auto *FD = E->getDirectCallee();
      auto &SM = *Result.SourceManager;
      matcher.addImportFile(FD->getLocation().printToString(SM), true);
    }
  }

  void InterfaceCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *ID = Result.Nodes.getNodeAs<ObjCInterfaceDecl>(nodeKey)) {
      auto &SM = *Result.SourceManager;
      auto inMainFile = SM.isInMainFile(ID->getLocation());

      if (auto *SC = ID->getSuperClass()) {
        matcher.addImportFile(SC->getLocation().printToString(SM), inMainFile);
      }

      if (!inMainFile) {
        matcher.addImportFile(ID->getLocation().printToString(SM), true);
      }

      for (auto P = ID->protocol_begin(); P != ID->protocol_end(); P++) {
        matcher.addImportFile((*P)->getLocation().printToString(SM), inMainFile);
      }
    }
  }

  void MessageExprCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *E = Result.Nodes.getNodeAs<ObjCMessageExpr>(nodeKey)) {
      auto *ID = E->getReceiverInterface();
      auto Name = ID->getNameAsString();
      if (classNames.count(Name) == 0) {
        classNames.insert(Name);
        auto &SM = *Result.SourceManager;
        matcher.addImportFile(ID->getLocation().printToString(SM), true);
      }
    }
  }

  void ImportMatcher::registerMatchers(MatchFinder &Finder) {
    auto CallMatcher = callExpr(isExpansionInMainFile()).bind(nodeKey);
    auto InterfaceMatcher = interface(isImplementationInMainFile()).bind(nodeKey);
    auto MsgMatcher = message(isExpansionInMainFile()).bind(nodeKey);
    Finder.addMatcher(CallMatcher, &callCallback);
    Finder.addMatcher(InterfaceMatcher, &interfaceCallback);
    Finder.addMatcher(MsgMatcher, &msgCallback);
  }

  void ImportMatcher::addImportFile(std::string Path, bool InImplementation) {
      if (InImplementation) impImports.insert(Path);
      else headerImports.insert(Path);
  }

  void ImportMatcher::dumpImports(raw_ostream& out) {
    out << "Header Imports" << '\n';
    out << "==============" << '\n';

    for (auto i = headerImports.begin(); i != headerImports.end(); i++) {
      out << *i << '\n';
    }

    out << '\n';
    out << "Implementation Imports" << '\n';
    out << "======================" << '\n';

    for (auto i = impImports.begin(); i != impImports.end(); i++) {
      out << *i << '\n';
    }
  }

} // end namespace import_tidy
