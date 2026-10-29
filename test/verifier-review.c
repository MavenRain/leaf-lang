/* Exercise the ERC-4337 decoder with a real signature ending in zero. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "target.h"

enum { CAPACITY = 8192, SIG_OFFSET = 516, SIG_LEN = 2312, TUPLE_OFFSET = 100, LENGTH_OFFSET = 484 };
static const char *entry_point = "0x0000000071727de22e5e9d8baf0edac6f37da032";
static char runtime_path[4096], input_path[4096];
static unsigned rows;

static void word(unsigned char *out, unsigned n) {
  memset(out, 0, 32);
  out[30] = (unsigned char)(n >> 8);
  out[31] = (unsigned char)n;
}

static void run_case(const char *name, const unsigned char *input, size_t len, unsigned want) {
  FILE *file = fopen(input_path, "w");
  assert(file != NULL);
  for (size_t i = 0; i < len; i++) assert(fprintf(file, "%02x", input[i]) == 2);
  assert(fputc('\n', file) != EOF);
  assert(fclose(file) == 0);
  int stream[2];
  assert(pipe(stream) == 0);
  pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    close(stream[0]);
    if (dup2(stream[1], STDOUT_FILENO) < 0 || dup2(stream[1], STDERR_FILENO) < 0) _exit(126);
    close(stream[1]);
    execlp("evm", "evm", "run", "--codefile", runtime_path, "--inputfile", input_path,
           "--sender", entry_point, (char *)NULL);
    _exit(127);
  }
  close(stream[1]);
  char got[1024], expected[80];
  size_t used = 0;
  for (;;) {
    assert(used < sizeof got - 1);
    ssize_t n = read(stream[0], got + used, sizeof got - 1 - used);
    if (n < 0 && errno == EINTR) continue;
    assert(n >= 0);
    if (n == 0) break;
    used += (size_t)n;
  }
  got[used] = '\0';
  close(stream[0]);
  int status;
  while (waitpid(child, &status, 0) < 0) assert(errno == EINTR);
  snprintf(expected, sizeof expected, "0x%064u\n", want);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || strcmp(got, expected) != 0) {
    fprintf(stderr, "FAIL verifier ABI %s: want word %u, got %s", name, want, got);
    exit(1);
  }
  rows++;
}

int main(int argc, char **argv) {
  assert(argc == 4);
  unsigned char pk[72], input[CAPACITY], changed[CAPACITY];
  FILE *file = fopen(argv[1], "rb");
  assert(file != NULL && fread(pk, 1, sizeof pk, file) == sizeof pk);
  assert(fgetc(file) == EOF && fclose(file) == 0);
  file = fopen(argv[2], "r");
  assert(file != NULL);
  size_t len = 0;
  unsigned byte;
  while (fscanf(file, "%2x", &byte) == 1) {
    assert(len < sizeof input);
    input[len++] = (unsigned char)byte;
  }
  assert(feof(file) && fclose(file) == 0);
  assert(len == 2852 && input[SIG_OFFSET + SIG_LEN - 1] == 0);
  assert(snprintf(runtime_path, sizeof runtime_path, "%s/abi-runtime.hex", argv[3]) < (int)sizeof runtime_path);
  assert(snprintf(input_path, sizeof input_path, "%s/abi-input.hex", argv[3]) < (int)sizeof input_path);
  file = fopen(runtime_path, "w");
  assert(file != NULL && target_write_verifier(4, pk, VERIFIER_4337, file, stderr));
  assert(fclose(file) == 0);

  run_case("complete", input, len, 0);
  run_case("without unused ABI padding", input, SIG_OFFSET + SIG_LEN, 0);
  /* CALLDATACOPY must not supply the signature's last zero byte. */
  run_case("missing signature zero byte", input, SIG_OFFSET + SIG_LEN - 1, 1);
  run_case("short tuple", input, TUPLE_OFFSET + 9 * 32 - 1, 1);
  memcpy(changed, input, len);
  memset(changed + 4, 0xff, 32);
  run_case("tuple offset overflow", changed, len, 1);
  memcpy(changed, input, len);
  memset(changed + TUPLE_OFFSET + 8 * 32, 0xff, 32);
  run_case("signature offset overflow", changed, len, 1);
  memcpy(changed, input, len);
  changed[TUPLE_OFFSET + 8 * 32] = 0x10;
  run_case("signature offset outside calldata", changed, len, 1);
  memcpy(changed, input, len);
  memmove(changed + LENGTH_OFFSET + 32, changed + LENGTH_OFFSET, len - LENGTH_OFFSET);
  memset(changed + LENGTH_OFFSET, 0, 32);
  word(changed + TUPLE_OFFSET + 8 * 32, 416);
  run_case("relocated signature", changed, len + 32, 0);
  memcpy(changed, input, len);
  memmove(changed + TUPLE_OFFSET + 32, changed + TUPLE_OFFSET, len - TUPLE_OFFSET);
  memset(changed + TUPLE_OFFSET, 0, 32);
  word(changed + 4, 128);
  run_case("relocated tuple", changed, len + 32, 0);
  printf("verifier ABI regressions: %u passed\n", rows);
  return 0;
}
