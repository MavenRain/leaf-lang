# Builds the tcc-wasm kit with TinyCC. See README.md.
TCC ?= tcc
CC ?= cc
CFLAGS = -std=c99 -Wall -Werror -Isrc
CLANG_FLAGS = -std=c99 -Wall -Wextra -Wswitch-enum -Werror -fsyntax-only -Isrc
FRONT = src/front/base.c src/front/lexer.c src/front/parser.c src/front/fmt.c src/front/front.c src/front/eval.c src/front/check.c src/front/lower.c src/front/keccak.c
TARGET = src/wasm.c src/asm.c src/evm.c
SRC = src/main.c $(FRONT) $(TARGET)
FRONT_TOOL = test/fronttool.c $(filter-out src/front/front.c src/front/lower.c,$(FRONT))
VERIFIER_REVIEW = test/verifier-review.c src/asm.c src/evm.c
HEADERS = $(wildcard src/*.h src/front/*.h)

.PHONY: build check check-clang clean gas

build: build/langc build/fronttool build/verifier-review

build/domain.c: domain/domain.lang gen/embed.c
	mkdir -p build
	$(TCC) -run gen/embed.c domain/domain.lang build/domain.c

build/langc: $(SRC) $(HEADERS) build/domain.c
	$(TCC) $(CFLAGS) -o build/langc $(SRC) build/domain.c

build/fronttool: $(FRONT_TOOL) $(HEADERS)
	mkdir -p build
	$(TCC) $(CFLAGS) -o build/fronttool $(FRONT_TOOL)

build/verifier-review: $(VERIFIER_REVIEW) $(HEADERS)
	mkdir -p build
	$(TCC) $(CFLAGS) -o build/verifier-review $(VERIFIER_REVIEW)

check-clang: build/domain.c
	$(CC) $(CLANG_FLAGS) $(SRC) build/domain.c
	$(CC) $(CLANG_FLAGS) $(FRONT_TOOL)
	$(CC) $(CLANG_FLAGS) $(VERIFIER_REVIEW)

check: build/langc build/fronttool build/verifier-review check-clang
	sh test/gate.sh
	sh test/review.sh
	build/fronttool

# The S6 gas rows (docs/PLAN.md 5.1). GAS_H sets the heights (default 4 10 16).
gas: build/langc
	sh test/gas.sh $(GAS_H)

clean:
	rm -rf build
