#include "ShellTranslate.hpp"

#include <cctype>
#include <vector>

namespace havel::repl {
namespace {

// ============================================================================
// small helpers
// ============================================================================

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

bool isBareIdent(const std::string& s) {
  if (s.empty() || !isIdentStart(s[0]))
    return false;
  for (char c : s)
    if (!isIdentChar(c))
      return false;
  return true;
}

bool isGlobChar(char c) { return c == '*' || c == '?' || c == '['; }

std::string trimCopy(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos)
    return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

bool startsWith(const std::string& s, const char* prefix) {
  return s.rfind(prefix, 0) == 0;
}

// Havel double-quoted string literal for `s`.
std::string havelQuote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\t': out += "\\t"; break;
    case '\r': out += "\\r"; break;
    default: out += c; break;
    }
  }
  out += "\"";
  return out;
}

// A word of a shell command: literal pieces plus $VAR / $(cmd) segments.
struct Segment {
  enum Kind { Literal, Var, Subst } kind = Literal;
  std::string text;  // literal text, variable name, or substitution payload
};

struct Word {
  std::vector<Segment> segs;
  std::string raw;      // source text incl. quotes
  bool quoted = false;  // quoting used anywhere (disables glob expansion)
  bool empty() const { return segs.empty(); }
};

struct Stage {
  std::vector<Word> words;
  Word inFile;   // `< file` target
  bool hasIn = false;
  Word outFile;  // `> file` / `>> file` target
  bool hasOut = false;
  bool append = false;
};

struct Token {
  enum Kind { WordT, Pipe, In, Out, Append, Semi } kind = WordT;
  Word word;
};

// ============================================================================
// tokenizer
// ============================================================================

bool tokenize(const std::string& line, std::vector<Token>& out, std::string& err) {
  size_t i = 0;
  const size_t n = line.size();

  auto flushWord = [&](Word w) {
    if (w.segs.empty())
      return;
    Token t;
    t.kind = Token::WordT;
    t.word = std::move(w);
    out.push_back(std::move(t));
  };

  while (i < n) {
    char c = line[i];
    if (c == ' ' || c == '\t') {
      i++;
      continue;
    }
    if (c == ';') {
      Token t;
      t.kind = Token::Semi;
      out.push_back(std::move(t));
      i++;
      continue;
    }
    if (c == '|') {
      if (i + 1 < n && line[i + 1] == '|') {
        err = "'||' between commands is not supported - put the commands on separate lines "
              "or use Havel's || on process results";
        return false;
      }
      Token t;
      t.kind = Token::Pipe;
      out.push_back(std::move(t));
      i++;
      continue;
    }
    if (c == '&') {
      if (i + 1 < n && line[i + 1] == '&') {
        err = "'&&' between commands is not supported - put the commands on separate lines "
              "or use Havel's && on process results";
        return false;
      }
      if (i + 1 < n && line[i + 1] == '>') {
        err = "'&>' is not supported - a process result exposes .stdout and .stderr separately";
        return false;
      }
      err = "background execution ('&') is not supported - use go { ... } to run it in a fiber";
      return false;
    }
    if (c == '>') {
      Token t;
      if (i + 1 < n && line[i + 1] == '>') {
        t.kind = Token::Append;
        i += 2;
      } else {
        t.kind = Token::Out;
        i++;
      }
      if (i < n && line[i] == '&') {
        err = "stderr redirection is not supported - a process result exposes .stderr";
        return false;
      }
      out.push_back(std::move(t));
      continue;
    }
    if (c == '<') {
      if (i + 1 < n && line[i + 1] == '<') {
        err = "heredocs ('<<') are not supported - use a Havel multiline string (``` ... ```)";
        return false;
      }
      Token t;
      t.kind = Token::In;
      out.push_back(std::move(t));
      i++;
      continue;
    }

    // A word (possibly quoted / with expansions).
    Word w;
    bool digitPrefix = false;
    if (std::isdigit(static_cast<unsigned char>(c))) {
      // "2>..." style fd prefix: remember, handled at the redirect token.
      size_t j = i;
      while (j < n && std::isdigit(static_cast<unsigned char>(line[j])))
        j++;
      if (j < n && line[j] == '>') {
        err = "file-descriptor redirection (" + line.substr(i, j - i) +
              ">) is not supported - a process result exposes .stdout and .stderr";
        return false;
      }
      digitPrefix = true;
    }
    (void)digitPrefix;

    while (i < n) {
      char ch = line[i];
      if (ch == ' ' || ch == '\t' || ch == '|' || ch == ';')
        break;
      if ((ch == '<' || ch == '>') && w.segs.empty())
        break;  // redirect token, not part of a word
      if (ch == '\'') {
        w.quoted = true;
        i++;
        std::string lit;
        while (i < n && line[i] != '\'') {
          if (line[i] == '\\' && i + 1 < n) {
            i++;
            lit += line[i++];
          } else {
            lit += line[i++];
          }
        }
        if (i >= n) {
          err = "unterminated single quote";
          return false;
        }
        i++;  // closing '
        Segment s;
        s.kind = Segment::Literal;
        s.text = std::move(lit);
        w.segs.push_back(std::move(s));
        continue;
      }
      if (ch == '"') {
        w.quoted = true;
        i++;
        std::string lit;
        bool closed = false;
        while (i < n) {
          char d = line[i];
          if (d == '"') {
            i++;
            closed = true;
            break;
          }
          if (d == '\\' && i + 1 < n) {
            char e = line[i + 1];
            switch (e) {
            case 'n': lit += '\n'; break;
            case 't': lit += '\t'; break;
            case '"': lit += '"'; break;
            case '\\': lit += '\\'; break;
            case '$': lit += '$'; break;
            default: lit += e; break;
            }
            i += 2;
            continue;
          }
          if (d == '$' && i + 1 < n && (isIdentStart(line[i + 1]) || line[i + 1] == '(')) {
            if (!lit.empty()) {
              Segment s;
              s.kind = Segment::Literal;
              s.text = lit;
              w.segs.push_back(std::move(s));
              lit.clear();
            }
            if (line[i + 1] == '(') {
              i += 2;
              int depth = 1;
              std::string inner;
              while (i < n && depth > 0) {
                if (line[i] == '(')
                  depth++;
                else if (line[i] == ')') {
                  depth--;
                  if (depth == 0) {
                    i++;
                    break;
                  }
                }
                inner += line[i++];
              }
              if (depth != 0) {
                err = "unterminated $( ... ) substitution";
                return false;
              }
              Segment s;
              s.kind = Segment::Subst;
              s.text = trimCopy(inner);
              w.segs.push_back(std::move(s));
            } else {
              i++;
              std::string name;
              while (i < n && isIdentChar(line[i]))
                name += line[i++];
              Segment s;
              s.kind = Segment::Var;
              s.text = name;
              w.segs.push_back(std::move(s));
            }
            continue;
          }
          if (d == '$' && i + 1 < n) {
            err = std::string("unsupported expansion '$") + line[i + 1] +
                  "' - use Havel variables or sys.env(name)";
            return false;
          }
          lit += d;
          i++;
        }
        if (!closed) {
          err = "unterminated double quote";
          return false;
        }
        if (!lit.empty()) {
          Segment s;
          s.kind = Segment::Literal;
          s.text = std::move(lit);
          w.segs.push_back(std::move(s));
        }
        continue;
      }
      if (ch == '\\' && i + 1 < n) {
        i++;
        Segment s;
        s.kind = Segment::Literal;
        s.text = std::string(1, line[i++]);
        w.segs.push_back(std::move(s));
        continue;
      }
      if (ch == '$' && i + 1 < n && (isIdentStart(line[i + 1]) || line[i + 1] == '(')) {
        if (line[i + 1] == '(') {
          i += 2;
          int depth = 1;
          std::string inner;
          while (i < n && depth > 0) {
            if (line[i] == '(')
              depth++;
            else if (line[i] == ')') {
              depth--;
              if (depth == 0) {
                i++;
                break;
              }
            }
            inner += line[i++];
          }
          if (depth != 0) {
            err = "unterminated $( ... ) substitution";
            return false;
          }
          Segment s;
          s.kind = Segment::Subst;
          s.text = trimCopy(inner);
          w.segs.push_back(std::move(s));
        } else {
          i++;
          std::string name;
          while (i < n && isIdentChar(line[i]))
            name += line[i++];
          Segment s;
          s.kind = Segment::Var;
          s.text = name;
          w.segs.push_back(std::move(s));
        }
        continue;
      }
      if (ch == '$') {
        err = std::string("unsupported expansion '$") + (i + 1 < n ? line[i + 1] : ' ') +
              "' - use Havel variables or sys.env(name)";
        return false;
      }
      // plain literal char
      size_t start = i;
      while (i < n) {
        char d = line[i];
        if (d == ' ' || d == '\t' || d == '|' || d == ';' || d == '\'' || d == '"' ||
            d == '\\' || d == '$' || d == '<' || d == '>')
          break;
        if (d == '&')
          break;  // handled by the caller on the next token boundary
        i++;
      }
      if (i == start) {
        err = std::string("unexpected character '") + ch + "' in shell command";
        return false;
      }
      Segment s;
      s.kind = Segment::Literal;
      s.text = line.substr(start, i - start);
      w.segs.push_back(std::move(s));
    }

    if (!w.segs.empty()) {
      w.raw = line.substr(0, 0);  // not tracked precisely; codegen uses segments
      flushWord(std::move(w));
    }
  }
  return true;
}

// ============================================================================
// codegen
// ============================================================================

struct Ctx {
  const std::unordered_set<std::string>* known = nullptr;
  std::unordered_set<std::string> locals;

  bool isLocalOrKnown(const std::string& n) const {
    if (locals.count(n))
      return true;
    return known && known->count(n) > 0;
  }
};

std::string varExpr(const std::string& name, const Ctx& ctx) {
  if (ctx.isLocalOrKnown(name))
    return "str(" + name + ")";
  return "(sys.env(" + havelQuote(name) + ") || \"\")";
}

// Forward declarations (mutual recursion: substitution payloads are command
// lines, and command words may contain substitutions).
std::string translateCommandText(const std::string& cmdline, Ctx& ctx, std::string& err);
std::string translateStatement(const std::string& stmt, Ctx& ctx, std::string& err);

std::string segmentExpr(const Segment& seg, Ctx& ctx, std::string& err) {
  switch (seg.kind) {
  case Segment::Literal:
    return havelQuote(seg.text);
  case Segment::Var:
    return varExpr(seg.text, ctx);
  case Segment::Subst: {
    std::string inner = translateCommandText(seg.text, ctx, err);
    if (!err.empty())
      return "";
    // Command substitution reads stdout; bash strips the trailing newlines,
    // trim() also drops leading whitespace (documented in the REPL help).
    return "(" + inner + ").stdout.trim()";
  }
  }
  return havelQuote("");
}

std::string wordExpr(const Word& w, Ctx& ctx, std::string& err) {
  if (w.segs.empty())
    return havelQuote("");
  std::string out;
  for (const auto& seg : w.segs) {
    std::string e = segmentExpr(seg, ctx, err);
    if (!err.empty())
      return "";
    if (out.empty())
      out = e;
    else
      out += " + " + e;
  }
  return out;
}

bool wordIsGlob(const Word& w) {
  if (w.quoted)
    return false;
  for (const auto& seg : w.segs) {
    if (seg.kind != Segment::Literal)
      return false;  // expansions make matching unpredictable: treat as text
    for (char c : seg.text)
      if (isGlobChar(c))
        return true;
  }
  return false;
}

std::string wordLiteral(const Word& w) {
  std::string s;
  for (const auto& seg : w.segs)
    if (seg.kind == Segment::Literal)
      s += seg.text;
  return s;
}

// `[ ... ]` test content -> Havel boolean expression.
std::string testExpr(const std::string& inner, Ctx& ctx, std::string& err) {
  std::string t = trimCopy(inner);
  bool negate = false;
  if (startsWith(t, "!")) {
    negate = true;
    t = trimCopy(t.substr(1));
  }
  size_t sp = t.find_first_of(" \t");
  if (sp == std::string::npos) {
    err = "[ ... ] test needs an operator and a path (e.g. [ -f file ])";
    return "";
  }
  std::string op = t.substr(0, sp);
  std::string operandRaw = trimCopy(t.substr(sp + 1));
  // One token expected; keep it simple (paths with spaces go in quotes,
  // which the tokenizer already strips into a single segment).
  std::vector<Token> toks;
  std::string terr;
  if (!tokenize(operandRaw, toks, terr) || toks.size() != 1 || toks[0].kind != Token::WordT) {
    err = "[ ... ] test expects a single path operand";
    return "";
  }
  std::string pathExpr = wordExpr(toks[0].word, ctx, err);
  if (!err.empty())
    return "";
  std::string call;
  if (op == "-f")
    call = "fs.isFile(" + pathExpr + ")";
  else if (op == "-d")
    call = "fs.isDir(" + pathExpr + ")";
  else if (op == "-e")
    call = "fs.exists(" + pathExpr + ")";
  else {
    err = "unsupported [ " + op + " ] test - use fs.isFile / fs.isDir / fs.exists "
          "(or Havel's and/or) directly";
    return "";
  }
  return negate ? "!(" + call + ")" : call;
}

// Argument list expression: `[a, b]`, splicing globs as they were unquoted.
std::string argvExpr(const std::vector<Word>& words, Ctx& ctx, std::string& err) {
  std::vector<std::string> pieces;  // array fragments joined with +
  std::vector<std::string> run;      // consecutive non-glob argv elements

  auto flushRun = [&]() {
    if (run.empty())
      return;
    std::string arr = "[";
    for (size_t i = 0; i < run.size(); ++i) {
      if (i)
        arr += ", ";
      arr += run[i];
    }
    arr += "]";
    pieces.push_back(arr);
    run.clear();
  };

  for (const auto& w : words) {
    if (wordIsGlob(w)) {
      flushRun();
      pieces.push_back("fs.globArgs(" + havelQuote(wordLiteral(w)) + ")");
      continue;
    }
    std::string e = wordExpr(w, ctx, err);
    if (!err.empty())
      return "";
    run.push_back(e);
  }
  flushRun();
  if (pieces.empty())
    return "[]";
  std::string out = pieces[0];
  for (size_t i = 1; i < pieces.size(); ++i)
    out += " + " + pieces[i];
  return out;
}

// Stage words that mean a HAVEL function stage rather than a binary. Kept
// deliberately small: names that are also common binaries (grep, sort, head,
// tail, wc, cat, find, cut) stay commands.
bool isHavelStageName(const std::string& name, bool isFirst) {
  if (!isBareIdent(name))
    return false;
  if (isFirst)
    // Bare Havel verbs with no binary counterpart.
    return name == "print" || name == "count" || name == "lines";
  for (const char* k : {"split", "filter", "count", "lines", "print", "map",
                        "where", "len", "keys", "values"}) {
    if (name == k)
      return true;
  }
  return false;
}

bool stageIsHavel(const Stage& st, bool isFirst) {
  if (st.words.empty())
    return false;
  const Word& w = st.words[0];
  if (w.quoted || w.segs.size() != 1 || w.segs[0].kind != Segment::Literal)
    return false;
  return isHavelStageName(w.segs[0].text, isFirst);
}

// A Havel stage's call expression. `filter 'pattern'` (string argument)
// becomes grep(pattern) - the substring filter the user means, since Havel's
// filter takes a predicate function.
std::string havelStageExpr(const Stage& st, Ctx& ctx, std::string& err) {
  const Word& w = st.words[0];
  std::string name = w.segs[0].text;
  std::string args;
  for (size_t i = 1; i < st.words.size(); ++i) {
    std::string e = wordExpr(st.words[i], ctx, err);
    if (!err.empty())
      return "";
    if (!args.empty())
      args += ", ";
    args += e;
  }
  if (name == "filter" && !args.empty() && st.words.size() == 2 &&
      st.words[1].segs.size() == 1 && st.words[1].segs[0].kind == Segment::Literal)
    name = "grep";  // `| filter 'chrome'` is a substring filter
  if (args.empty())
    return name;
  return name + "(" + args + ")";
}

// Path-ish expression for a word: `~/x` expands to sys.home() + "/x".
std::string pathWordExpr(const Word& w, Ctx& ctx, std::string& err) {
  std::string e = wordExpr(w, ctx, err);
  if (!err.empty())
    return "";
  if (w.segs.size() == 1 && w.segs[0].kind == Segment::Literal) {
    const std::string& lit = w.segs[0].text;
    if (lit == "~")
      return "sys.home()";
    if (startsWith(lit, "~/"))
      return "sys.home() + " + havelQuote(lit.substr(1));
  }
  return e;
}

// Build the source for a tokenized pipeline: consecutive command stages
// collapse into one process.pipe call (real OS pipes); Havel stages chain
// with the pipe operator. A command stage after a Havel stage is rejected -
// the piped value would become process.pipe's stage list, not its stdin.
std::string buildPipelineSource(const std::vector<Stage>& stages, Ctx& ctx,
                                std::string& err, const std::string& stdinExpr) {
  struct Run {
    bool havel = false;
    std::vector<const Stage*> stages;
  };
  std::vector<Run> runs;
  for (size_t i = 0; i < stages.size(); ++i) {
    bool hv = stageIsHavel(stages[i], i == 0);
    if (runs.empty() || runs.back().havel != hv) {
      Run r;
      r.havel = hv;
      runs.push_back(std::move(r));
    }
    runs.back().stages.push_back(&stages[i]);
  }

  std::string out;
  for (size_t ri = 0; ri < runs.size(); ++ri) {
    Run& run = runs[ri];
    if (run.havel) {
      for (const Stage* st : run.stages) {
        std::string e = havelStageExpr(*st, ctx, err);
        if (!err.empty())
          return "";
        out += out.empty() ? e : " | " + e;
      }
      continue;
    }
    // Command run: one process.pipe call.
    std::string stagesExpr;
    if (run.stages.size() == 1) {
      stagesExpr = argvExpr(run.stages[0]->words, ctx, err);
    } else {
      stagesExpr = "[";
      for (size_t i = 0; i < run.stages.size(); ++i) {
        if (i)
          stagesExpr += ", ";
        stagesExpr += argvExpr(run.stages[i]->words, ctx, err);
        if (!err.empty())
          return "";
      }
      stagesExpr += "]";
    }
    if (!err.empty())
      return "";
    std::string call = "process.pipe(" + stagesExpr;
    if (ri == 0 && !stdinExpr.empty())
      call += ", " + stdinExpr;
    call += ")";
    out += out.empty() ? call : " | " + call;
  }
  return out;
}

// Full command line (may contain `|`, `<`, `>`) as a process.pipe(...) call
// (or a mixed process.pipe | havelStage chain), wrapped in a redirect when
// one is present.
std::string translateCommandText(const std::string& cmdline, Ctx& ctx, std::string& err) {
  std::vector<Token> toks;
  if (!tokenize(cmdline, toks, err))
    return "";

  std::vector<Stage> stages(1);
  for (const auto& t : toks) {
    if (t.kind == Token::Semi) {
      err = "';' inside a command is not supported - put statements on separate lines";
      return "";
    }
    if (t.kind == Token::Pipe) {
      stages.emplace_back();
      continue;
    }
    if (t.kind == Token::In) {
      if (stages.size() > 1 || stages.back().hasIn || stages.back().hasOut) {
        err = "'<' is only supported on the first command of a pipeline";
        return "";
      }
      stages.back().hasIn = true;  // the next word becomes the file operand
      continue;
    }
    if (t.kind == Token::Out || t.kind == Token::Append) {
      stages.back().hasOut = true;  // the next word becomes the file operand
      stages.back().append = (t.kind == Token::Append);
      continue;
    }
    // Word: attach to the pending redirect if one was opened, else argv.
    Stage& st = stages.back();
    bool wantIn = st.hasIn && st.inFile.segs.empty();
    bool wantOut = st.hasOut && st.outFile.segs.empty();
    if (wantIn) {
      st.inFile = t.word;
      continue;
    }
    if (wantOut) {
      st.outFile = t.word;
      continue;
    }
    st.words.push_back(t.word);
  }

  // Validate stages.
  bool anyHavel = false;
  for (size_t i = 0; i < stages.size(); ++i) {
    Stage& st = stages[i];
    if (st.words.empty()) {
      err = "empty command in pipeline";
      return "";
    }
    if (st.hasOut && i + 1 != stages.size()) {
      err = "'>' on a non-final pipeline stage is not supported - redirect the finished "
            "pipeline instead";
      return "";
    }
    if (st.hasIn && (i != 0 || st.inFile.segs.empty())) {
      err = "'<' is only supported on the first command of a pipeline";
      return "";
    }
    if (st.hasOut && st.outFile.segs.empty()) {
      err = "'>' needs a file operand";
      return "";
    }
    if (stageIsHavel(st, i == 0))
      anyHavel = true;
  }

  std::string stdinExpr;
  if (stages[0].hasIn)
    stdinExpr = "fs.read(" + pathWordExpr(stages[0].inFile, ctx, err) + ")";
  if (!err.empty())
    return "";

  // Output redirection is only defined for pure command pipelines: the
  // written value is the pipeline's stdout.
  const Stage& last = stages.back();
  if (last.hasOut && anyHavel) {
    err = "'>' is only supported for command pipelines - capture the Havel "
          "result first (x = ...) and use fs.write";
    return "";
  }

  std::string call = buildPipelineSource(stages, ctx, err, stdinExpr);
  if (!err.empty())
    return "";

  if (last.hasOut) {
    std::string fileExpr = pathWordExpr(last.outFile, ctx, err);
    if (!err.empty())
      return "";
    std::string fn = last.append ? "fs.append" : "fs.write";
    return fn + "(" + fileExpr + ", " + call + ".stdout)";
  }
  return call;
}

// ============================================================================
// statement classification
// ============================================================================

// Split a line on unquoted, top-level `;` and `{`/`}` boundaries are ignored
// (braces belong to the statement itself).
std::vector<std::string> splitTopLevel(const std::string& line) {
  std::vector<std::string> parts;
  std::string cur;
  int depth = 0;
  bool inS = false, inD = false;
  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (inS) {
      cur += c;
      if (c == '\'')
        inS = false;
      continue;
    }
    if (inD) {
      cur += c;
      if (c == '\\' && i + 1 < line.size()) {
        cur += line[++i];
        continue;
      }
      if (c == '"')
        inD = false;
      continue;
    }
    if (c == '\'') {
      inS = true;
      cur += c;
      continue;
    }
    if (c == '"') {
      inD = true;
      cur += c;
      continue;
    }
    if (c == '{' || c == '(' || c == '[') {
      depth++;
      cur += c;
      continue;
    }
    if (c == '}' || c == ')' || c == ']') {
      depth--;
      cur += c;
      continue;
    }
    if (c == ';' && depth == 0) {
      parts.push_back(cur);
      cur.clear();
      continue;
    }
    cur += c;
  }
  parts.push_back(cur);
  return parts;
}

// Index of first unquoted '{' (npos when absent) and the matching '}' of a
// trailing inline body.
size_t firstBrace(const std::string& s) {
  bool inS = false, inD = false;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (inS) {
      if (c == '\'')
        inS = false;
      continue;
    }
    if (inD) {
      if (c == '\\' && i + 1 < s.size()) {
        i++;
        continue;
      }
      if (c == '"')
        inD = false;
      continue;
    }
    if (c == '\'')
      inS = true;
    else if (c == '"')
      inD = true;
    else if (c == '{')
      return i;
  }
  return std::string::npos;
}

std::string replaceVarRefs(const std::string& text, Ctx& ctx) {
  std::string out;
  bool inS = false, inD = false;
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (inS) {
      out += c;
      if (c == '\'')
        inS = false;
      continue;
    }
    if (inD) {
      out += c;
      if (c == '\\' && i + 1 < text.size()) {
        out += text[++i];
        continue;
      }
      if (c == '"')
        inD = false;
      continue;
    }
    if (c == '\'') {
      inS = true;
      out += c;
      continue;
    }
    if (c == '"') {
      inD = true;
      out += c;
      continue;
    }
    if (c == '$' && i + 1 < text.size() && isIdentStart(text[i + 1])) {
      size_t j = i + 1;
      std::string name;
      while (j < text.size() && isIdentChar(text[j]))
        name += text[j++];
      out += varExpr(name, ctx);
      i = j - 1;
      continue;
    }
    out += c;
  }
  return out;
}

// Translate the body of an inline `{ ... }` (statements separated by `;`).
std::string translateBody(const std::string& body, Ctx& ctx, std::string& err) {
  auto parts = splitTopLevel(body);
  std::string out;
  for (auto& part : parts) {
    std::string t = trimCopy(part);
    if (t.empty())
      continue;
    std::string s = translateStatement(t, ctx, err);
    if (!err.empty())
      return "";
    if (!out.empty())
      out += "; ";
    out += s;
  }
  return out;
}

std::vector<std::string> splitWordsSimple(const std::string& s);

std::string translateStructural(const std::string& header, const std::string& body,
                                bool hasBody, Ctx& ctx, std::string& err) {
  std::string h = trimCopy(header);
  std::vector<std::string> hw = splitWordsSimple(h);
  std::string out = h;

  if (!hw.empty() && hw[0] == "for") {
    // for NAME in <iterable> - and Havel's paren form for (a, b) in <map>.
    std::string varPart, iterRaw;
    if (startsWith(h, "for (")) {
      // for (a, b) in <iterable>: keep Havel's destructuring paren form.
      size_t close = h.find(')');
      size_t in = h.find(" in ");
      if (close == std::string::npos || in == std::string::npos || in < close) {
        err = "for (a, b) loop needs Havel's `for (a, b) in <iterable> { ... }` shape";
        return "";
      }
      varPart = trimCopy(h.substr(3, close - 3));
      iterRaw = trimCopy(h.substr(in + 4));
    } else {
      size_t p = h.find(" in ");
      if (p == std::string::npos) {
        err = "for-loop needs Havel's `for name in <iterable> { ... }` shape";
        return "";
      }
      varPart = trimCopy(h.substr(3, p - 3));
      iterRaw = trimCopy(h.substr(p + 4));
    }
    if (varPart.find(',') != std::string::npos) {
      // Multi-iterator: register both names as block locals; Havel's paren
      // form carries them (used for maps: for (k, v) in m { ... }). The
      // input may or may not include the parens; emit exactly one pair.
      std::string inner = varPart;
      while (!inner.empty() && (inner.front() == '(' || inner.front() == ' '))
        inner.erase(inner.begin());
      while (!inner.empty() && (inner.back() == ')' || inner.back() == ' '))
        inner.pop_back();
      auto names = splitWordsSimple(inner);
      for (auto& nm : names) {
        while (!nm.empty() && (nm.front() == ','))
          nm.erase(nm.begin());
        while (!nm.empty() && (nm.back() == ','))
          nm.pop_back();
        if (isBareIdent(nm))
          ctx.locals.insert(nm);
      }
      varPart = "(" + trimCopy(inner) + ")";
    } else if (!isBareIdent(varPart)) {
      err = "for-loop variable must be a plain name";
      return "";
    }
    ctx.locals.insert(varPart);

    std::string iterExpr;
    std::vector<Token> toks;
    std::string terr;
    if (tokenize(iterRaw, toks, terr) && toks.size() == 1 && toks[0].kind == Token::WordT &&
        wordIsGlob(toks[0].word)) {
      iterExpr = "fs.glob(" + havelQuote(wordLiteral(toks[0].word)) + ")";
    } else if (isBareIdent(iterRaw) && iterRaw == "glob") {
      iterExpr = "fs.glob";  // bare `glob` in Havel terms
    } else {
      iterExpr = replaceVarRefs(iterRaw, ctx);
      // Bare `glob("~/Documents")` (no fs. prefix) maps to fs.glob.
      if (startsWith(iterExpr, "glob("))
        iterExpr = "fs." + iterExpr;
      else if (startsWith(iterExpr, "glob ("))
        iterExpr = "fs." + iterExpr.substr(0, 4) + iterExpr.substr(5);
    }
    out = "for " + varPart + " in " + iterExpr;
  } else if (!hw.empty() && (hw[0] == "if" || hw[0] == "while")) {
    // Rewrite any `[ ... ]` test in the condition.
    std::string cond = trimCopy(h.substr(hw[0].size()));
    size_t lb = cond.find('[');
    size_t rb = cond.rfind(']');
    if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
      std::string inner = cond.substr(lb + 1, rb - lb - 1);
      std::string expr = testExpr(inner, ctx, err);
      if (!err.empty())
        return "";
      cond = trimCopy(cond.substr(0, lb) + expr + cond.substr(rb + 1));
    }
    cond = replaceVarRefs(cond, ctx);
    out = hw[0] + " " + cond;
  } else {
    // go { ... } and anything else structural: keep the header.
    out = replaceVarRefs(h, ctx);
  }

  if (hasBody) {
    std::string bodyOut = translateBody(body, ctx, err);
    if (!err.empty())
      return "";
    out += " { " + bodyOut + " }";
  } else {
    out += " {";
  }
  return out;
}

// ============================================================================
// classification
// ============================================================================

std::vector<std::string> splitWordsSimple(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == ' ' || c == '\t') {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
      continue;
    }
    cur += c;
  }
  if (!cur.empty())
    out.push_back(cur);
  return out;
}

const char* kStructuralKeywords[] = {"for", "if", "while", "go", "else", "match"};

bool isStructural(const std::string& first) {
  for (const char* k : kStructuralKeywords)
    if (first == k)
      return true;
  return false;
}

std::string fail(std::string& err, const std::string& msg) {
  err = msg;
  return "";
}

std::string translateOneStatement(const std::string& raw, Ctx& ctx, std::string& err) {
  std::string line = trimCopy(raw);
  if (line.empty())
    return "";

  // ---- rejections (outside quotes) ---------------------------------------
  {
    bool inS = false, inD = false;
    for (size_t i = 0; i < line.size(); ++i) {
      char c = line[i];
      if (inS) {
        if (c == '\'')
          inS = false;
        continue;
      }
      if (inD) {
        if (c == '\\' && i + 1 < line.size()) {
          i++;
          continue;
        }
        if (c == '"')
          inD = false;
        continue;
      }
      if (c == '\'') {
        inS = true;
        continue;
      }
      if (c == '"') {
        inD = true;
        continue;
      }
      if (c == '[' && i + 1 < line.size() && line[i + 1] == '[')
        return fail(err, "[[ ]] is not supported - use [ -f x ] or Havel's && / ||");
      if (c == '$' && i + 1 < line.size() &&
          (line[i + 1] == '@' || line[i + 1] == '*' || line[i + 1] == '#'))
        return fail(err, "positional parameters ($@ $* $#) are not supported - "
                         "use Havel's args array");
      if (c == '$' && i + 2 < line.size() && line[i + 1] == '(' && line[i + 2] == '(')
        return fail(err, "arithmetic expansion $(( )) is not supported - use Havel arithmetic");
      if ((c == '(' && i + 1 < line.size() && line[i + 1] == '('))
        return fail(err, "arithmetic (( )) is not supported - use Havel arithmetic "
                         "(e.g. x * 2)");
      if (c == '(' && trimCopy(line).rfind("(", 0) == 0)
        return fail(err, "subshells are not supported - use go { ... } or plain braces");
      if (c == '2' && i + 1 < line.size() && line[i + 1] == '>' )
        return fail(err, "stderr redirection is not supported - a process result "
                         "exposes .stderr");
    }
    for (const char* kw : {"declare ", "local ", "readonly ", "typeset "}) {
      if (startsWith(line, kw))
        return fail(err, std::string("'") + trimCopy(kw) +
                             "' is not supported - use Havel's val / plain assignment");
    }
    size_t eqp = line.find("=(");
    if (eqp != std::string::npos && eqp > 0 && isIdentChar(line[eqp - 1]) &&
        (eqp < 2 || !isIdentChar(line[eqp - 2]) || isIdentStart(line[eqp - 2]))) {
      std::string before = line.substr(0, eqp);
      if (isBareIdent(trimCopy(before)))
        return fail(err, "array assignment is not supported - use a Havel array "
                         "(files = [\"a\", \"b\"])");
    }
  }

  // ---- assignment:  NAME = RHS  (also NAME=RHS) ---------------------------
  {
    size_t i = 0;
    if (isIdentStart(line[0])) {
      std::string name;
      while (i < line.size() && isIdentChar(line[i]))
        name += line[i++];
      size_t j = i;
      while (j < line.size() && (line[j] == ' ' || line[j] == '\t'))
        j++;
      if (j < line.size() && line[j] == '=' && (j + 1 >= line.size() || line[j + 1] != '=')) {
        std::string rhs = trimCopy(line.substr(j + 1));
        ctx.locals.insert(name);
        std::string rhsExpr;
        if (rhs.empty() || rhs == "\"\"" || rhs == "''") {
          rhsExpr = "\"\"";
        } else if (startsWith(rhs, "$(") && rhs.back() == ')') {
          std::string inner = trimCopy(rhs.substr(2, rhs.size() - 3));
          std::string call = translateCommandText(inner, ctx, err);
          if (!err.empty())
            return "";
          rhsExpr = call + ".stdout.trim()";
        } else if (rhs.find('|') != std::string::npos) {
          // `x = cmd | cmd` (or a mixed chain): the RHS is a pipeline.
          std::string call = translateCommandText(rhs, ctx, err);
          if (!err.empty())
            return "";
          rhsExpr = call;
        } else {
          std::vector<Token> toks;
          std::string terr;
          if (tokenize(rhs, toks, terr) && toks.size() == 1 && toks[0].kind == Token::WordT &&
              wordIsGlob(toks[0].word)) {
            rhsExpr = "fs.glob(" + havelQuote(wordLiteral(toks[0].word)) + ")";
          } else if (tokenize(rhs, toks, terr) && toks.size() == 1 &&
                     toks[0].kind == Token::WordT &&
                     !toks[0].word.segs.empty()) {
            // Single word: quoted string, $VAR, or bare word.
            const Word& w = toks[0].word;
            bool bareLiteral =
                w.segs.size() == 1 && w.segs[0].kind == Segment::Literal && !w.quoted;
            if (bareLiteral) {
              rhsExpr = havelQuote(wordLiteral(w));  // bash: NAME=word is a string
            } else {
              rhsExpr = wordExpr(w, ctx, err);
              if (!err.empty())
                return "";
            }
          } else {
            // Anything else: a Havel expression (1 + 1, process.run(...), [..]).
            rhsExpr = replaceVarRefs(rhs, ctx);
          }
        }
        return name + " = " + rhsExpr;
      }
    }
  }

  std::vector<std::string> words = splitWordsSimple(line);
  std::string first = words.empty() ? "" : words[0];

  // Paren-less call: `print "too many"`, `print a, b` - Havel accepts these,
  // so keep them Havel (the shell layer only rewrites `echo`, not print).
  if (isBareIdent(first)) {
    std::string rest = trimCopy(line.substr(first.size()));
    if (!rest.empty() && rest[0] == '"' && first == "print")
      return replaceVarRefs(line, ctx);
  }

  // ---- builtins -----------------------------------------------------------
  if (first == "cd") {
    std::string arg = trimCopy(line.substr(2));
    if (arg.empty())
      return "shell.cd(sys.home())";
    std::vector<Token> toks;
    std::string terr;
    if (!tokenize(arg, toks, terr) || toks.size() != 1 || toks[0].kind != Token::WordT)
      return fail(err, "cd takes one path");
    std::string e = pathWordExpr(toks[0].word, ctx, err);
    if (!err.empty())
      return "";
    return "shell.cd(" + e + ")";
  }
  if (first == "pwd")
    return "print(shell.cwd())";
  if (first == "export") {
    std::string rest = trimCopy(line.substr(6));
    if (rest.empty())
      return fail(err, "export needs NAME=value (env vars are read with sys.env(name))");
    auto parts = splitWordsSimple(rest);
    std::string out;
    for (const auto& p : parts) {
      size_t eq = p.find('=');
      if (eq == std::string::npos || !isBareIdent(p.substr(0, eq)))
        return fail(err, "export expects NAME=value pairs");
      std::string name = p.substr(0, eq);
      std::string val = p.substr(eq + 1);
      ctx.locals.insert(name);  // `$NAME` afterwards prefers this value
      std::string valExpr;
      std::vector<Token> toks;
      std::string terr;
      if (!val.empty() && tokenize(val, toks, terr) && toks.size() == 1 &&
          toks[0].kind == Token::WordT) {
        const Word& w = toks[0].word;
        if (w.segs.size() == 1 && w.segs[0].kind == Segment::Literal && !w.quoted)
          valExpr = havelQuote(wordLiteral(w));
        else
          valExpr = wordExpr(w, ctx, err);
        if (!err.empty())
          return "";
      } else {
        // Multi-word / unparseable value: keep the raw text as a literal.
        valExpr = havelQuote(val);
      }
      if (!out.empty())
        out += "; ";
      out += "shell.env(" + havelQuote(name) + ", " + valExpr + ")";
    }
    return out;
  }
  if (first == "echo") {
    std::string rest = trimCopy(line.substr(4));
    if (rest.empty())
      return "print(\"\")";
    std::vector<Token> toks;
    std::string terr;
    if (!tokenize(rest, toks, terr))
      return fail(err, terr);
    std::string joined;
    for (const auto& t : toks) {
      if (t.kind != Token::WordT)
        return fail(err, "echo arguments must be words (no pipes or redirects)");
      std::string e = wordExpr(t.word, ctx, err);
      if (!err.empty())
        return "";
      if (!joined.empty())
        joined += " + \" \" + ";
      joined += e;
    }
    return "print(" + joined + ")";
  }
  if (first == "exit") {
    std::string arg = trimCopy(line.substr(4));
    if (arg.empty())
      return "exit()";
    return "exit(" + arg + ")";
  }

  // ---- [ ... ] test -------------------------------------------------------
  if (first == "[" && line.find(']') != std::string::npos) {
    size_t rb = line.rfind(']');
    return testExpr(line.substr(1, rb - 1), ctx, err);
  }

  // ---- structural (Havel control flow with shell-shaped parts) ------------
  if (isStructural(first)) {
    size_t b = firstBrace(line);
    std::string header = (b == std::string::npos) ? line : line.substr(0, b);
    std::string body;
    bool hasBody = false;
    if (b != std::string::npos) {
      size_t close = line.rfind('}');
      if (close == std::string::npos || close < b)
        return fail(err, "unbalanced '{' in " + first + " line");
      std::string tail = trimCopy(line.substr(close + 1));
      if (!tail.empty())
        return fail(err, "unexpected text after '}'");
      body = line.substr(b + 1, close - b - 1);
      hasBody = true;
    }
    return translateStructural(header, body, hasBody, ctx, err);
  }

  // ---- Havel passthrough --------------------------------------------------
  {
    char c0 = line[0];
    if (c0 == '"' || c0 == '\'' || c0 == '{' || c0 == '(' || c0 == '@' || c0 == '`' ||
        std::isdigit(static_cast<unsigned char>(c0)) || c0 == '-' || c0 == '!')
      return replaceVarRefs(line, ctx);
    if (isIdentStart(c0)) {
      // The identifier must be followed IMMEDIATELY by '(' or '.' for the
      // line to be a Havel call: `print(x)` and `process.run("ls")` are
      // Havel, `ls -la` is a command (`ls` + space is not a call).
      size_t ilen = 0;
      while (ilen < line.size() && isIdentChar(line[ilen]))
        ilen++;
      bool havelShape =
          (ilen < line.size() && (line[ilen] == '(' || line[ilen] == '.')) ||
          (ilen == line.size() && ctx.isLocalOrKnown(line));
      if (havelShape)
        return replaceVarRefs(line, ctx);
    }
  }

  // ---- shell command ------------------------------------------------------
  return translateCommandText(line, ctx, err);
}

std::string translateStatement(const std::string& stmt, Ctx& ctx, std::string& err) {
  auto parts = splitTopLevel(stmt);
  std::string out;
  for (auto& part : parts) {
    std::string t = trimCopy(part);
    if (t.empty())
      continue;
    std::string s = translateOneStatement(t, ctx, err);
    if (!err.empty())
      return "";
    if (!out.empty())
      out += "\n";
    out += s;
  }
  return out;
}

// Last non-empty line of the generated source starts with a side-effect
// builtin: the REPL suppresses the printed value for those (a shell prints
// nothing for cd/export, and a redirected command's output went to a file).
bool sourceIsSilent(const std::string& src) {
  std::string last;
  size_t pos = 0;
  while (pos < src.size()) {
    size_t nl = src.find('\n', pos);
    std::string l = (nl == std::string::npos) ? src.substr(pos) : src.substr(pos, nl - pos);
    std::string t = trimCopy(l);
    if (!t.empty())
      last = t;
    if (nl == std::string::npos)
      break;
    pos = nl + 1;
  }
  for (const char* p : {"shell.cd(", "shell.env(", "fs.write(", "fs.append(", "exit("}) {
    if (startsWith(last, p))
      return true;
  }
  return false;
}

}  // namespace

ShellTranslation translateShellLine(const std::string& line,
                                    const std::unordered_set<std::string>* knownGlobals) {
  ShellTranslation result;
  Ctx ctx;
  ctx.known = knownGlobals;
  std::string err;
  std::string s = translateStatement(line, ctx, err);
  if (!err.empty()) {
    result.ok = false;
    result.error = err;
    return result;
  }
  result.source = s;
  result.silent = sourceIsSilent(s);
  return result;
}

ShellTranslation translateShellBlock(const std::string& block,
                                     const std::unordered_set<std::string>* knownGlobals) {
  ShellTranslation result;
  Ctx ctx;
  ctx.known = knownGlobals;

  std::string out;
  std::string err;
  size_t pos = 0;
  while (pos <= block.size()) {
    size_t nl = block.find('\n', pos);
    std::string line = nl == std::string::npos ? block.substr(pos) : block.substr(pos, nl - pos);
    pos = (nl == std::string::npos) ? block.size() + 1 : nl + 1;

    std::string t = trimCopy(line);
    if (t.empty())
      continue;
    // Closing brace lines stay as-is (they end a block opened earlier).
    if (t[0] == '}') {
      if (!out.empty())
        out += "\n";
      out += t;
      continue;
    }
    std::string s = translateStatement(t, ctx, err);
    if (!err.empty()) {
      result.ok = false;
      result.error = err;
      return result;
    }
    if (s.empty())
      continue;
    if (!out.empty())
      out += "\n";
    out += s;
  }
  result.source = out;
  result.silent = sourceIsSilent(out);
  return result;
}

}  // namespace havel::repl
