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

    bool operator==(const Import &RHS) const;
    bool operator<(const Import &RHS) const;
    clang::FileID getFile() const { return File; }
    llvm::StringRef getName() const { return Name; }
    ImportType getType() const { return Type; }

  private:
    clang::FileID File;
    llvm::StringRef Name;
    ImportType Type;
  };

  llvm::raw_ostream& operator<<(llvm::raw_ostream&, const Import&);
  const std::vector<const Import*> sortedUniqueImports(const std::vector<Import>&);
  clang::SourceLocation getDeclLoc(const clang::Decl*);
}

#endif /* defined(__LLVM__Import__) */
