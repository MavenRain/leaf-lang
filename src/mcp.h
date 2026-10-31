/* `langc mcp` (S9 D3, Q-S9-2 (a)): a small JSON reader (the quorum-lang
   reader, verbatim) and the MCP stdio server. mcp.c does not call main.c:
   main.c gives the tools that need the front through McpRun. SHARED by the
   tcc kits. */
#ifndef LANG_MCP_H
#define LANG_MCP_H

#include <stddef.h>
#include <stdio.h>

#include "front/base.h"

/* The deepest nesting of arrays and objects that json_parse accepts. */
#define JSON_DEPTH_MAX 64

typedef enum {
  JSON_NULL,
  JSON_FALSE,
  JSON_TRUE,
  JSON_NUMBER,
  JSON_STRING,
  JSON_ARRAY,
  JSON_OBJECT
} JsonKind;

/* One JSON value in the arena. The items of an array and the members of
   an object are a list through NEXT. */
typedef struct JsonValue JsonValue;
struct JsonValue {
  JsonKind kind;
  const char *text;       /* STRING: the decoded UTF-8 bytes. NUMBER: the source text. NUL-terminated. */
  size_t len;             /* the byte count of TEXT, without the NUL */
  const JsonValue *first; /* ARRAY, OBJECT: the first item or member, or NULL */
  size_t count;           /* ARRAY, OBJECT: the count of items or members */
  const char *key;        /* a member of an object: the decoded key, NUL-terminated */
  size_t key_len;
  const JsonValue *next;  /* the next item or member, or NULL */
};

/* Reads the LEN bytes of TEXT as one JSON value (RFC 8259) with optional
   white space around it. Returns 1, or 0 after a USAGE or OOM diagnostic. */
int json_parse(Arena *arena, const char *text, size_t len, JsonValue *out, Diag *diag);
/* The first member KEY of OBJECT, or NULL when OBJECT is not an object or
   has no member KEY. */
const JsonValue *json_member(const JsonValue *object, const char *key);
/* The text of VALUE when VALUE is a string without a NUL byte, else NULL. */
const char *json_get_string(const JsonValue *value);

/* The tools that need the front (S9 D3). show-law is in mcp.c. */
typedef enum { MCP_TOOL_CHECK, MCP_TOOL_FMT, MCP_TOOL_EXPLAIN, MCP_TOOL_LIST_BASE } McpTool;

/* Runs TOOL on the LEN bytes of the program TEXT (NULL for list-base) and
   the declaration NAME (NULL: all), and writes the tool text on OUT.
   Returns 0, or 1 for isError true. */
typedef int (*McpRun)(McpTool tool, const char *text, size_t len, const char *name, FILE *out);

/* The MCP stdio transport: one JSON-RPC 2.0 message on each line of IN,
   one reply line on OUT, no Content-Length frame. An empty line and a
   notification get no reply. Returns 0 at EOF, or 2 after an IO error. */
int mcp_serve(McpRun run, FILE *in, FILE *out);

#endif
