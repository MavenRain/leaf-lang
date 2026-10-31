/* The token baseline tool (S9 D6, Q-S9-5 (a), docs/TOKEN-BASELINE.md).
   It counts each file with a fixed offline proxy: a token is a run of ASCII
   letters, digits and `_`, or one ASCII punctuation character; a non-ASCII
   code point counts as its UTF-8 byte count. Whitespace and control bytes
   are not tokens. The output is a Markdown table of the bytes, the code
   points and the tokens of each file. An argument `--` starts a second
   group of files: then the table has a total row for each group, and the
   last line is the ratio of the tokens (total 2 over total 1). */
#include <stdio.h>
#include <string.h>

#define GROUP_MARK "--"
#define USAGE "usage: tokcount FILE... [-- FILE...]\n"

/* The counts of one file or of one group. */
typedef struct {
  unsigned long bytes;
  unsigned long points;
  unsigned long tokens;
} Count;

static int is_word(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static int is_punct(int c) {
  return c >= 0x21 && c <= 0x7e && !is_word(c);
}

/* Counts the file at PATH into COUNT. Returns 0, or 1 when the file does not read. */
static int count_file(const char *path, Count *count) {
  FILE *f = fopen(path, "rb");
  int c;
  int in_word = 0;
  int bad;
  count->bytes = 0;
  count->points = 0;
  count->tokens = 0;
  if (f == NULL) return 1;
  while ((c = fgetc(f)) != EOF) {
    count->bytes++;
    if (c < 0x80 || c >= 0xc0) count->points++;
    if (is_word(c)) {
      if (!in_word) count->tokens++;
      in_word = 1;
    } else {
      in_word = 0;
      if (is_punct(c) || c >= 0x80) count->tokens++;
    }
  }
  bad = ferror(f);
  fclose(f);
  return bad ? 1 : 0;
}

static void add(Count *total, const Count *c) {
  total->bytes += c->bytes;
  total->points += c->points;
  total->tokens += c->tokens;
}

static void row(const char *name, const Count *c) {
  printf("| %s | %lu | %lu | %lu |\n", name, c->bytes, c->points, c->tokens);
}

/* NUM over DEN, rounded half up to two decimal places. */
static void ratio(unsigned long num, unsigned long den) {
  unsigned long hundredths;
  if (den == 0) {
    printf("\nratio of the tokens (total 2 over total 1): none\n");
    return;
  }
  hundredths = (num * 200u + den) / (den * 2u);
  printf("\nratio of the tokens (total 2 over total 1): %lu.%02lu\n", hundredths / 100u, hundredths % 100u);
}

int main(int argc, char **argv) {
  Count total[2] = {{0, 0, 0}, {0, 0, 0}};
  Count c;
  int split = 0;
  int group = 0;
  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], GROUP_MARK) == 0) {
      if (split != 0) {
        fputs(USAGE, stderr);
        return 2;
      }
      split = i;
    }
  }
  if (argc < 2 || split == 1 || split == argc - 1) {
    fputs(USAGE, stderr);
    return 2;
  }
  printf("| file | bytes | code points | tokens |\n");
  printf("| :--- | ---: | ---: | ---: |\n");
  for (i = 1; i < argc; i++) {
    if (i == split) {
      row("total 1", &total[0]);
      group = 1;
      continue;
    }
    if (count_file(argv[i], &c) != 0) {
      fprintf(stderr, "tokcount: cannot read %s\n", argv[i]);
      return 1;
    }
    row(argv[i], &c);
    add(&total[group], &c);
  }
  if (split == 0) {
    row("total", &total[0]);
    return 0;
  }
  row("total 2", &total[1]);
  ratio(total[1].tokens, total[0].tokens);
  return 0;
}
