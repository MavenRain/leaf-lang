/* `langc mcp` review (S9 D4, Q-S9-6 (a)). Fixed rows: the replies to
   test/mcp/requests.jsonl are the bytes of test/mcp/responses.jsonl, with
   @COMMIT@ = langc_commit. Computed rows: one `tools/call check` for each
   examples/NAME.lang and examples/graded/NAME.lang; isError is false and the last
   line of the text is the last line of `langc check --json FILE`. */
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/mcp.h"

extern const char langc_commit[];

#define REVIEW_ARENA_BYTES (64u * 1024u * 1024u)
#define COMMIT_MARK "@COMMIT@"
#define REQUEST_PATH "build/mcp-review.jsonl"
#define REQUEST_HEAD "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"check\",\"arguments\":{\"source\":"

/* A byte buffer on the heap, NUL-terminated after LEN when DATA is set. */
typedef struct {
  char *data;
  size_t len;
  size_t cap;
} Bytes;

static int bytes_put(Bytes *b, const char *s, size_t n) {
  if (b->len + n + 1 > b->cap) {
    size_t cap = (b->len + n + 1) * 2;
    char *data = realloc(b->data, cap);
    if (data == NULL) return 0;
    b->data = data;
    b->cap = cap;
  }
  if (n > 0) memcpy(b->data + b->len, s, n);
  b->len += n;
  b->data[b->len] = '\0';
  return 1;
}

static void bytes_free(Bytes *b) {
  free(b->data);
  b->data = NULL;
  b->len = 0;
  b->cap = 0;
}

static int read_stream(FILE *f, Bytes *b) {
  char chunk[4096];
  size_t n;
  int ok = 1;
  while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) ok = bytes_put(b, chunk, n) && ok;
  return ok && !ferror(f);
}

static int read_file(const char *path, Bytes *b) {
  FILE *f = fopen(path, "rb");
  int ok;
  if (f == NULL) return 0;
  ok = read_stream(f, b);
  return fclose(f) == 0 && ok;
}

static int write_file(const char *path, const Bytes *b) {
  FILE *f = fopen(path, "wb");
  int ok;
  if (f == NULL) return 0;
  ok = b->len == 0 || fwrite(b->data, 1, b->len, f) == b->len;
  return fclose(f) == 0 && ok;
}

/* The stdout of CMD; 1 when CMD exits 0. */
static int run_cmd(const char *cmd, Bytes *b) {
  FILE *p = popen(cmd, "r");
  int ok;
  if (p == NULL) return 0;
  ok = read_stream(p, b);
  return pclose(p) == 0 && ok;
}

static size_t line_count(const Bytes *b) {
  size_t count = 0;
  size_t i;
  for (i = 0; i < b->len; i++) count += b->data[i] == '\n';
  return count;
}

/* The last line of the N bytes at S, without its newline: its length, and
   its first byte in START. */
static size_t last_line(const char *s, size_t n, size_t *start) {
  size_t end = n > 0 && s[n - 1] == '\n' ? n - 1 : n;
  size_t i = end;
  while (i > 0 && s[i - 1] != '\n') i--;
  *start = i;
  return end - i;
}

/* One byte of a JSON string body. */
static int put_json_byte(Bytes *b, unsigned char c) {
  char esc[8];
  if (c == '"' || c == '\\') {
    esc[0] = '\\';
    esc[1] = (char)c;
    return bytes_put(b, esc, 2);
  }
  if (c < 0x20) {
    snprintf(esc, sizeof esc, "\\u%04x", (unsigned)c);
    return bytes_put(b, esc, 6);
  }
  esc[0] = (char)c;
  return bytes_put(b, esc, 1);
}

static int put_json_text(Bytes *b, const char *s, size_t n) {
  int ok = bytes_put(b, "\"", 1);
  size_t i;
  for (i = 0; i < n && ok; i++) ok = put_json_byte(b, (unsigned char)s[i]);
  return ok && bytes_put(b, "\"", 1);
}

/* The want bytes: test/mcp/responses.jsonl with COMMIT_MARK = langc_commit. */
static int want_fixed(Bytes *want) {
  Bytes raw = {NULL, 0, 0};
  int ok = read_file("test/mcp/responses.jsonl", &raw) && raw.data != NULL &&
           strstr(raw.data, COMMIT_MARK) != NULL;
  const char *start = raw.data;
  const char *at;
  while (ok && (at = strstr(start, COMMIT_MARK)) != NULL) {
    ok = bytes_put(want, start, (size_t)(at - start)) &&
         bytes_put(want, langc_commit, strlen(langc_commit));
    start = at + strlen(COMMIT_MARK);
  }
  if (ok) ok = bytes_put(want, start, strlen(start));
  bytes_free(&raw);
  return ok;
}

static int fixed_rows(size_t *count) {
  Bytes want = {NULL, 0, 0};
  Bytes got = {NULL, 0, 0};
  int ok = want_fixed(&want) && run_cmd("build/langc mcp <test/mcp/requests.jsonl", &got);
  ok = ok && got.len == want.len && memcmp(got.data, want.data, want.len) == 0;
  *count = line_count(&want);
  if (!ok) fprintf(stderr, "mcp-review: the replies to test/mcp/requests.jsonl are not test/mcp/responses.jsonl\n");
  bytes_free(&want);
  bytes_free(&got);
  return ok;
}

/* The text of the one content item of REPLY, or NULL; IS_ERROR is 0 only
   when isError is false. */
static const char *reply_text(Arena *arena, const Bytes *reply, int *is_error) {
  JsonValue v;
  Diag diag;
  const JsonValue *result;
  const JsonValue *flag;
  const JsonValue *content;
  diag_init(&diag);
  *is_error = 1;
  if (reply->data == NULL || !json_parse(arena, reply->data, reply->len, &v, &diag)) return NULL;
  result = json_member(&v, "result");
  if (result == NULL) return NULL;
  flag = json_member(result, "isError");
  content = json_member(result, "content");
  *is_error = flag == NULL || flag->kind != JSON_FALSE;
  if (content == NULL || content->kind != JSON_ARRAY || content->first == NULL) return NULL;
  return json_get_string(json_member(content->first, "text"));
}

static int example_row(Arena *arena, const char *path) {
  Bytes src = {NULL, 0, 0};
  Bytes req = {NULL, 0, 0};
  Bytes reply = {NULL, 0, 0};
  Bytes counts = {NULL, 0, 0};
  char cmd[1100];
  const char *text = NULL;
  int is_error = 1;
  size_t got_start = 0;
  size_t want_start = 0;
  size_t got_len = 0;
  size_t want_len = 0;
  int ok = read_file(path, &src) && bytes_put(&req, REQUEST_HEAD, strlen(REQUEST_HEAD)) &&
           put_json_text(&req, src.data, src.len) && bytes_put(&req, "}}}\n", 4) && write_file(REQUEST_PATH, &req) &&
           run_cmd("build/langc mcp <" REQUEST_PATH, &reply);
  snprintf(cmd, sizeof cmd, "build/langc check --json %s", path);
  ok = ok && run_cmd(cmd, &counts);
  if (ok) text = reply_text(arena, &reply, &is_error);
  if (text != NULL) got_len = last_line(text, strlen(text), &got_start);
  if (counts.data != NULL) want_len = last_line(counts.data, counts.len, &want_start);
  ok = ok && text != NULL && !is_error && got_len > 0 && got_len == want_len &&
       memcmp(text + got_start, counts.data + want_start, got_len) == 0;
  if (!ok) fprintf(stderr, "mcp-review: %s: the check reply is not the langc check --json counts\n", path);
  bytes_free(&src);
  bytes_free(&req);
  bytes_free(&reply);
  bytes_free(&counts);
  return ok;
}

/* Each DIR/NAME.lang; a DIR that does not exist has 0 rows. */
static int example_dir(Arena *arena, const char *dir, size_t *count) {
  DIR *d = opendir(dir);
  struct dirent *e;
  int ok = 1;
  if (d == NULL) return 1;
  while ((e = readdir(d)) != NULL) {
    size_t n = strlen(e->d_name);
    char path[1024];
    if (n < 5 || strcmp(e->d_name + n - 5, ".lang") != 0) continue;
    snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
    ok = example_row(arena, path) && ok;
    (*count)++;
  }
  closedir(d);
  return ok;
}

int main(void) {
  Arena arena;
  size_t fixed = 0;
  size_t examples = 0;
  int ok;
  arena_init(&arena, REVIEW_ARENA_BYTES);
  ok = fixed_rows(&fixed);
  ok = example_dir(&arena, "examples", &examples) && ok;
  ok = example_dir(&arena, "examples/graded", &examples) && ok;
  printf("mcp: %zu checked (%zu fixed, %zu examples)\n", fixed + examples, fixed, examples);
  return ok ? 0 : 1;
}
