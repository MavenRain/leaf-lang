/* Poseidon2 over BabyBear, width 16: Poseidon2BabyBear<16> of Plonky3 rev
 * 5fcf8d7c (docs/PLAN.md 5.4). */
#ifndef LANG_POSEIDON2_H
#define LANG_POSEIDON2_H
#include <stddef.h>
#include <stdint.h>

/* BabyBear p = 15 * 2^27 + 1 (baby-bear/src/baby_bear.rs:18). */
#define BABYBEAR_P 0x78000001u

/* Width 16; 4 + 4 external and 13 internal rounds (poseidon2/src/round_numbers.rs:47, :50). */
enum { POSEIDON2_WIDTH = 16, POSEIDON2_HALF_F = 4, POSEIDON2_ROUNDS_P = 13 };

/* The round constants of one instance, as canonical values below p. */
typedef struct Poseidon2Constants {
  uint32_t initial[POSEIDON2_HALF_F][POSEIDON2_WIDTH];
  uint32_t terminal[POSEIDON2_HALF_F][POSEIDON2_WIDTH];
  uint32_t internal[POSEIDON2_ROUNDS_P];
} Poseidon2Constants;

/* The Horizen Labs constants of default_babybear_poseidon2_16() (Q-S7-5). */
extern const Poseidon2Constants poseidon2_babybear16;

/* Permutes the state in place. Each element must be below p. */
void poseidon2_permute(const Poseidon2Constants *rc, uint32_t state[POSEIDON2_WIDTH]);

/* The sponge PaddingFreeSponge<Perm, 16, 8, 8>: rate 8 and 8 output elements (docs/PLAN.md 5.4). */
enum { POSEIDON2_RATE = 8 };

/* The Poseidon2 digest of a 5.4 hash input. SIZE must be a multiple of 32. */
void poseidon2_hash_bytes(const unsigned char *data, size_t size, unsigned char digest[32]);
#endif
