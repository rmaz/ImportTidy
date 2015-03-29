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
      auto Name = FD->getNameAsString();
      if (function_names.count(Name) == 0) {
        function_names.insert(Name);
        imports.insert(FD->getLocation().printToString(*Result.SourceManager));
      }
    }
  }

  void InterfaceCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *ID = Result.Nodes.getNodeAs<ObjCInterfaceDecl>(nodeKey)) {
      imports.insert(ID->getLocation().printToString(*Result.SourceManager));

      for (auto P = ID->protocol_begin(); P != ID->protocol_end(); P++) {
        imports.insert((*P)->getLocation().printToString(*Result.SourceManager));
      }
    }
  }

  void MessageExprCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *E = Result.Nodes.getNodeAs<ObjCMessageExpr>(nodeKey)) {
      auto *ID = E->getReceiverInterface();
      auto Name = ID->getNameAsString();
      if (class_names.count(Name) == 0) {
        class_names.insert(Name);
        imports.insert(ID->getLocation().printToString(*Result.SourceManager));
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

  std::vector<std::string> ImportMatcher::collectImports() {
    std::vector<std::string> outports;
    std::copy(imports.begin(), imports.end(), back_inserter(outports));

    return outports;
  }
} // end namespace import_tidy
