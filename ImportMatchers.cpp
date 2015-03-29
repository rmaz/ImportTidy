#include "ImportMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/AST/ExprObjC.h"
#include <unordered_set>

using namespace clang;
using namespace clang::ast_matchers;
using namespace import_tidy;

namespace import_tidy {
static const internal::VariadicDynCastAllOfMatcher<Stmt, ObjCMessageExpr> message;
static const StringRef nodeKey = "key";

void MessageExprCallback::run(const MatchFinder::MatchResult &Result) {
  if (const ObjCMessageExpr *E = Result.Nodes.getNodeAs<ObjCMessageExpr>(nodeKey)) {
    const ObjCInterfaceDecl *ID = E->getReceiverInterface();
    std::string Name = ID->getNameAsString();
    if (class_names.count(Name) == 0) {
      class_names.insert(Name);
      imports.insert(ID->getLocation().printToString(*Result.SourceManager));
    }
  }
}

void CallExprCallback::run(const MatchFinder::MatchResult &Result) {
  if (const CallExpr *E = Result.Nodes.getNodeAs<CallExpr>(nodeKey)) {
    const FunctionDecl *FD = E->getDirectCallee();
    std::string Name = FD->getNameAsString();
    if (function_names.count(Name) == 0) {
      function_names.insert(Name);
      imports.insert(FD->getLocation().printToString(*Result.SourceManager));
    }
  }
}

void ImportMatcher::registerMatchers(clang::ast_matchers::MatchFinder &Finder) {
  auto MsgMatcher = message(isExpansionInMainFile()).bind(nodeKey);
  auto CallMatcher = callExpr(isExpansionInMainFile()).bind(nodeKey);
  Finder.addMatcher(MsgMatcher, &msgCallback);
  Finder.addMatcher(CallMatcher, &callCallback);
}

std::vector<std::string> ImportMatcher::collectImports() {
  std::vector<std::string> outports;
  std::copy(imports.begin(), imports.end(), back_inserter(outports));

  return outports;
}

}
