#include "ImportMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/AST/ExprObjC.h"
#include <unordered_set>

using namespace clang;
using namespace clang::ast_matchers;

namespace {
  static const internal::VariadicDynCastAllOfMatcher<Stmt, ObjCMessageExpr> message;
  static const StringRef nodeKey = "key";

  class MessageExprCallback : public MatchFinder::MatchCallback {
  public:
    void run(const MatchFinder::MatchResult &Result) override {
      if (const ObjCMessageExpr *E = Result.Nodes.getNodeAs<ObjCMessageExpr>(nodeKey)) {
        const ObjCInterfaceDecl *ID = E->getReceiverInterface();
        std::string Name = ID->getNameAsString();
        if (class_names.count(Name) == 0) {
          class_names.insert(Name);
          imports.insert(ID->getLocation().printToString(*Result.SourceManager));
        }
      }
    }

    std::unordered_set<std::string> &getImports() { return imports; }
  private:
    std::unordered_set<std::string> imports, class_names;
  };

  class CallExprCallback : public MatchFinder::MatchCallback {
  public:
    void run(const MatchFinder::MatchResult &Result) override {
      if (const CallExpr *E = Result.Nodes.getNodeAs<CallExpr>(nodeKey)) {
        const FunctionDecl *FD = E->getDirectCallee();
        std::string Name = FD->getNameAsString();
        if (function_names.count(Name) == 0) {
          function_names.insert(Name);
          imports.insert(FD->getLocation().printToString(*Result.SourceManager));
        }
      }
    }

    std::unordered_set<std::string> &getImports() { return imports; }
  private:
    std::unordered_set<std::string> imports, function_names;
  };
  
  MessageExprCallback MsgCallback;
  CallExprCallback CallCallback;
}

void registerMatchers(clang::ast_matchers::MatchFinder &Finder) {
  auto MsgMatcher = message(isExpansionInMainFile()).bind(nodeKey);
  auto CallMatcher = callExpr(isExpansionInMainFile()).bind(nodeKey);
  Finder.addMatcher(MsgMatcher, &MsgCallback);
  Finder.addMatcher(CallMatcher, &CallCallback);
}

std::vector<std::string> collectImports() {
  std::vector<std::string> imports;

  auto &message_imports = MsgCallback.getImports();
  auto &call_imports = CallCallback.getImports();
  std::copy(message_imports.begin(), message_imports.end(), back_inserter(imports));
  std::copy(call_imports.begin(), call_imports.end(), back_inserter(imports));

  return imports;
}

