#include <UI/CodeTextEdit/Lexers/CPPLexer.h>
#include <regex>
#include <QDebug>

namespace { // Functions reserved for this TU's internal use

  // Detects if a token is a whitespace or newline character
  bool isWhitespace(const QChar c) {
    if (c == ' ' || c == '\n' || c == '\r')
      return true;
    else
      return false;
  }

  void populateReservedKeywords (std::unordered_set<std::string>& set) {
    set.emplace( "alignas" );
    set.emplace( "alignof" );
    set.emplace( "and" );
    set.emplace( "and_eq" );
    set.emplace( "asm" );
    set.emplace( "auto" );
    set.emplace( "bitand" );
    set.emplace( "bitor" );
    set.emplace( "bool" );
    set.emplace( "break" );
    set.emplace( "case" );
    set.emplace( "catch" );
    set.emplace( "char" );
    set.emplace( "char16_t" );
    set.emplace( "char32_t" );
    set.emplace( "class" );
    set.emplace( "compl" );
    set.emplace( "concept" );
    set.emplace( "const" );
    set.emplace( "constexpr" );
    set.emplace( "const_cast" );
    set.emplace( "continue" );
    set.emplace( "decltype" );
    set.emplace( "default" );
    set.emplace( "delete" );
    set.emplace( "do" );
    set.emplace( "double" );
    set.emplace( "dynamic_cast" );
    set.emplace( "else" );
    set.emplace( "enum" );
    set.emplace( "explicit" );
    set.emplace( "export" );
    set.emplace( "extern" );
    set.emplace( "false" );
    set.emplace( "float" );
    set.emplace( "for" );
    set.emplace( "friend" );
    set.emplace( "goto" );
    set.emplace( "if" );
    set.emplace( "inline" );
    set.emplace( "int" );
    set.emplace( "long" );
    set.emplace( "mutable" );
    set.emplace( "namespace" );
    set.emplace( "new" );
    set.emplace( "noexcept" );
    set.emplace( "not" );
    set.emplace( "not_eq" );
    set.emplace( "nullptr" );
    set.emplace( "operator" );
    set.emplace( "or" );
    set.emplace( "or_eq" );
    set.emplace( "private" );
    set.emplace( "protected" );
    set.emplace( "public" );
    set.emplace( "register" );
    set.emplace( "reinterpret_cast" );
    set.emplace( "requires" );
    set.emplace( "return" );
    set.emplace( "short" );
    set.emplace( "signed" );
    set.emplace( "sizeof" );
    set.emplace( "static" );
    set.emplace( "static_assert" );
    set.emplace( "static_cast" );
    set.emplace( "struct" );
    set.emplace( "switch" );
    set.emplace( "template" );
    set.emplace( "this" );
    set.emplace( "thread_local" );
    set.emplace( "throw" );
    set.emplace( "true" );
    set.emplace( "try" );
    set.emplace( "typedef" );
    set.emplace( "typeid" );
    set.emplace( "typename" );
    set.emplace( "union" );
    set.emplace( "unsigned" );
    set.emplace( "using" );
    set.emplace( "virtual" );
    set.emplace( "void" );
    set.emplace( "volatile" );
    set.emplace( "wchar_t" );
    set.emplace( "while" );
    set.emplace( "xor" );
    set.emplace( "xor_eq" );
    set.emplace( "nullptr" );
  }



}

CPPLexer::CPPLexer() :
  LexerBase(CPPLexerType)
{
  populateReservedKeywords (m_reservedKeywords);
}

void CPPLexer::reset() {
  // Reset this lexer's internal state to start another lexing session
}

void CPPLexer::lexInput(std::string& input, StyleDatabase& sdb) {

  str = &input;
  sdb.styleSegment.clear(); // Relex everything // TODO - lex from a position forward?
  styleDb = &sdb;
  pos = 0;

  try {
    globalScope();
  }
  catch (std::out_of_range&) {
    qDebug() << "Parsing terminated!";
  }
}


//==---------------------------------------------------------------------------==//
//                         Scopes handling functions                             //
//==---------------------------------------------------------------------------==//


// Utility function: adds a segment to the style database
void CPPLexer::addSegment(size_t pos, size_t len, Style style) {
  styleDb->styleSegment.emplace_back(pos, len, style);
}

void CPPLexer::declarationOrDefinition() { // A global scope declaration or definition
  // of a function (or some macro-ed stuff e.g. CALLME();)

  // Skip whitespaces
  while (str->at(pos) == ' ') {
    pos++;
  }

  // A return type or identifier is expected here
  size_t startSegment = pos;
  while (str->at(pos) != ' ' && str->at(pos) != '(') {
    pos++;
  }
  if (str->at(pos) == ' ') { // Probably a return type (and it might be a keyword)
    Style s = Normal;
    if(m_reservedKeywords.find(str->substr(startSegment, pos - startSegment)) !=
       m_reservedKeywords.end())
      s = Keyword;
    addSegment(startSegment, pos - startSegment, s);
    pos++;

    // Now add the real identifier (if any)

    // Skip whitespaces
    while (str->at(pos) == ' ') {
      pos++;
    }

    startSegment = pos;
    while (str->at(pos) != '(' && str->at(pos) != ' ') {
      pos++;
    }
    addSegment(startSegment, pos - startSegment, Identifier);
  } else if (str->at(pos) == '(') {
    // Probably some kind of CALLME();
    addSegment(startSegment, pos - startSegment, Identifier);
    pos++;
  }

  // Skip whitespaces
  while (str->at(pos) == ' ') {
    pos++;
  }

  // Either way, there should be a (..arguments..) section here (or nothing at all)
  //TODO
  while (str->at(pos) != '\n') { // REMOVE THIS
    pos++;
  }// REMOVE THIS
  // TODO: HANDLE PARAMETERS (...) - possibly make it portable also for <> templates

}

void CPPLexer::defineStatement() {
  // A define statement is a particular one: it might span one or more lines

  addSegment(pos, 7, Keyword); // #define
  pos += 7;

  // Skip whitespaces
  while (str->at(pos) == ' ') {
    pos++;
  }

  // Now we might have something like
  // #define MYMACRO XX
  // or
  // #define MYMACRO(a,b,c..) something
  // or even
  // #define MYMACRO XX \
  //                 multiline
  //

  size_t startSegment = pos;
  while (str->at(pos) != '(' && str->at(pos) != ' ') {
    pos++;
  }
  addSegment(startSegment, pos - startSegment, Identifier);

  // Regular style for all the rest. A macro, even multiline, ends when a newline not preceded
  // by \ is found
  startSegment = pos;
  while (!(str->at(pos) != '\\' && str->at(pos+1) == '\n')) {
    pos++;
  }
  addSegment(startSegment, pos - startSegment, Normal);
  pos++; // Eat the last character

  // Do not add the \n to the comment (it will be handled outside)
}

void CPPLexer::lineCommentStatement() {
  // A statement spans until a newline is found (or EOF)

  size_t startSegment = pos;

  // Skip everything until \n
  while (str->at(pos) != '\n') {
    pos++;
  }
  // Do not add the \n to the comment (it will be handled outside)

  addSegment(startSegment, pos - startSegment, Comment);
}


void CPPLexer::usingStatement() {
  // A statement spans until a newline is found (or EOF)

  addSegment(pos, 5, Keyword); // using
  pos += 5;

  // Skip whitespaces
  while (str->at(pos) == ' ') {
    pos++;
  }

  if (str->substr(pos, 9).compare("namespace") == 0) {
    addSegment(pos, 9, Keyword); // namespace
    pos += 9;
  }

  // Skip whitespaces
  while (str->at(pos) == ' ') {
    pos++;
  }

  // Whatever identifier we've found until \n
  size_t startSegment = pos;
  while (str->at(pos) != '\n') {
    pos++;
  }
  addSegment(startSegment, pos - startSegment, Normal);
}

void CPPLexer::includeStatement() {
  // A statement spans until a newline is found (or EOF)

  addSegment(pos, 8, Keyword); // #include
  pos += 8;

  // Skip whitespaces, a quoted string is expected
  while (str->at(pos) == ' ') {
    pos++;
  }

  if (str->at(pos) == '"') {
    size_t segmentStart = pos;
    pos++;

    while (str->at(pos) != '"') {
      if (str->at(pos) == '\n')
        return; // Interrupt if a newline is found
      pos++;
    }
    pos++;

    addSegment(segmentStart, pos - segmentStart, QuotedString);
  }

  if (str->at(pos) == '<') {
    size_t segmentStart = pos;
    pos++;

    while (str->at(pos) != '>') {
      if (str->at(pos) == '\n')
        return; // Interrupt if a newline is found
      pos++;
    }
    pos++;

    addSegment(segmentStart, pos - segmentStart, QuotedString);
  }
}

void CPPLexer::multilineComment() {
  size_t segmentStart = pos;

  pos += 2; // Add the '/*' characters

  // Ignore everything until a */ sequence
  while (str->at(pos) != '*' && str->at(pos+1) != '/')
    pos++;

  // Add '*/'
  pos += 2;

  addSegment(segmentStart, pos - segmentStart, Comment);

  return; // Return to whatever scope we were in
}

void CPPLexer::globalScope() {

  // We're at global scope, this will end with EOF
  while (true) {

    // const char *ptr = str->c_str() + pos; // debug

    // Skip newlines and whitespaces
    while (str->at(pos) == ' ' || str->at(pos) == '\r' || str->at(pos) == '\n') {
      // addSegment(pos, 1, Normal); // This is not needed
      pos++;
    }

    if (str->at(pos) == '/' && str->at(pos + 1) == '*') { // Multiline C-style string
      multilineComment();
      continue;
    }

    if (str->at(pos) == '/' && str->at(pos + 1) == '/') { // Line comment
      lineCommentStatement();
      continue;
    }

    if (str->at(pos) == '#' && str->substr(pos + 1, 7).compare("include") == 0) { // #include
      includeStatement();
      continue;
    }

    if (str->at(pos) == '#' && str->substr(pos + 1, 6).compare("define") == 0) { // #define
      defineStatement();
      continue;
    }

    if (str->substr(pos, 5).compare("using") == 0) { // using
      usingStatement();
      continue;
    }

    declarationOrDefinition(); // Last chance: something custom

    // TODO simple/unrecognized identifiers (and increment pos!! FGS!)
    pos++;
  }
}