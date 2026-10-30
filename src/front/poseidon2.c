/* Poseidon2 over BabyBear, width 16, S-box x^7: Poseidon2BabyBear<16> of
 * Plonky3 rev 5fcf8d7c. The file:line references are to that rev. The values
 * are canonical field elements below p, not the Montgomery form of Plonky3. */
#include "front/poseidon2.h"

enum { WIDTH = POSEIDON2_WIDTH };

static uint32_t add(uint32_t a, uint32_t b) {
  uint32_t sum = a + b;
  return sum >= BABYBEAR_P ? sum - BABYBEAR_P : sum;
}

static uint32_t sub(uint32_t a, uint32_t b) {
  return a >= b ? a - b : a + (BABYBEAR_P - b);
}

static uint32_t mul(uint32_t a, uint32_t b) {
  return (uint32_t)((uint64_t)a * b % BABYBEAR_P);
}

/* x / 2: p is odd, so an odd x becomes (x + p) / 2 (x + p is below 2^32). */
static uint32_t halve(uint32_t x) {
  return (x & 1u) ? (x + BABYBEAR_P) >> 1 : x >> 1;
}

/* The S-box x^7 (D = 7: baby-bear/src/poseidon2.rs:35). */
static uint32_t sbox(uint32_t x) {
  uint32_t x2 = mul(x, x);
  return mul(mul(x2, x), mul(x2, x2));
}

/* M4 = MDSMat4 (poseidon2/src/external.rs:60-74): row i of M4 x is the sum
 * of x, plus x[i], plus 2 x[i + 1] (indices mod 4). */
static void mat4(uint32_t x[4]) {
  uint32_t sum = add(add(x[0], x[1]), add(x[2], x[3]));
  uint32_t y[4];
  for (unsigned i = 0; i < 4; i++)
    y[i] = add(add(sum, x[i]), add(x[(i + 1) % 4], x[(i + 1) % 4]));
  for (unsigned i = 0; i < 4; i++)
    x[i] = y[i];
}

/* The external linear layer circ(2 M4, M4, M4, M4): M4 on each chunk of 4,
 * then each element adds the sum of its column (poseidon2/src/external.rs:135-148). */
static void external_linear(uint32_t state[WIDTH]) {
  uint32_t sums[4] = {0, 0, 0, 0};
  for (unsigned j = 0; j < WIDTH; j += 4)
    mat4(state + j);
  for (unsigned i = 0; i < WIDTH; i++)
    sums[i % 4] = add(sums[i % 4], state[i]);
  for (unsigned i = 0; i < WIDTH; i++)
    state[i] = add(state[i], sums[i % 4]);
}

/* Each external round adds the constants and applies the S-box to each element
 * (poseidon2/src/generic.rs:24), then the linear layer (external.rs:299-305). */
static void external_rounds(uint32_t state[WIDTH], const uint32_t rc[POSEIDON2_HALF_F][WIDTH]) {
  for (unsigned r = 0; r < POSEIDON2_HALF_F; r++) {
    for (unsigned i = 0; i < WIDTH; i++)
      state[i] = sbox(add(state[i], rc[r][i]));
    external_linear(state);
  }
}

/* The internal matrix 1 + Diag(V), V_i = (negative ? -1 : 1) * factor / 2^shift:
 * V = [-2, 1, 2, 1/2, 3, 4, -1/2, -3, -4, 1/2^8, 1/4, 1/8, 1/2^27, -1/2^8,
 * -1/16, -1/2^27] (baby-bear/src/poseidon2.rs:11, :219-244). */
static const struct { unsigned negative, factor, shift; } diagonal[WIDTH] = {
  {1, 2, 0}, {0, 1, 0}, {0, 2, 0}, {0, 1, 1}, {0, 3, 0}, {0, 4, 0}, {1, 1, 1}, {1, 3, 0},
  {1, 4, 0}, {0, 1, 8}, {0, 1, 2}, {0, 1, 3}, {0, 1, 27}, {1, 1, 8}, {1, 1, 4}, {1, 1, 27}
};

/* One internal round (monty-31/src/poseidon2.rs:76-85): the constant and the
 * S-box on element 0 only, then s_i = full + V_i s_i, with full the sum of the
 * state. For element 0 this is full - 2 s_0 = part - s_0, as in the source. */
static void internal_round(uint32_t state[WIDTH], uint32_t rc) {
  uint32_t full = 0;
  state[0] = sbox(add(state[0], rc));
  for (unsigned i = 0; i < WIDTH; i++)
    full = add(full, state[i]);
  for (unsigned i = 0; i < WIDTH; i++) {
    uint32_t scaled = mul(state[i], diagonal[i].factor);
    for (unsigned k = 0; k < diagonal[i].shift; k++)
      scaled = halve(scaled);
    state[i] = diagonal[i].negative ? sub(full, scaled) : add(full, scaled);
  }
}

/* The initial linear layer, 4 external rounds, 13 internal rounds, then 4
 * external rounds (poseidon2/src/lib.rs:107-110, external.rs:332-335). */
void poseidon2_permute(const Poseidon2Constants *rc, uint32_t state[POSEIDON2_WIDTH]) {
  external_linear(state);
  external_rounds(state, rc->initial);
  for (unsigned r = 0; r < POSEIDON2_ROUNDS_P; r++)
    internal_round(state, rc->internal[r]);
  external_rounds(state, rc->terminal);
}

/* One block of the sponge over the Horizen Labs instance: overwrite the 8 rate
 * elements, then permute (symmetric/src/sponge.rs:43-45, :53). */
static void absorb(uint32_t state[WIDTH], const uint32_t block[POSEIDON2_RATE]) {
  for (unsigned i = 0; i < POSEIDON2_RATE; i++)
    state[i] = block[i];
  poseidon2_permute(&poseidon2_babybear16, state);
}

/* PaddingFreeSponge::hash_iter (symmetric/src/sponge.rs:32-57) on the limbs of
 * the input. The state starts at zero (:37). Each 32-byte part is 16
 * big-endian 16-bit limbs (docs/PLAN.md 5.4), so 16 bytes fill one block. The
 * output e0..e7 packs as w_j = e_{2j} * 2^32 + e_{2j+1}, w0 first, each w_j
 * big-endian (5.4): thus e_k is the 4 big-endian bytes at 4k. */
void poseidon2_hash_bytes(const unsigned char *data, size_t size, unsigned char digest[32]) {
  uint32_t state[WIDTH] = {0};
  for (size_t block = 0; block < size; block += 2 * POSEIDON2_RATE) {
    uint32_t limbs[POSEIDON2_RATE];
    for (unsigned i = 0; i < POSEIDON2_RATE; i++)
      limbs[i] = (uint32_t)data[block + 2 * i] << 8 | data[block + 2 * i + 1];
    absorb(state, limbs);
  }
  for (size_t i = 0; i < 32; i++)
    digest[i] = (unsigned char)(state[i / 4] >> (8 * (3 - i % 4)));
}

/* The Horizen Labs constants of default_babybear_poseidon2_16() (Q-S7-5):
 * BABYBEAR_RC16_EXTERNAL_INITIAL (baby-bear/src/poseidon2.rs:60-81),
 * BABYBEAR_RC16_EXTERNAL_FINAL (:86-107), BABYBEAR_RC16_INTERNAL (:112-115). */
const Poseidon2Constants poseidon2_babybear16 = {
  {
    {0x69cbb6afu, 0x46ad93f9u, 0x60a00f4eu, 0x6b1297cdu, 0x23189afeu, 0x732e7befu, 0x72c246deu, 0x2c941900u,
     0x0557eedeu, 0x1580496fu, 0x3a3ea77bu, 0x54f3f271u, 0x0f49b029u, 0x47872fe1u, 0x221e2e36u, 0x1ab7202eu},
    {0x487779a6u, 0x3851c9d8u, 0x38dc17c0u, 0x209f8849u, 0x268dcee8u, 0x350c48dau, 0x5b9ad32eu, 0x0523272bu,
     0x3f89055bu, 0x01e894b2u, 0x13ddeddeu, 0x1b2ef334u, 0x7507d8b4u, 0x6ceeb94eu, 0x52eb6ba2u, 0x50642905u},
    {0x05453f3fu, 0x06349efcu, 0x6922787cu, 0x04bfff9cu, 0x768c714au, 0x3e9ff21au, 0x15737c9cu, 0x2229c807u,
     0x0d47f88cu, 0x097e0eccu, 0x27eadba0u, 0x2d7d29e4u, 0x3502aaa0u, 0x0f475fd7u, 0x29fbda49u, 0x018afffdu},
    {0x0315b618u, 0x6d4497d1u, 0x1b171d9eu, 0x52861abdu, 0x2e5d0501u, 0x3ec8646cu, 0x6e5f250au, 0x148ae8e6u,
     0x17f5fa4au, 0x3e66d284u, 0x0051aa3bu, 0x483f7913u, 0x2cfe5f15u, 0x023427cau, 0x2cc78315u, 0x1e36ea47u}
  },
  {
    {0x7290a80du, 0x6f7e5329u, 0x598ec8a8u, 0x76a859a0u, 0x6559e868u, 0x657b83afu, 0x13271d3fu, 0x1f876063u,
     0x0aeeae37u, 0x706e9ca6u, 0x46400ceeu, 0x72a05c26u, 0x2c589c9eu, 0x20bd37a7u, 0x6a2d3d10u, 0x20523767u},
    {0x5b8fe9c4u, 0x2aa501d6u, 0x1e01ac3eu, 0x1448bc54u, 0x5ce5ad1cu, 0x4918a14du, 0x2c46a83fu, 0x4fcf6876u,
     0x61d8d5c8u, 0x6ddf4ff9u, 0x11fda4d3u, 0x02933a8fu, 0x170eaf81u, 0x5a9c314fu, 0x49a12590u, 0x35ec52a1u},
    {0x58eb1611u, 0x5e481e65u, 0x367125c9u, 0x0eba33bau, 0x1fc28dedu, 0x066399adu, 0x0cbec0eau, 0x75fd1af0u,
     0x50f5bf4eu, 0x643d5f41u, 0x6f4fe718u, 0x5b3cbbdeu, 0x1e3afb3eu, 0x296fb027u, 0x45e1547bu, 0x4a8db2abu},
    {0x59986d19u, 0x30bcdfa3u, 0x1db63932u, 0x1d7c2824u, 0x53b33681u, 0x0673b747u, 0x038a98a3u, 0x2c5bce60u,
     0x351979cdu, 0x5008fb73u, 0x547bca78u, 0x711af481u, 0x3f93bf64u, 0x644d987bu, 0x3c8bcd87u, 0x608758b8u}
  },
  {0x5a8053c0u, 0x693be639u, 0x3858867du, 0x19334f6bu, 0x128f0fd8u, 0x4e2b1ccbu, 0x61210ce0u,
   0x3c318939u, 0x0b5b2f22u, 0x2edb11d5u, 0x213effdfu, 0x0cac4606u, 0x241af16du}
};
