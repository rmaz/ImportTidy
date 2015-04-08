#include "Import.h"

using namespace llvm;

namespace import_tidy {

  static StringRef twoLevelPath(StringRef Path) {
    auto Div = Path.find_last_of('/');
    Div = Path.rfind('/', Div);
    return Path.drop_front(Div + 1);
  }

  static StringRef strippedLibraryPath(StringRef Path) {
    return Path.drop_front(Path.find_last_of("/usr/include/") + strlen("/usr/include/"));
  }

  static StringRef frameworkName(StringRef Path) {
    auto End = Path.rfind(".framework/Headers");
    auto Start = Path.rfind('/', End) + 1;
    return Path.substr(Start, End - Start);
  }

  static StringRef filename(StringRef Path) {
    return Path.drop_front(Path.find_last_of('/') + 1);
  }

  static bool isFramework(StringRef Path) {
    return Path.rfind(".framework/Headers/") != StringRef::npos;
  }

  static bool isSystemLibrary(StringRef Path) {
    return Path.rfind("/usr/include/") != StringRef::npos;
  }

  llvm::raw_ostream& operator<<(llvm::raw_ostream &OS, const Import &Import) {
    switch (Import.getType()) {
      case ImportType::Module:
        OS << "@import " << Import.getName() << ";";
        break;

      case ImportType::Library: {
        auto Path = Import.getName();
        OS << "#import <";

        if (isFramework(Path)) {
          OS << frameworkName(Path) << "/" << filename(Path);
        } else if (isSystemLibrary(Path)) {
          OS << strippedLibraryPath(Path);
        } else {
          OS << twoLevelPath(Path);
        }

        OS << ">";
        break;
      }

      case ImportType::File:
        OS << "#import \"" << Import.getName() << "\"";
        break;

      case ImportType::ForwardDeclare:
        OS << "@class " << Import.getName() << ";";
        break;
    }
    return OS;
  }
}
