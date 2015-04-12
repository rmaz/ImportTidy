#ifndef __LLVM__Import__
#define __LLVM__Import__

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/Basic/SourceManager.h"

namespace import_tidy {
  enum class ImportType {
    Module,
    Library,
    File,
    ForwardDeclare
  };

  class Import {
  public:
    Import(ImportType Type, llvm::StringRef Name) :
      Type(Type), Name(Name) { };

    bool operator==(const Import &RHS) const {
      return Type == RHS.Type && Name == RHS.Name;
    }

    bool operator<(const Import &RHS) const {
      if (Type != RHS.Type) return Type < RHS.Type;
      return Name < RHS.Name;
    }

    ImportType getType() const { return Type; }
    llvm::StringRef getName() const { return Name; }

  private:
    ImportType Type;
    llvm::StringRef Name;
  };

  llvm::raw_ostream& operator<<(llvm::raw_ostream&, const Import &);
}

#endif /* defined(__LLVM__Import__) */
