#ifndef __LLVM__Import__
#define __LLVM__Import__

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/Basic/SourceManager.h"
#include "clang/AST/Decl.h"

namespace import_tidy {
  enum class ImportType {
    Module,
    Library,
    File,
    ForwardDeclareClass,
    ForwardDeclareProtocol
  };

  class Import {
  public:
    Import(const clang::SourceManager&,
           const clang::Decl*,
           bool isForwardDeclare = false);

  private:
    friend llvm::raw_ostream& operator<<(llvm::raw_ostream&, const Import &);
    const clang::SourceManager &SM;
    const clang::Decl *ImportedDecl;
    const clang::FileID File;
    const ImportType Type;
  };

  llvm::raw_ostream& operator<<(llvm::raw_ostream&, const Import &);
}

#endif /* defined(__LLVM__Import__) */
