/* The build manifest (S8 D2). open_memstream is POSIX 2008 (D2j). */
#define _POSIX_C_SOURCE 200809L

#include "manifest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "front/keccak.h"

static const char MANIFEST_SUFFIX[] = ".manifest.json";

/* The bytes 0x01 to 0x1f (D2i): a word with one of them gives USAGE. */
static const char CONTROL_BYTES[] = "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
                                    "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f";

void manifest_print_laws(const LawCounts *laws, FILE *out) {
  fprintf(out, "{\"parsed\":\"%zu of %zu\",\"typed\":\"%zu of %zu\",\"affine\":\"%zu of %zu\",\"laws\":\"0 of 0\"}",
          laws->done, laws->count, laws->ok, laws->count, laws->ok, laws->count);
}

int out_buffer_open(OutBuffer *buf) {
  buf->data = NULL;
  buf->size = 0;
  buf->file = open_memstream(&buf->data, &buf->size);
  return buf->file != NULL;
}

int out_buffer_close(OutBuffer *buf) {
  int ok = buf->file != NULL && fclose(buf->file) == 0;
  buf->file = NULL;
  return ok && buf->data != NULL;
}

void out_buffer_free(OutBuffer *buf) {
  if (buf->file != NULL) fclose(buf->file);
  free(buf->data);
  buf->file = NULL;
  buf->data = NULL;
  buf->size = 0;
}

/* 1 when TEXT has no byte below 0x20. */
static int text_ok(const char *text) {
  return text[strcspn(text, CONTROL_BYTES)] == '\0';
}

/* Prints TEXT as a JSON string: `"` and `\` get a `\` before them (D2i). */
static void print_string(const char *text, FILE *out) {
  size_t i;
  fputc('"', out);
  for (i = 0; text[i] != '\0'; i++) {
    if (text[i] == '"' || text[i] == '\\') fputc('\\', out);
    fputc(text[i], out);
  }
  fputc('"', out);
}

void manifest_keccak_hex(const unsigned char *data, size_t size, char hex[MANIFEST_HASH_CHARS]) {
  static const char DIGITS[] = "0123456789abcdef";
  unsigned char digest[32];
  size_t i;
  keccak256(data, size, digest);
  hex[0] = '0';
  hex[1] = 'x';
  for (i = 0; i < sizeof digest; i++) {
    hex[2 + 2 * i] = DIGITS[digest[i] >> 4];
    hex[3 + 2 * i] = DIGITS[digest[i] & 15];
  }
  hex[2 + 2 * sizeof digest] = '\0';
}

/* Prints the keccak256 of the SIZE bytes of DATA as a JSON string: 0x and
   64 lowercase hex digits (D2h). */
static void print_keccak(const unsigned char *data, size_t size, FILE *out) {
  char hex[MANIFEST_HASH_CHARS];
  manifest_keccak_hex(data, size, hex);
  fprintf(out, "\"%s\"", hex);
}

/* 1 when word I is OUT_PATH after `-o`. */
static int out_word(const Manifest *m, int i) {
  return i >= 1 && m->words[i] == m->out_path && strcmp(m->words[i - 1], "-o") == 0;
}

/* 1 when word I is in `command`: it is not in the pair -o OUT (D2c). */
static int in_command(const Manifest *m, int i) {
  return !out_word(m, i) && !(i + 1 < m->word_count && out_word(m, i + 1));
}

/* The one line of D2a and its newline. */
static void print_manifest(const Manifest *m, const unsigned char *data, size_t size, FILE *out) {
  const char *sep = "";
  int i;
  fputs("{\"manifest\":1,\"command\":[", out);
  for (i = 0; i < m->word_count; i++) {
    if (in_command(m, i)) {
      fputs(sep, out);
      print_string(m->words[i], out);
      sep = ",";
    }
  }
  fputs("],\"source\":[", out);
  for (i = 0; i < m->source_count; i++) {
    fputs(i == 0 ? "{\"path\":" : ",{\"path\":", out);
    print_string(m->sources[i].path, out);
    fputs(",\"keccak256\":", out);
    print_keccak(m->sources[i].data, m->sources[i].size, out);
    fputc('}', out);
  }
  fputs("],\"langc\":{\"commit\":", out);
  print_string(langc_commit, out);
  fputs(langc_dirty ? ",\"dirty\":true},\"laws\":" : ",\"dirty\":false},\"laws\":", out);
  manifest_print_laws(&m->laws, out);
  fputs(",\"output\":{\"path\":", out);
  print_string(m->out_path, out);
  fprintf(out, ",\"size\":%zu,\"keccak256\":", size);
  print_keccak(data, size, out);
  fputs("}}\n", out);
}

/* Writes the SIZE bytes of DATA to PATH. 1 on success. */
static int write_bytes(const char *path, const char *data, size_t size) {
  FILE *file = fopen(path, "wb");
  if (file == NULL) return 0;
  size_t put = fwrite(data, 1, size, file);
  return fclose(file) == 0 && put == size;
}

static ManifestStatus write_manifest(const Manifest *m, const char *path, const char *data, size_t size) {
  FILE *file = fopen(path, "wb");
  if (file == NULL) return MANIFEST_FILE_IO;
  print_manifest(m, (const unsigned char *)data, size, file);
  int ok = ferror(file) == 0;
  return fclose(file) == 0 && ok ? MANIFEST_OK : MANIFEST_FILE_IO;
}

ManifestStatus manifest_write(const Manifest *m, const char *data, size_t size) {
  int i, ok = text_ok(m->out_path);
  for (i = 0; i < m->word_count; i++) ok = ok && text_ok(m->words[i]);
  if (!ok) return MANIFEST_USAGE;
  size_t len = strlen(m->out_path);
  char *path = malloc(len + sizeof MANIFEST_SUFFIX);
  if (path == NULL) return MANIFEST_NO_MEMORY;
  memcpy(path, m->out_path, len);
  memcpy(path + len, MANIFEST_SUFFIX, sizeof MANIFEST_SUFFIX);
  ManifestStatus status = write_bytes(m->out_path, data, size) ? write_manifest(m, path, data, size) : MANIFEST_OUT_IO;
  free(path);
  return status;
}

/* D3d: the reader. CUR moves over the text; after a mismatch OK is 0 and
   each step does nothing. No step reads the byte at END or past it. */
typedef struct {
  char *at;
  char *end;
  int ok;
} Cursor;

/* Takes the bytes of WORD at CUR. */
static void take(Cursor *cur, const char *word) {
  size_t n = strlen(word);
  cur->ok = cur->ok && (size_t)(cur->end - cur->at) >= n && memcmp(cur->at, word, n) == 0;
  cur->at += cur->ok ? n : 0;
}

/* 1 when the byte at CUR is C. */
static int next_is(const Cursor *cur, char c) {
  return cur->ok && cur->at < cur->end && *cur->at == c;
}

/* Takes a JSON string at CUR into *OUT (D2i): `\"` and `\\` are the only
   escapes, and a byte below 0x20 is not in it. The text stays in place: the
   escapes come out, and a NUL goes at the end. */
static void take_string(Cursor *cur, char **out) {
  char *put;
  take(cur, "\"");
  put = cur->at;
  *out = put;
  while (cur->ok && cur->at < cur->end && *cur->at != '"') {
    int escape = *cur->at == '\\';
    char *from = cur->at + escape;
    cur->ok = from < cur->end && (unsigned char)*from >= 0x20 && (!escape || *from == '"' || *from == '\\');
    if (cur->ok) {
      *put++ = *from;
      cur->at = from + 1;
    }
  }
  take(cur, "\"");
  if (cur->ok) *put = '\0';
}

/* Takes a decimal number at CUR: digits, no leading 0 (D2a prints %zu). */
static void take_size(Cursor *cur, size_t *out) {
  char *first = cur->at;
  size_t n = 0;
  while (cur->ok && cur->at < cur->end && *cur->at >= '0' && *cur->at <= '9') {
    size_t digit = (size_t)(*cur->at - '0');
    cur->ok = n <= (SIZE_MAX - digit) / 10;
    n = n * 10 + digit;
    cur->at++;
  }
  cur->ok = cur->ok && cur->at > first && (cur->at - first == 1 || *first != '0');
  *out = n;
}

/* 1 when TEXT is N lowercase hex digits and no more. */
static int hex_ok(const char *text, size_t n) {
  return strlen(text) == n && strspn(text, "0123456789abcdef") == n;
}

/* Takes a keccak256 string at CUR: 0x and 64 hex digits (D2h). */
static void take_hash(Cursor *cur, char **out) {
  take_string(cur, out);
  cur->ok = cur->ok && strncmp(*out, "0x", 2) == 0 && hex_ok(*out + 2, 64);
}

/* The words that write an OUT and its manifest (D2). */
static const char *const WRITERS[] = {"build", "signer", "verifier", "zkml"};

/* 1 when WORD is one of WRITERS. */
static int writer_ok(const char *word) {
  int found = 0;
  size_t i;
  for (i = 0; i < sizeof WRITERS / sizeof WRITERS[0]; i++) found = found || strcmp(word, WRITERS[i]) == 0;
  return found;
}

/* Takes one `command` word at CUR into OUT, CAP words at most. */
static void take_word(Cursor *cur, ManifestRead *out, size_t cap) {
  char *word = NULL;
  take_string(cur, &word);
  cur->ok = cur->ok && (size_t)out->word_count < cap;
  if (cur->ok) out->words[out->word_count++] = word;
}

/* Takes one `source` object at CUR into OUT, CAP sources at most. */
static void take_source(Cursor *cur, ManifestRead *out, size_t cap) {
  char *path = NULL;
  char *hash = NULL;
  take(cur, "{\"path\":");
  take_string(cur, &path);
  take(cur, ",\"keccak256\":");
  take_hash(cur, &hash);
  take(cur, "}");
  cur->ok = cur->ok && (size_t)out->source_count < cap;
  if (cur->ok) {
    out->source_paths[out->source_count] = path;
    out->source_hashes[out->source_count] = hash;
    out->source_count++;
  }
}

ManifestReadStatus manifest_read(char *text, size_t len, ManifestRead *out) {
  size_t cap = len / 3 + 1; /* a word or a source takes 3 bytes or more */
  char *law = NULL;
  Cursor cur = {text, text + len, 1};
  memset(out, 0, sizeof *out);
  out->words = malloc(3 * cap * sizeof *out->words);
  if (out->words == NULL) return MANIFEST_READ_NO_MEMORY;
  out->source_paths = out->words + cap;
  out->source_hashes = out->words + 2 * cap;
  take(&cur, "{\"manifest\":1,\"command\":[");
  if (!next_is(&cur, ']')) take_word(&cur, out, cap);
  while (next_is(&cur, ',')) {
    take(&cur, ",");
    take_word(&cur, out, cap);
  }
  take(&cur, "],\"source\":[");
  if (!next_is(&cur, ']')) take_source(&cur, out, cap);
  while (next_is(&cur, ',')) {
    take(&cur, ",");
    take_source(&cur, out, cap);
  }
  take(&cur, "],\"langc\":{\"commit\":");
  take_string(&cur, &out->commit);
  take(&cur, ",\"dirty\":");
  out->dirty = next_is(&cur, 't');
  take(&cur, out->dirty ? "true" : "false");
  take(&cur, "},\"laws\":{\"parsed\":");
  take_string(&cur, &law);
  take(&cur, ",\"typed\":");
  take_string(&cur, &law);
  take(&cur, ",\"affine\":");
  take_string(&cur, &law);
  take(&cur, ",\"laws\":");
  take_string(&cur, &law);
  take(&cur, "},\"output\":{\"path\":");
  take_string(&cur, &out->out_path);
  take(&cur, ",\"size\":");
  take_size(&cur, &out->out_size);
  take(&cur, ",\"keccak256\":");
  take_hash(&cur, &out->out_hash);
  take(&cur, "}}\n");
  cur.ok = cur.ok && cur.at == cur.end && out->word_count >= 1 && writer_ok(out->words[0]) &&
           (strcmp(out->commit, "unknown") == 0 || hex_ok(out->commit, 40));
  if (cur.ok) return MANIFEST_READ_OK;
  manifest_read_free(out);
  return MANIFEST_READ_SHAPE;
}

void manifest_read_free(ManifestRead *r) {
  free(r->words); /* one block: the words, the source paths and hashes */
  memset(r, 0, sizeof *r);
}
