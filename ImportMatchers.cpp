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

namespace import_tidy {
  static const StringRef nodeKey = "key";

  void CallExprCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *FD = Result.Nodes.getNodeAs<CallExpr>(nodeKey)->getDirectCallee()) {
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
      auto &SM = *Result.SourceManager;

      if (auto *ID = E->getReceiverInterface()) {
        matcher.addImportFile(ID->getLocation().printToString(SM), true);
      } else if (auto *Ptr = E->getReceiverType()->getAs<ObjCObjectPointerType>()) {
        assert(Ptr->isObjCQualifiedIdType());
        for (auto i = Ptr->qual_begin(); i != Ptr->qual_end(); i++) {
          matcher.addImportFile((*i)->getLocation().printToString(SM), true);
        }
      } else {
        llvm::outs() << "failed to unpack expr of kind " << E->getReceiverKind();
        assert(0);
      }
    }
  }

  void MethodCallback::run(const MatchFinder::MatchResult &Result) {
    if (auto *M = Result.Nodes.getNodeAs<ObjCMethodDecl>(nodeKey)) {
      addType(M->getReturnType());

      for (auto i = M->param_begin(); i != M->param_end(); i++) {
        addType((*i)->getType());
      }
    }
  }

  void MethodCallback::addType(QualType T) {
    if (auto *PT = T->getAs<ObjCObjectPointerType>()) {
      if (auto *ID = PT->getInterfaceDecl()) {
        matcher.addHeaderForwardDeclare(ID->getName());
      }
    }
  }

  void ImportMatcher::registerMatchers(MatchFinder &Finder) {
    auto CallMatcher = callExpr(isExpansionInMainFile()).bind(nodeKey);
    auto InterfaceMatcher = interface(isImplementationInMainFile()).bind(nodeKey);
    auto MsgMatcher = message(isExpansionInMainFile()).bind(nodeKey);
    auto MtdMatcher = method(isDefinedInHeader()).bind(nodeKey);

    Finder.addMatcher(CallMatcher, &callCallback);
    Finder.addMatcher(InterfaceMatcher, &interfaceCallback);
    Finder.addMatcher(MsgMatcher, &msgCallback);
    Finder.addMatcher(MtdMatcher, &mtdCallback);
  }

  void ImportMatcher::addHeaderForwardDeclare(llvm::StringRef Name) {
    headerClasses.insert(Name.str());
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
    out << "Foward Declares" << '\n';
    out << "===============" << '\n';

    for (auto i = headerClasses.begin(); i != headerClasses.end(); i++) {
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
