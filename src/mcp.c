#define _POSIX_C_SOURCE 200809L

#include "mcp.h"

#include <string.h>

/* The state of one json_parse call. */
typedef struct {
  Arena *arena;
  const char *text;
  size_t len;
  size_t pos;
  int depth; /* the open arrays and objects */
  Diag *diag;
} Reader;

/* The byte at POS, or -1 after the end. */
static int peek_at(const Reader *r, size_t pos) {
  return pos < r->len ? (unsigned char)r->text[pos] : -1;
}

static int peek(const Reader *r) {
  return peek_at(r, r->pos);
}

static int fail(Reader *r, const char *what) {
  return diag_fail(r->diag, "USAGE", NULL, "JSON: %s at byte %zu", what, r->pos);
}

static int no_memory(Reader *r) {
  return diag_fail(r->diag, "OOM", NULL, "no memory for a JSON value");
}

static void skip_space(Reader *r) {
  while (peek(r) == ' ' || peek(r) == '\t' || peek(r) == '\n' || peek(r) == '\r') r->pos++;
}

static int is_digit(int c) {
  return c >= '0' && c <= '9';
}

static size_t skip_digits(Reader *r) {
  size_t start = r->pos;
  while (is_digit(peek(r))) r->pos++;
  return r->pos - start;
}

/* true, false or null. */
static int read_word(Reader *r, const char *word, JsonKind kind, JsonValue *out) {
  size_t n = strlen(word);
  if (r->len - r->pos < n || memcmp(r->text + r->pos, word, n) != 0) return fail(r, "expected a value");
  r->pos += n;
  out->kind = kind;
  return 1;
}

/* -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)? as RFC 8259 says. TEXT
   keeps the source text. */
static int read_number(Reader *r, JsonValue *out) {
  size_t start = r->pos;
  r->pos += peek(r) == '-';
  int zero = peek(r) == '0';
  r->pos += zero;
  if (!zero && skip_digits(r) == 0) return fail(r, "expected a digit");
  int fraction = peek(r) == '.';
  r->pos += fraction;
  if (fraction && skip_digits(r) == 0) return fail(r, "expected a digit");
  int exponent = peek(r) == 'e' || peek(r) == 'E';
  r->pos += exponent;
  r->pos += exponent && (peek(r) == '+' || peek(r) == '-');
  if (exponent && skip_digits(r) == 0) return fail(r, "expected a digit");
  char *text = arena_strndup(r->arena, r->text + start, r->pos - start);
  if (text == NULL) return no_memory(r);
  out->kind = JSON_NUMBER;
  out->text = text;
  out->len = r->pos - start;
  return 1;
}

static int hex_digit(int c) {
  return is_digit(c) ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* The value of the 4 hex digits at POS, or -1. */
static long hex4(const Reader *r, size_t pos) {
  long value = 0;
  for (size_t i = 0; i < 4; i++) {
    int digit = hex_digit(peek_at(r, pos + i));
    if (digit < 0) return -1;
    value = value * 16 + digit;
  }
  return value;
}

/* Writes the scalar CP as UTF-8 at DST. Returns the byte count. */
static size_t put_utf8(unsigned char *dst, unsigned long cp) {
  static const unsigned char LEAD[] = {0x00, 0x00, 0xC0, 0xE0, 0xF0};
  size_t n = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
  for (size_t i = n; i > 1; i--) {
    dst[i - 1] = (unsigned char)(0x80 | (cp & 0x3F));
    cp >>= 6;
  }
  dst[0] = (unsigned char)(LEAD[n] | cp);
  return n;
}

/* Decodes the escape at POS into BUF at *N. The \u escape of a high
   surrogate takes the \u escape of the low surrogate after it. */
static int read_escape(Reader *r, unsigned char *buf, size_t *n) {
  static const char FROM[] = "\"\\/bfnrt";
  static const char TO[] = "\"\\/\b\f\n\r\t";
  int c = peek_at(r, r->pos + 1);
  const char *hit = c > 0 ? strchr(FROM, c) : NULL;
  if (hit != NULL) {
    buf[*n] = (unsigned char)TO[hit - FROM];
    *n += 1;
    r->pos += 2;
    return 1;
  }
  long cp = c == 'u' ? hex4(r, r->pos + 2) : -2;
  if (cp == -2) return fail(r, "an unknown escape");
  if (cp < 0) return fail(r, "expected 4 hex digits after \\u");
  int high = cp >= 0xD800 && cp <= 0xDBFF;
  long low = high && peek_at(r, r->pos + 6) == '\\' && peek_at(r, r->pos + 7) == 'u' ? hex4(r, r->pos + 8) : -1;
  if (cp >= 0xDC00 && cp <= 0xDFFF) return fail(r, "a low surrogate without a high surrogate");
  if (high && (low < 0xDC00 || low > 0xDFFF)) return fail(r, "a high surrogate without a low surrogate");
  cp = high ? 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00) : cp;
  r->pos += high ? 12 : 6;
  *n += put_utf8(buf + *n, (unsigned long)cp);
  return 1;
}

/* Sets END to the position of the closing quote of the string at POS. */
static int string_end(const Reader *r, size_t *end) {
  size_t pos = r->pos + 1;
  int c = peek_at(r, pos);
  while (c >= 0 && c != '"') {
    pos += c == '\\' ? 2 : 1;
    c = peek_at(r, pos);
  }
  *end = pos;
  return c == '"';
}

/* Reads the string at POS into TEXT, NUL-terminated, and its byte count
   into LEN. The decoded bytes are not more than the source bytes. */
static int read_string(Reader *r, const char **text, size_t *len) {
  size_t end;
  if (!string_end(r, &end)) return fail(r, "the string has no closing quote");
  unsigned char *buf = arena_alloc(r->arena, end - r->pos);
  if (buf == NULL) return no_memory(r);
  size_t n = 0;
  r->pos++;
  while (r->pos < end) {
    int c = peek(r);
    if (c == '\\') {
      if (!read_escape(r, buf, &n)) return 0;
      continue;
    }
    if (c < 0x20) return fail(r, "a control byte in a string");
    size_t width = utf8_char_size(r->text + r->pos, end - r->pos);
    if (width == 0) return fail(r, "invalid UTF-8 in a string");
    memcpy(buf + n, r->text + r->pos, width);
    n += width;
    r->pos += width;
  }
  buf[n] = '\0';
  r->pos++;
  *text = (const char *)buf;
  *len = n;
  return 1;
}

static int read_value(Reader *r, JsonValue *out);

/* Reads the key of an object member and the colon after it. */
static int read_key(Reader *r, JsonValue *member) {
  if (peek(r) != '"') return fail(r, "expected a string key");
  if (!read_string(r, &member->key, &member->key_len)) return 0;
  skip_space(r);
  if (peek(r) != ':') return fail(r, "expected :");
  r->pos++;
  skip_space(r);
  return 1;
}

/* Reads the items of an array (CLOSE ']') or the members of an object
   (CLOSE '}') after the open bracket. */
static int read_list(Reader *r, int close, JsonValue *out) {
  const JsonValue **link = &out->first;
  r->pos++;
  skip_space(r);
  int c = peek(r);
  r->pos += c == close;
  while (c != close) {
    JsonValue *item = arena_alloc(r->arena, sizeof *item);
    if (item == NULL) return no_memory(r);
    memset(item, 0, sizeof *item);
    if (close == '}' && !read_key(r, item)) return 0;
    if (!read_value(r, item)) return 0;
    *link = item;
    link = &item->next;
    out->count++;
    skip_space(r);
    c = peek(r);
    if (c != close && c != ',') return fail(r, close == '}' ? "expected , or }" : "expected , or ]");
    r->pos++;
    skip_space(r);
  }
  return 1;
}

static int read_nested(Reader *r, JsonValue *out) {
  int object = peek(r) == '{';
  if (r->depth >= JSON_DEPTH_MAX) return fail(r, "the nesting is too deep");
  out->kind = object ? JSON_OBJECT : JSON_ARRAY;
  r->depth++;
  int ok = read_list(r, object ? '}' : ']', out);
  r->depth--;
  return ok;
}

static int read_value(Reader *r, JsonValue *out) {
  int c = peek(r);
  switch (c) {
    case '{':
    case '[':
      return read_nested(r, out);
    case '"':
      out->kind = JSON_STRING;
      return read_string(r, &out->text, &out->len);
    case 't':
      return read_word(r, "true", JSON_TRUE, out);
    case 'f':
      return read_word(r, "false", JSON_FALSE, out);
    case 'n':
      return read_word(r, "null", JSON_NULL, out);
    default:
      return c == '-' || is_digit(c) ? read_number(r, out) : fail(r, "expected a value");
  }
}

int json_parse(Arena *arena, const char *text, size_t len, JsonValue *out, Diag *diag) {
  Reader r = {arena, text, len, 0, 0, diag};
  memset(out, 0, sizeof *out);
  skip_space(&r);
  if (!read_value(&r, out)) return 0;
  skip_space(&r);
  return r.pos == len ? 1 : fail(&r, "expected the end of the text");
}

const JsonValue *json_member(const JsonValue *object, const char *key) {
  size_t n = strlen(key);
  const JsonValue *m = object != NULL && object->kind == JSON_OBJECT ? object->first : NULL;
  while (m != NULL && !(m->key_len == n && memcmp(m->key, key, n) == 0)) m = m->next;
  return m;
}

const char *json_get_string(const JsonValue *value) {
  return value != NULL && value->kind == JSON_STRING && strlen(value->text) == value->len ? value->text : NULL;
}

/* The MCP server of `langc mcp` (S9 D3, Q-S9-2 (a)). The JSON reader above
   is the quorum-lang reader; this part is the quorum-lang server with the
   leaf-lang tools. */

#include <stdlib.h>

/* build/version.c (D2e). */
extern const char langc_commit[];

/* The arena limit of one request line. */
#define MCP_ARENA_BYTES ((size_t)1 << 30)
#define MCP_PROTOCOL_VERSION "2025-06-18"

static void put_json_bytes(const char *s, size_t len, FILE *out) {
  size_t i;
  fputc('"', out);
  for (i = 0; i < len; i++) {
    unsigned char ch = (unsigned char)s[i];
    switch (ch) {
      case '"':
      case '\\':
        fputc('\\', out);
        fputc(ch, out);
        break;
      default:
        if (ch < 0x20) fprintf(out, "\\u%04x", (unsigned)ch);
        else fputc(ch, out);
        break;
    }
  }
  fputc('"', out);
}

static void put_json_str(const char *s, FILE *out) {
  put_json_bytes(s, strlen(s), out);
}

/* JSON-RPC 2.0 error codes. */
enum {
  RPC_PARSE = -32700,
  RPC_BAD_REQUEST = -32600,
  RPC_UNKNOWN_METHOD = -32601,
  RPC_BAD_PARAMS = -32602,
  RPC_INTERNAL = -32603
};

/* The tools/list result (S9 D3). */
static const char MCP_TOOLS[] =
  "{\"tools\":["
  "{\"name\":\"check\",\"description\":\"Checks the leaf-lang program text in source. The text is the langc check --json lines.\","
  "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\"}},\"required\":[\"source\"]}},"
  "{\"name\":\"fmt\",\"description\":\"Gives the canonical text of the leaf-lang program text in source, or the diagnostic as one JSON line.\","
  "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\"}},\"required\":[\"source\"]}},"
  "{\"name\":\"explain\",\"description\":\"Gives the langc explain --json lines of the leaf-lang program text in source. name selects one declaration.\","
  "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"source\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"}},\"required\":[\"source\"]}},"
  "{\"name\":\"list-base\",\"description\":\"Gives the names of the base signature (PLAN sec 3), one on each line.\","
  "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
  "{\"name\":\"show-law\",\"description\":\"Gives the law id (L1 to L7, PLAN sec 6) as one JSON line: the text, the slice and the check.\","
  "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"string\"}},\"required\":[\"id\"]}}"
  "]}";

/* The laws of PLAN sec 6 for `show-law`: the id, the text, the slice and
   the check that discharges the law (NULL: PLAN sec 6 gives no check). */
static const struct {
  const char *id;
  const char *text;
  const char *slice;
  const char *check;
} MCP_LAWS[] = {
  {"L1", "correctness: for each keystore ks and message m, when next ks gives (l, ks'), verifyAt (leafHeight l) (pk ks) m (sign l m) is true",
   "S4, S5",
   "an eval check at h = 4 on verify pk4 m (signAt sk skPrf pk4 i m) for i = 0 to 15 (test/eval/laws.lang); the keystore form at h = 4 for the positions 0 to 15, and next gives none at 16"},
  {"L2", "nonce binding: accept pk k m s is true implies index s is k", "S6",
   "an eval check at h = 4 (test/eval/laws.lang): for i = 0 to 15, accept pk4 i m (signAt sk skPrf pk4 i m) is true and accept pk4 (i + 1) m (signAt sk skPrf pk4 i m) is false; at the nonce of the signature, a different m gives false"},
  {"L3", "commit-before-sign: in each Durable tree, each sign of a leaf with index i is under a commit node that writes i + 1", "S5",
   "an eval row at h = 4: a guest fold checks that each some (l, ks') is under commit (i + 1) and that index (sign l m) is i"},
  {"L4", "no reset: the committed positions on each path of a Durable tree increase strictly", "S5",
   "by construction: only next makes a commit node"},
  {"L5", "distinct leaves: two signatures that one program makes have different indices (from the affine check and L4)", "S5",
   "a refusal fixture: two sign of one leaf and two next of one keystore are REFUSE_AFFINE_REUSE; an eval row: two signatures of one program have different indices"},
  {"L6", "binding independence: the kernel never unfolds a hash op, so each law that checks holds for each binding", "S4",
   "test/check/hash-node.lang, hash-root.lang and hash-verify.lang refuse with TYPE_REFL, because the hash ops stay stuck in check"},
  {"L7", "aggregation: the zkml statement holds exactly when aggregate is true (at the zkml-lang semantics)", "S7", NULL},
};

/* Writes the id of a request: a string, a number or null. */
static void put_id(const JsonValue *id, FILE *out) {
  if (id != NULL && id->kind == JSON_STRING) {
    put_json_bytes(id->text, id->len, out);
    return;
  }
  fputs(id != NULL && id->kind == JSON_NUMBER ? id->text : "null", out);
}

static void rpc_head(const JsonValue *id, FILE *out) {
  fputs("{\"jsonrpc\":\"2.0\",\"id\":", out);
  put_id(id, out);
}

static void rpc_error(const JsonValue *id, int code, const char *message, FILE *out) {
  rpc_head(id, out);
  fprintf(out, ",\"error\":{\"code\":%d,\"message\":", code);
  put_json_str(message, out);
  fputs("}}\n", out);
}

/* Negotiate the implemented version, including when the client requests
   an unsupported version. */
static void mcp_initialize(McpRun run, const JsonValue *id, const JsonValue *params, FILE *out) {
  (void)run;
  const char *version = json_get_string(json_member(params, "protocolVersion"));
  if (version == NULL) {
    rpc_error(id, RPC_BAD_PARAMS, "Invalid params", out);
    return;
  }
  rpc_head(id, out);
  fputs(",\"result\":{\"protocolVersion\":", out);
  put_json_str(MCP_PROTOCOL_VERSION, out);
  fputs(",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"langc\",\"version\":", out);
  put_json_str(langc_commit, out);
  fputs("}}}\n", out);
}

static void mcp_ping(McpRun run, const JsonValue *id, const JsonValue *params, FILE *out) {
  (void)run;
  (void)params;
  rpc_head(id, out);
  fputs(",\"result\":{}}\n", out);
}

static void mcp_tools_list(McpRun run, const JsonValue *id, const JsonValue *params, FILE *out) {
  (void)run;
  (void)params;
  rpc_head(id, out);
  fputs(",\"result\":", out);
  fputs(MCP_TOOLS, out);
  fputs("}\n", out);
}

/* The `show-law` tool: the law ID as one JSON line, or a USAGE diagnostic.
   Returns 0, or 1 for isError true. */
static int show_law(const char *id, FILE *out) {
  size_t i;
  for (i = 0; i < sizeof MCP_LAWS / sizeof MCP_LAWS[0]; i++) {
    if (strcmp(MCP_LAWS[i].id, id) == 0) {
      fputs("{\"id\":", out);
      put_json_str(MCP_LAWS[i].id, out);
      fputs(",\"text\":", out);
      put_json_str(MCP_LAWS[i].text, out);
      fputs(",\"slice\":", out);
      put_json_str(MCP_LAWS[i].slice, out);
      fputs(",\"check\":", out);
      if (MCP_LAWS[i].check != NULL) put_json_str(MCP_LAWS[i].check, out);
      else fputs("null", out);
      fputs("}\n", out);
      return 0;
    }
  }
  Diag diag;
  diag_init(&diag);
  diag_fail(&diag, "USAGE", NULL, "unknown law %s; the laws are L1 to L7", id);
  diag_print_json(&diag, out);
  return 1;
}

/* Runs the tool NAME with ARGS and writes its text on OUT. Returns 0, 1
   for isError true, or -1 for bad params. */
static int call_tool(McpRun run, const char *name, const JsonValue *args, FILE *out) {
  const JsonValue *source = json_member(args, "source");
  const char *text = json_get_string(source);
  const JsonValue *word = json_member(args, "name");
  const char *entry = json_get_string(word);
  const char *law = json_get_string(json_member(args, "id"));
  if (strcmp(name, "check") == 0) return text != NULL ? run(MCP_TOOL_CHECK, text, source->len, NULL, out) : -1;
  if (strcmp(name, "fmt") == 0) return text != NULL ? run(MCP_TOOL_FMT, text, source->len, NULL, out) : -1;
  if (strcmp(name, "explain") == 0) {
    return text != NULL && (word == NULL || entry != NULL) ? run(MCP_TOOL_EXPLAIN, text, source->len, entry, out) : -1;
  }
  if (strcmp(name, "list-base") == 0) return run(MCP_TOOL_LIST_BASE, NULL, 0, NULL, out);
  if (strcmp(name, "show-law") == 0) return law != NULL ? show_law(law, out) : -1;
  return -1;
}

/* `tools/call` gives one text item with the text of the tool. */
static void mcp_call(McpRun run, const JsonValue *id, const JsonValue *params, FILE *out) {
  const char *name = json_get_string(json_member(params, "name"));
  const JsonValue *args = json_member(params, "arguments");
  if (name == NULL || (args != NULL && args->kind != JSON_OBJECT)) {
    rpc_error(id, RPC_BAD_PARAMS, "Invalid params", out);
    return;
  }
  char *text = NULL;
  size_t len = 0;
  FILE *capture = open_memstream(&text, &len);
  if (capture == NULL) {
    rpc_error(id, RPC_INTERNAL, "Internal error", out);
    return;
  }
  int status = call_tool(run, name, args, capture);
  int closed = fclose(capture) == 0;
  if (status < 0 || !closed) {
    rpc_error(id, closed ? RPC_BAD_PARAMS : RPC_INTERNAL, closed ? "Invalid params" : "Internal error", out);
    free(text);
    return;
  }
  rpc_head(id, out);
  fputs(",\"result\":{\"content\":[{\"type\":\"text\",\"text\":", out);
  put_json_bytes(text != NULL ? text : "", text != NULL ? len : 0, out);
  fprintf(out, "}],\"isError\":%s}}\n", status != 0 ? "true" : "false");
  free(text);
}

typedef void (*McpMethod)(McpRun run, const JsonValue *id, const JsonValue *params, FILE *out);

static const struct {
  const char *name;
  McpMethod run;
} MCP_METHODS[] = {
  {"initialize", mcp_initialize},
  {"ping", mcp_ping},
  {"tools/list", mcp_tools_list},
  {"tools/call", mcp_call},
};

/* An id is absent, a string, a number or null. */
static int id_ok(const JsonValue *id) {
  return id == NULL || id->kind == JSON_NUMBER || id->kind == JSON_NULL || id->kind == JSON_STRING;
}

/* Answers the message in the LEN bytes of LINE on OUT. A notification (a
   request without an id) gets no reply. */
static void mcp_message(McpRun run, Arena *arena, const char *line, size_t len, FILE *out) {
  JsonValue msg;
  Diag diag;
  diag_init(&diag);
  if (!json_parse(arena, line, len, &msg, &diag)) {
    rpc_error(NULL, RPC_PARSE, "Parse error", out);
    return;
  }
  const JsonValue *id = json_member(&msg, "id");
  const JsonValue *params = json_member(&msg, "params");
  const char *version = json_get_string(json_member(&msg, "jsonrpc"));
  const JsonValue *method = json_member(&msg, "method");
  int valid = version != NULL && strcmp(version, "2.0") == 0
    && method != NULL && method->kind == JSON_STRING && id_ok(id)
    && (params == NULL || params->kind == JSON_OBJECT || params->kind == JSON_ARRAY);
  if (!valid) {
    rpc_error(id_ok(id) ? id : NULL, RPC_BAD_REQUEST, "Invalid Request", out);
    return;
  }
  if (id == NULL) return;
  for (size_t i = 0; i < sizeof MCP_METHODS / sizeof MCP_METHODS[0]; i++) {
    if (strlen(MCP_METHODS[i].name) == method->len
        && memcmp(MCP_METHODS[i].name, method->text, method->len) == 0) {
      MCP_METHODS[i].run(run, id, params, out);
      return;
    }
  }
  rpc_error(id, RPC_UNKNOWN_METHOD, "Method not found", out);
}

int mcp_serve(McpRun run, FILE *in, FILE *out) {
  char *line = NULL;
  size_t cap = 0;
  ssize_t got;
  int ok = 1;
  while (ok && (got = getline(&line, &cap, in)) >= 0) {
    size_t len = (size_t)got;
    len -= len > 0 && line[len - 1] == '\n';
    len -= len > 0 && line[len - 1] == '\r';
    Arena arena;
    arena_init(&arena, MCP_ARENA_BYTES);
    if (len > 0) mcp_message(run, &arena, line, len, out);
    arena_release(&arena);
    ok = fflush(out) == 0;
  }
  free(line);
  return ok && !ferror(in) ? 0 : 2;
}
