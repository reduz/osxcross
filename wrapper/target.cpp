/***********************************************************************
 *  OSXCross Compiler Wrapper                                          *
 *  Copyright (C) 2014, 2015 by Thomas Poechtrager                     *
 *  t.poechtrager@gmail.com                                            *
 *                                                                     *
 *  This program is free software; you can redistribute it and/or      *
 *  modify it under the terms of the GNU General Public License        *
 *  as published by the Free Software Foundation; either version 2     *
 *  of the License, or (at your option) any later version.             *
 *                                                                     *
 *  This program is distributed in the hope that it will be useful,    *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of     *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the      *
 *  GNU General Public License for more details.                       *
 *                                                                     *
 *  You should have received a copy of the GNU General Public License  *
 *  along with this program; if not, write to the Free Software        *
 *  Foundation, Inc.,                                                  *
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.      *
 ***********************************************************************/

#include "compat.h"

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>
#include <cstring>
#include <strings.h>
#include <cstdlib>
#include <climits>
#include <cassert>

#include "tools.h"
#include "target.h"

namespace target {

OSVersion Target::getSDKOSNum() const {
  if (SDK) {
    std::string SDKPath = SDK;

    while (SDKPath.size() && SDKPath[SDKPath.size() - 1] == PATHDIV)
      SDKPath.erase(SDKPath.size() - 1, 1);

    const char *SDKName = getFileName(SDKPath);

    if (strncasecmp(SDKName, "MacOSX", 6))
      return OSVersion();

    return parseOSVersion(SDKName + 6);
  } else {
    if (target.size() < 7)
      return OSVersion();

    int n = atoi(target.c_str() + 6);
    return OSVersion(10, 4 + (n - 8));
  }
}

bool Target::getSDKPath(std::string &path) const {
  if (SDK) {
    path = SDK;
  } else {
    OSVersion SDKVer = getSDKOSNum();

    path = execpath;
    path += "/../SDK/MacOSX";
    path += SDKVer.shortStr();

    if (SDKVer <= OSVersion(10, 4))
      path += "u";

    path += ".sdk";
  }

  if (!dirExists(path)) {
    err << "cannot find Mac OS X SDK (expected in: " << path << ")"
        << err.endl();
    return false;
  }

  return true;
}

bool Target::getMacPortsDir(std::string &path) const {
  path = execpath;
  path += "/../macports";
  return dirExists(path);
}

bool Target::getMacPortsSysRootDir(std::string &path) const {
  if (!getMacPortsDir(path))
    return false;

  path += "/pkgs";
  return dirExists(path);
}

bool Target::getMacPortsPkgConfigDir(std::string &path) const {
  if (!getMacPortsDir(path))
    return false;

  path += "/pkgs/opt/local/lib/pkgconfig";
  return dirExists(path);
}

bool Target::getMacPortsIncludeDir(std::string &path) const {
  if (!getMacPortsDir(path))
    return false;

  path += "/pkgs/opt/local/include";
  return dirExists(path);
}

bool Target::getMacPortsLibDir(std::string &path) const {
  if (!getMacPortsDir(path))
    return false;

  path += "/pkgs/opt/local/lib";
  return dirExists(path);
}

bool Target::getMacPortsFrameworksDir(std::string &path) const {
  if (!getMacPortsDir(path))
    return false;

  path += "/pkgs/opt/local/Library/Frameworks";
  return dirExists(path);
}

void Target::addArch(const Arch arch) {
  auto &v = targetarch;
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] == arch) {
      v.erase(v.begin() + i);
      addArch(arch);
      return;
    }
  }
  v.push_back(arch);
}

bool Target::haveArch(const Arch arch) {
  for (auto a : targetarch) {
    if (arch == a)
      return true;
  }
  return false;
}

bool Target::hasLibCXX() const { return getSDKOSNum() >= OSVersion(10, 7); }

bool Target::libCXXIsDefaultCXXLib() const {
  OSVersion OSNum = this->OSNum;

  if (!OSNum.Num())
    OSNum = getSDKOSNum();

  return stdlib != libstdcxx && hasLibCXX() && !isGCC() &&
         OSNum >= OSVersion(10, 9);
}

bool Target::isLibCXX() const {
  return stdlib == StdLib::libcxx || libCXXIsDefaultCXXLib();
}

bool Target::isLibSTDCXX() const { return stdlib == StdLib::libstdcxx; }

bool Target::isCXX() {
  return endsWith(compilername, "++");
}

bool Target::isGCH() {
  if (!language)
    return false;

  return !strcmp(language, "c-header") ||
         !strcmp(language, "c++-header") ||
         !strcmp(language, "objective-c-header") ||
         !strcmp(language, "objective-c++-header");
}


bool Target::isClang() const {
  return !strncmp(getFileName(compilername.c_str()), "clang", 5);
}

bool Target::isGCC() const {
  const char *c = getFileName(compilername.c_str());
  return (!strncmp(c, "gcc", 3) || !strncmp(c, "g++", 3));
}


const std::string &Target::getDefaultTriple(std::string &triple) const {
  triple = getArchName(Arch::x86_64);
  triple += "-";
  triple += getDefaultVendor();
  triple += "-";
  triple += getDefaultTarget();
  return triple;
}

void Target::setCompilerPath() {
  if (isGCC()) {
    compilerpath = execpath;
    compilerpath += "/";
    compilerpath += getTriple();
    compilerpath += "-";
    compilerpath += "base-";
    compilerpath += compilername;

    compilerexecname = getTriple();
    compilerexecname += "-";
    compilerexecname += compilername;
  } else {
    if (!realPath(compilername.c_str(), compilerpath, ignoreCCACHE))
      compilerpath = compilername;

    compilerexecname += compilername;
  }
}

bool Target::findClangIntrinsicHeaders(std::string &path) {
  static std::stringstream dir;

  assert(isClang());

  if (compilerpath.empty())
    return false;

  std::string clangbindir = compilerpath;
  stripFileName(clangbindir);

  static ClangVersion *clangversion;
  static std::string pathtmp;

  clangversion = &this->clangversion;

  clear(dir);
  *clangversion = ClangVersion();
  pathtmp.clear();

  auto tryDir = [&]()->bool {
    listFiles(dir.str().c_str(), nullptr, [](const char *file) {
      if (file[0] != '.' && isDirectory(file, dir.str().c_str())) {
        ClangVersion cv = parseClangVersion(file);

        if (cv != ClangVersion()) {
          static std::stringstream tmp;
          clear(tmp);

          auto checkDir = [&](std::stringstream &dir) {
            static std::string intrindir;
            auto &file = dir;

            intrindir = dir.str();
            file << "/xmmintrin.h";

            if (fileExists(file.str())) {
              if (cv > *clangversion) {
                *clangversion = cv;
                pathtmp.swap(intrindir);
              }
              return true;
            }

            return false;
          };

          tmp << dir.str() << "/" << file << "/include";

          if (!checkDir(tmp)) {
            clear(tmp);
            tmp << dir.str() << "/" << file;
            checkDir(tmp);
          }
        }
        return true;
      }
      return true;
    });
    return *clangversion != ClangVersion();
  };

#define TRYDIR(basedir, subdir)                                                \
do {                                                                           \
  dir << basedir << subdir;                                                    \
  if (tryDir()) {                                                              \
    path.swap(pathtmp);                                                        \
    return true;                                                               \
  }                                                                            \
  clear(dir);                                                                  \
} while (0)

#define TRYDIR2(libdir) TRYDIR(clangbindir, libdir)
#define TRYDIR3(libdir) TRYDIR(std::string(), libdir)

#ifdef __CYGWIN__
#ifdef __x86_64__
  TRYDIR2("/../lib/clang/x86_64-pc-cygwin");
#else
  TRYDIR2("/../lib/clang/i686-pc-cygwin");
#endif
#endif

  TRYDIR2("/../lib/clang");

#ifdef __linux__
#ifdef __x86_64__
  // opensuse uses lib64 instead of lib on x86_64
  TRYDIR2("/../lib64/clang");
#elif __i386__
  TRYDIR2("/../lib32/clang");
#endif
#endif

#ifdef __APPLE__
  constexpr const char *OSXIntrinDirs[] = {
    "/Library/Developer/CommandLineTools/usr/lib/clang",
    "/Applications/Contents/Developer/Toolchains/"
    "XcodeDefault.xctoolchain/usr/lib/clang"
  };

  for (auto intrindir : OSXIntrinDirs)
    TRYDIR3(intrindir);
#endif

  TRYDIR2("/../include/clang");
  TRYDIR2("/usr/include/clang");

  return false;
#undef TRYDIR
#undef TRYDIR2
#undef TRYDIR3
}

void Target::setupGCCLibs(Arch arch) {
  assert(stdlib == StdLib::libstdcxx);
  fargs.push_back("-nodefaultlibs");

  std::string SDKPath;
  std::stringstream GCCLibSTDCXXPath;
  std::stringstream GCCLibPath;

  const bool dynamic = !!getenv("OSXCROSS_GCC_NO_STATIC_RUNTIME");

  getSDKPath(SDKPath);

  GCCLibPath << SDKPath << "/../../lib/gcc/" << otriple << "/"
             << gccversion.Str();

  GCCLibSTDCXXPath << SDKPath << "/../../" << otriple << "/lib";

  switch (arch) {
  case Arch::i386:
  case Arch::i486:
  case Arch::i586:
  case Arch::i686:
    GCCLibPath << "/" << getArchName(Arch::i386);
    GCCLibSTDCXXPath << "/" << getArchName(i386);
  default:
    ;
  }

  if (dynamic) {
    fargs.push_back("-L");
    fargs.push_back(GCCLibPath.str());
    fargs.push_back("-L");
    fargs.push_back(GCCLibSTDCXXPath.str());
  }

  auto addLib = [&](const std::stringstream &path, const char *lib) {
    if (dynamic) {
      fargs.push_back("-l");
      fargs.push_back(lib);
    } else {
      static std::stringstream tmp;
      clear(tmp);
      tmp << path.str() << "/lib" << lib << ".a";
      fargs.push_back(tmp.str());
    }
  };

  fargs.push_back("-Qunused-arguments");

  addLib(GCCLibSTDCXXPath, "stdc++");
  addLib(GCCLibSTDCXXPath, "supc++");
  addLib(GCCLibPath, "gcc");
  addLib(GCCLibPath, "gcc_eh");

  fargs.push_back("-lc");
  fargs.push_back("-Wl,-no_compact_unwind");
}

bool Target::setup() {
  std::string SDKPath;
  OSVersion SDKOSNum = getSDKOSNum();

  if (!isKnownCompiler())
    warn << "unknown compiler '" << compilername << "'" << warn.endl();

  if (!getSDKPath(SDKPath))
    return false;

  if (targetarch.empty())
    targetarch.push_back(arch);

  triple = getArchName(arch);
  triple += "-";
  triple += vendor;
  triple += "-";
  triple += target;

  otriple = getArchName(Arch::x86_64);
  otriple += "-";
  otriple += vendor;
  otriple += "-";
  otriple += target;

  setCompilerPath();

  if (!OSNum.Num()) {
    if (haveArch(Arch::x86_64h)) {
      OSNum = OSVersion(10, 8); // Default to 10.8 for x86_64h
      if (SDKOSNum < OSNum) {
        err << "'" << getArchName(arch) << "' requires Mac OS X SDK "
            << OSNum.shortStr() << " (or later)" << err.endl();
        return false;
      }
    } else if (stdlib == StdLib::libcxx) {
      OSNum = OSVersion(10, 7); // Default to 10.7 for libc++
    } else {
      OSNum = getDefaultMinTarget();
    }
  }

  if (OSNum > SDKOSNum) {
    err << "targeted OS X version must be <= " << SDKOSNum.Str() << " (SDK)"
        << err.endl();
    return false;
  } else if (OSNum < OSVersion(10, 4)) {
    err << "targeted OS X version must be >= 10.4" << err.endl();
    return false;
  }

  if (haveArch(Arch::x86_64h) && OSNum < OSVersion(10, 8)) {
    // -mmacosx-version-min= < 10.8 in combination with '-arch x86_64h'
    // may cause linker errors.

    // Erroring here is really annoying, better risk linking errors instead
    // of enforcing '-mmacosx-version-min= >= 10.8'.

    if (!getenv("OSXCROSS_NO_X86_64H_DEPLOYMENT_TARGET_WARNING"))
      warn << "'-mmacosx-version-min=' should be '>= 10.8' for architecture "
           << "'" << getArchName(Arch::x86_64h) << "'" << warn.endl();
  }

  if (stdlib == StdLib::unset) {
    if (libCXXIsDefaultCXXLib()) {
      stdlib = StdLib::libcxx;
    } else {
      stdlib = StdLib::libstdcxx;
    }
  } else if (stdlib == StdLib::libcxx) {
    if (!hasLibCXX()) {
      err << "libc++ requires Mac OS X SDK 10.7 (or later)" << err.endl();
      return false;
    }

    if (OSNum.Num() && OSNum < OSVersion(10, 7)) {
      err << "libc++ requires '-mmacosx-version-min=10.7' (or later)"
          << err.endl();
      return false;
    }
  }

  std::string CXXHeaderPath = SDKPath;
  string_vector AdditionalCXXHeaderPaths;

  auto addCXXPath = [&](const std::string &path) {
    std::string tmp;
    tmp = CXXHeaderPath;
    tmp += "/";
    tmp += path;
    AdditionalCXXHeaderPaths.push_back(tmp);
  };

  switch (stdlib) {
  case StdLib::libcxx: {
    CXXHeaderPath += "/usr/include/c++/v1";
    if (!dirExists(CXXHeaderPath)) {
      err << "cannot find " << getStdLibString(stdlib) << " headers"
          << err.endl();
      return false;
    }
    break;
  }
  case StdLib::libstdcxx: {
    if (isGCC())
      break;

    if (usegcclibs) {
      // Use libs from './build_gcc.sh' installation

      CXXHeaderPath += "/../../";
      CXXHeaderPath += otriple;
      CXXHeaderPath += "/include/c++";

      static std::vector<GCCVersion> v;
      v.clear();

      listFiles(CXXHeaderPath.c_str(), nullptr, [](const char *path) {
        if (path[0] != '.')
          v.push_back(parseGCCVersion(path));
        return false;
      });

      if (v.empty()) {
        err << "'-foc-use-gcc-libstdc++' requires gcc to be installed "
               "(./build_gcc.sh)" << err.endl();
        return false;
      }

      std::sort(v.begin(), v.end());
      gccversion = v[v.size() - 1];

      CXXHeaderPath += "/";
      CXXHeaderPath += gccversion.Str();

      addCXXPath(otriple);
    } else {
      // Use SDK libs
      std::string tmp;

      if (SDKOSNum <= OSVersion(10, 5))
        CXXHeaderPath += "/usr/include/c++/4.0.0";
      else
        CXXHeaderPath += "/usr/include/c++/4.2.1";

      tmp = getArchName(arch);
      tmp += "-apple-";
      tmp += target;
      addCXXPath(tmp);
    }

    addCXXPath("backward");

    if (!dirExists(CXXHeaderPath)) {
      err << "cannot find " << getStdLibString(stdlib) << " headers"
          << err.endl();
      return false;
    }

    break;
  }
  case StdLib::unset:
    abort();
  }

  fargs.push_back(compilerexecname);

  if (isClang()) {
    std::string tmp;

    fargs.push_back("-target");
    fargs.push_back(getTriple());

    tmp = "-mlinker-version=";
    tmp += getLinkerVersion();

    fargs.push_back(tmp);
    tmp.clear();

#ifndef __APPLE__
    if (!findClangIntrinsicHeaders(tmp)) {
      warn << "cannot find clang intrinsic headers; please report this "
              "issue to the OSXCross project" << warn.endl();
    } else {
      if (haveArch(Arch::x86_64h) && clangversion < ClangVersion(3, 5)) {
        err << "'" << getArchName(Arch::x86_64h) << "' requires clang 3.5 "
            << "(or later)" << err.endl();
        return false;
      }

      fargs.push_back("-isystem");
      fargs.push_back(tmp);
    }

    tmp.clear();
#endif

    fargs.push_back("-isysroot");
    fargs.push_back(SDKPath);

    if (isCXX()) {
      tmp = "-stdlib=";
      tmp += getStdLibString(stdlib);
      fargs.push_back(tmp);

      if (stdlib == StdLib::libcxx ||
          (stdlib == StdLib::libstdcxx && usegcclibs)) {
        fargs.push_back("-nostdinc++");
        fargs.push_back("-Qunused-arguments");
      }

      if (stdlib == StdLib::libstdcxx && usegcclibs && targetarch.size() < 2 &&
          !isGCH()) {
        // Use libs from './build_gcc' installation
        setupGCCLibs(targetarch[0]);
      }
    }
  } else if (isGCC()) {
    if (isCXX() && isLibCXX()) {
      fargs.push_back("-nostdinc++");
      fargs.push_back("-nodefaultlibs");

      if (!isGCH()) {
        fargs.push_back("-lc");
        fargs.push_back("-lc++");
        fargs.push_back("-lgcc_s.10.5");
      }
    } else if (!isLibCXX() && !isGCH() &&
               !getenv("OSXCROSS_GCC_NO_STATIC_RUNTIME")) {
      fargs.push_back("-static-libgcc");
      fargs.push_back("-static-libstdc++");
    }

    if (!isGCH())
      fargs.push_back("-Wl,-no_compact_unwind");
  }

  auto addCXXHeaderPath = [&](const std::string &path) {
    fargs.push_back(isClang() ? "-cxx-isystem" : "-isystem");
    fargs.push_back(path);
  };

  addCXXHeaderPath(CXXHeaderPath);

  for (auto &path : AdditionalCXXHeaderPaths)
    addCXXHeaderPath(path);

  if (getenv("OSXCROSS_MP_INC")) {
    std::string MacPortsIncludeDir;
    std::string MacPortsLibraryDir;
    std::string MacPortsFrameworksDir;

    // Add them to args (instead of fargs),
    // so the user's -I / -L / -F is prefered.

    if (getMacPortsIncludeDir(MacPortsIncludeDir)) {
      args.push_back("-isystem");
      args.push_back(MacPortsIncludeDir);

      if (getMacPortsLibDir(MacPortsLibraryDir)) {
        if (isClang())
          args.push_back("-Qunused-arguments");

        args.push_back("-L");
        args.push_back(MacPortsLibraryDir);
      }

      if (getMacPortsFrameworksDir(MacPortsFrameworksDir)) {
        args.push_back("-iframework");
        args.push_back(MacPortsFrameworksDir);
      }
    }
  }

  if (OSNum.Num()) {
    std::string tmp;
    tmp = "-mmacosx-version-min=";
    tmp += OSNum.Str();
    fargs.push_back(tmp);
  }

  for (auto arch : targetarch) {
    bool is32bit = false;

    switch (arch) {
    case Arch::i386:
    case Arch::i486:
    case Arch::i586:
    case Arch::i686:
      is32bit = true;
    case Arch::x86_64:
    case Arch::x86_64h:
      if (isGCC()) {
        if (arch == Arch::x86_64h) {
          err << "gcc does not support architecture '" << getArchName(arch)
              << "'" << err.endl();
          return false;
        }

        if (targetarch.size() > 1)
          break;

        fargs.push_back(is32bit ? "-m32" : "-m64");
      } else if (isClang()) {
        if (usegcclibs && targetarch.size() > 1)
          break;
        fargs.push_back("-arch");
        fargs.push_back(getArchName(arch));
      }
      break;
    default:
      err << "unsupported architecture: '" << getArchName(arch) << "'"
          << err.endl();
      return false;
    }
  }

  return true;
}
} // namespace target
