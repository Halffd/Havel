// Decoder unit test for the modding hook engine: the exact instructions
// clang/gcc emit at -O2 x86-64 function entries, plus the refusals that
// must fire (a wrong refusal length silently corrupts the caller's stack).
//
// Built and run by scripts/ci/hook-engine-test.sh. Includes hook.c to reach
// its static decoder.
#include "src/modding/hook.c"
#include <stdio.h>

struct Case {
    const char *name;
    uint8_t bytes[15];
    size_t len;
    long want; /* instruction length in bytes, or -1 for "must refuse" */
};

int main(void) {
    struct Case cases[] = {
        /* real -O2 prologue pieces, in the order they appear */
        { "endbr64 (CET)",              {0xF3,0x0F,0x1E,0xFA}, 4, 4 },
        { "push rbp",                   {0x55}, 1, 1 },
        { "push r15 (REX.B)",           {0x41,0x57}, 2, 2 },
        { "mov rbp,rsp",                {0x48,0x89,0xE5}, 3, 3 },
        { "mov [rbp-4],edi",            {0x89,0x7D,0xFC}, 3, 3 },
        { "mov [rbp-8],rsi",            {0x48,0x89,0x75,0xF8}, 4, 4 },
        { "sub rsp,0x28 (imm8)",        {0x48,0x83,0xEC,0x28}, 4, 4 },
        { "sub rsp,0x88 (imm32)",       {0x48,0x81,0xEC,0x88,0,0,0}, 7, 7 },
        { "lea rbp,[rbp-0x28]",         {0x48,0x8D,0x6D,0xD8}, 4, 4 },
        { "lea rdi,[rsp+0x10] (SIB)",   {0x48,0x8D,0x7C,0x24,0x10}, 5, 5 },
        { "lea rdi,[rsp+0x1234] SIB32", {0x48,0x8D,0xBC,0x24,0x34,0x12,0,0}, 8, 8 },
        { "mov rax,[rsp+0x30] SIB",     {0x48,0x8B,0x44,0x24,0x30}, 5, 5 },
        { "mov [rsp+0x20],rax",         {0x48,0x89,0x44,0x24,0x20}, 5, 5 },
        { "mov ecx,[rbp-0x14] (r32)",   {0x8B,0x4D,0xEC}, 3, 3 },
        { "mov r8,[rbp-0x14] (REX.B)",  {0x49,0x8B,0x45,0xEC}, 4, 4 },
        { "xor eax,eax",                {0x31,0xC0}, 2, 2 },
        { "mov rax,imm64 (REX.W)",      {0x48,0xB8,1,2,3,4,5,6,7,8}, 10, 10 },
        { "mov ecx,imm32",              {0xB9,1,2,3,4}, 5, 5 },
        { "mov ecx,imm32 after 66",     {0x66,0xB9,1,2,3,4}, 6, 6 },
        { "push rbp (1st of 2 insns)",  {0x55,0x48,0x89,0xE5}, 4, 1 },
        { "multi-byte nop 0F 1F 40 00", {0x0F,0x1F,0x40,0x00}, 4, 4 },
        { "multi-byte nop 0F 1F 440000",{0x0F,0x1F,0x44,0x00,0x00}, 5, 5 },
        { "multi-byte nop 66 0F 1F 44 0000", {0x66,0x0F,0x1F,0x44,0x00,0x00}, 6, 6 },
        { "push imm8 6A 08",            {0x6A,0x08}, 2, 2 },
        { "mov r/m,imm32 (C7 rsp)",     {0x48,0xC7,0x44,0x24,0x08,0,0,0,0}, 9, 9 },
        { "mov r/m8,imm8 (C6)",         {0x48,0xC6,0x45,0xF8,0x00}, 5, 5 },

        /* refusals: every one of these corrupts a caller if mis-decoded */
        { "mov ecx,[rip+..] RIP-REL",   {0x8B,0x0D,1,2,3,4}, 6, -1 },
        { "cmp [rip+..],eax RIP-REL",   {0x39,0x05,1,2,3,4}, 6, -1 },
        { "abs disp32 SIB base=5",      {0x8B,0x04,0x25,1,2,3,4}, 7, -1 },
        { "call rel32",                 {0xE8,1,2,3,4}, 5, -1 },
        { "jmp rel32",                  {0xE9,1,2,3,4}, 5, -1 },
        { "ret",                        {0xC3}, 1, -1 },
        { "truncated REX only",         {0x48}, 1, -1 },
        { "truncated modrm",            {0x89}, 1, -1 },
        { "syscall (0F 05)",            {0x0F,0x05}, 2, -1 },
    };
    int fails = 0;
    int n = (int)(sizeof(cases) / sizeof(cases[0]));
    for (int i = 0; i < n; i++) {
        char err[HAVEL_HOOK_ERR_SIZE];
        struct DecoderCtx c = { cases[i].bytes, cases[i].len, err, sizeof err };
        ssize_t got = insn_len(&c);
        int ok = (cases[i].want < 0) ? (got < 0) : (got == cases[i].want);
        if (!ok) {
            fails++;
            printf("FAIL %-32s want=%ld got=%zd\n", cases[i].name, cases[i].want, got);
            if (got < 0) printf("      err: %s\n", err);
        } else {
            printf("ok   %-32s %s\n", cases[i].name, got < 0 ? "(refused)" : "");
        }
    }
    if (fails) printf("FAIL: %d of %d case(s)\n", fails, n);
    else printf("PASS: all %d decoder cases\n", n);
    return fails ? 1 : 0;
}