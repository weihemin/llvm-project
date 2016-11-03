//===- Strings.cpp -------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Strings.h"
#include "Config.h"
#include "Error.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/config.h"
#include "llvm/Demangle/Demangle.h"
#include <algorithm>

using namespace llvm;
using namespace lld;
using namespace lld::elf;

// Returns true if S matches T. S can contain glob meta-characters.
// The asterisk ('*') matches zero or more characters, and the question
// mark ('?') matches one character.
static bool globMatch(StringRef S, StringRef T) {
  for (;;) {
    if (S.empty())
      return T.empty();
    if (S[0] == '*') {
      S = S.substr(1);
      if (S.empty())
        // Fast path. If a pattern is '*', it matches anything.
        return true;
      for (size_t I = 0, E = T.size(); I < E; ++I)
        if (globMatch(S, T.substr(I)))
          return true;
      return false;
    }
    if (T.empty() || (S[0] != T[0] && S[0] != '?'))
      return false;
    S = S.substr(1);
    T = T.substr(1);
  }
}

bool StringMatcher::match(StringRef S) {
  for (StringRef P : Patterns)
    if (globMatch(P, S))
      return true;
  return false;
}
// If an input string is in the form of "foo.N" where N is a number,
// return N. Otherwise, returns 65536, which is one greater than the
// lowest priority.
int elf::getPriority(StringRef S) {
  size_t Pos = S.rfind('.');
  if (Pos == StringRef::npos)
    return 65536;
  int V;
  if (S.substr(Pos + 1).getAsInteger(10, V))
    return 65536;
  return V;
}

bool elf::hasWildcard(StringRef S) {
  return S.find_first_of("?*[") != StringRef::npos;
}

StringRef elf::unquote(StringRef S) {
  if (!S.startswith("\""))
    return S;
  return S.substr(1, S.size() - 2);
}

// Converts a glob pattern to a regular expression.
static std::string toRegex(StringRef S) {
  std::string T;
  bool InBracket = false;
  while (!S.empty()) {
    char C = S.front();
    if (InBracket) {
      InBracket = C != ']';
      T += C;
      S = S.drop_front();
      continue;
    }

    if (C == '*')
      T += ".*";
    else if (C == '?')
      T += '.';
    else if (StringRef(".+^${}()|/\\").find_first_of(C) != StringRef::npos)
      T += std::string("\\") + C;
    else
      T += C;

    InBracket = C == '[';
    S = S.substr(1);
  }
  return T;
}

// Converts multiple glob patterns to a regular expression.
Regex elf::compileGlobPatterns(ArrayRef<StringRef> V) {
  std::string T = "^(" + toRegex(V[0]);
  for (StringRef S : V.slice(1))
    T += "|" + toRegex(S);
  return Regex(T + ")$");
}

// Converts a hex string (e.g. "deadbeef") to a vector.
std::vector<uint8_t> elf::parseHex(StringRef S) {
  std::vector<uint8_t> Hex;
  while (!S.empty()) {
    StringRef B = S.substr(0, 2);
    S = S.substr(2);
    uint8_t H;
    if (B.getAsInteger(16, H)) {
      error("not a hexadecimal value: " + B);
      return {};
    }
    Hex.push_back(H);
  }
  return Hex;
}

static bool isAlpha(char C) {
  return ('a' <= C && C <= 'z') || ('A' <= C && C <= 'Z') || C == '_';
}

static bool isAlnum(char C) { return isAlpha(C) || ('0' <= C && C <= '9'); }

// Returns true if S is valid as a C language identifier.
bool elf::isValidCIdentifier(StringRef S) {
  return !S.empty() && isAlpha(S[0]) &&
         std::all_of(S.begin() + 1, S.end(), isAlnum);
}

// Returns the demangled C++ symbol name for Name.
std::string elf::demangle(StringRef Name) {
  // __cxa_demangle can be used to demangle strings other than symbol
  // names which do not necessarily start with "_Z". Name can be
  // either a C or C++ symbol. Don't call __cxa_demangle if the name
  // does not look like a C++ symbol name to avoid getting unexpected
  // result for a C symbol that happens to match a mangled type name.
  if (!Name.startswith("_Z"))
    return Name;

  char *Buf = itaniumDemangle(Name.str().c_str(), nullptr, nullptr, nullptr);
  if (!Buf)
    return Name;
  std::string S(Buf);
  free(Buf);
  return S;
}

std::string elf::maybeDemangle(StringRef Name) {
  if (Config->Demangle)
    return demangle(Name);
  return Name;
}
