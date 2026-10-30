/* The build manifest (S8 D2): langc writes OUT.manifest.json after the OUT
   of each writer (build, signer, verifier, zkml). */
#ifndef MANIFEST_H
#define MANIFEST_H

#include <stddef.h>
#include <stdio.h>

/* build/version.c (D2e): the git commit of the tree that built langc
   (`unknown` out of a git tree), and 1 when that tree had a change (1 out
   of a git tree). */
extern const char langc_commit[];
extern const int langc_dirty;

/* The counts of the last line of `check --json` (D2f). */
typedef struct {
  size_t done;
  size_t count;
  size_t ok;
} LawCounts;

/* One input of the output (D2d): PATH as given and its SIZE bytes. */
typedef struct {
  const char *path;
  const unsigned char *data;
  size_t size;
} ManifestSource;

/* The fields of one manifest (D2a). OUT_PATH is one of the WORDS: the
   pair `-o OUT_PATH` is not in `command` (D2c). */
typedef struct {
  char *const *words;
  int word_count;
  const char *out_path;
  const ManifestSource *sources;
  int source_count;
  LawCounts laws;
} Manifest;

/* An output in memory (D2j): FILE writes into DATA (open_memstream). */
typedef struct {
  FILE *file;
  char *data;
  size_t size;
} OutBuffer;

typedef enum {
  MANIFEST_OK,
  MANIFEST_USAGE, /* a word has a byte below 0x20 (D2i) */
  MANIFEST_NO_MEMORY,
  MANIFEST_OUT_IO,
  MANIFEST_FILE_IO
} ManifestStatus;

/* Prints the `laws` object, `{"parsed":"a of b",...}`, with no newline. */
void manifest_print_laws(const LawCounts *laws, FILE *out);

/* Opens BUF->file on memory. 1 on success. */
int out_buffer_open(OutBuffer *buf);

/* Closes BUF->file. 1 when BUF->data holds the BUF->size bytes. */
int out_buffer_close(OutBuffer *buf);

/* Closes BUF->file if it is open, and frees the bytes. */
void out_buffer_free(OutBuffer *buf);

/* Writes M->out_path from the SIZE bytes of DATA, then
   M->out_path.manifest.json. After MANIFEST_USAGE neither file is
   written; after MANIFEST_FILE_IO only OUT is written. */
ManifestStatus manifest_write(const Manifest *m, const char *data, size_t size);

/* 0x, 64 hex digits and a NUL (D2h). */
#define MANIFEST_HASH_CHARS 67

/* Writes the keccak256 of the SIZE bytes of DATA into HEX: 0x and 64
   lowercase hex digits (D2h). */
void manifest_keccak_hex(const unsigned char *data, size_t size, char hex[MANIFEST_HASH_CHARS]);

/* One manifest that manifest_read read (D3d). The strings point into the
   text of manifest_read, with the D2i escapes taken out. */
typedef struct {
  char **words; /* the `command` words; WORDS[0] is a writer word */
  int word_count;
  char **source_paths;
  char **source_hashes; /* 0x and 64 hex digits (D2h) */
  int source_count;
  char *commit; /* 40 hex digits or `unknown` */
  int dirty;
  char *out_path;
  size_t out_size;
  char *out_hash;
} ManifestRead;

typedef enum {
  MANIFEST_READ_OK,
  MANIFEST_READ_SHAPE, /* the text is not one D2a line */
  MANIFEST_READ_NO_MEMORY
} ManifestReadStatus;

/* Reads the LEN bytes of TEXT as one D2a line into OUT (D3d). It reads no
   byte past LEN, and it changes TEXT: the strings of OUT stay in it. After
   MANIFEST_READ_OK, free OUT with manifest_read_free. */
ManifestReadStatus manifest_read(char *text, size_t len, ManifestRead *out);

/* Frees the arrays of R. */
void manifest_read_free(ManifestRead *r);

#endif
