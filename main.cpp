#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdint.h>
#include <signal.h>
/* Windows-specific headers for console I/O */
#include <Windows.h>
#include <conio.h>  // For _kbhit

/* ======== REGISTER DEFINITIONS ======== */
enum
{
    R_R0 = 0, R_R1, R_R2, R_R3,
    R_R4, R_R5, R_R6, R_R7,
    R_PC,      // Program Counter
    R_COND,    // Condition Flags
    R_COUNT    // Total number of registers
};

/* ======== CONDITION FLAGS ======== */
enum
{
    FL_POS = 1 << 0, // Positive flag
    FL_ZRO = 1 << 1, // Zero flag
    FL_NEG = 1 << 2  // Negative flag
};

/* ======== OPCODE DEFINITIONS ======== */
enum
{
    OP_BR = 0, OP_ADD, OP_LD, OP_ST,
    OP_JSR, OP_AND, OP_LDR, OP_STR,
    OP_RTI, OP_NOT, OP_LDI, OP_STI,
    OP_JMP, OP_RES, OP_LEA, OP_TRAP
};

/* ======== MEMORY-MAPPED I/O REGISTERS ======== */
enum
{
    MR_KBSR = 0xFE00, // Keyboard status register
    MR_KBDR = 0xFE02  // Keyboard data register
};

/* ======== TRAP CODES ======== */
enum
{
    TRAP_GETC = 0x20,  // Get a character from the keyboard
    TRAP_OUT = 0x21,  // Output a character to the console
    TRAP_PUTS = 0x22,  // Output a string
    TRAP_IN = 0x23,  // Prompt for input and read character
    TRAP_PUTSP = 0x24,  // Output a packed string
    TRAP_HALT = 0x25   // Halt the program
};

/* ======== MEMORY & REGISTER STATE ======== */
#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX];     // 65536 16-bit memory locations
uint16_t reg[R_COUNT];           // 10 general-purpose and special registers

/* ======== INPUT BUFFERING ======== */
HANDLE hStdin = INVALID_HANDLE_VALUE;
DWORD fdwMode, fdwOldMode;

void disable_input_buffering()
{
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &fdwOldMode); // Save old input mode
    fdwMode = fdwOldMode
        ^ ENABLE_ECHO_INPUT      // Turn off input echoing
        ^ ENABLE_LINE_INPUT;     // Turn off line-buffering
    SetConsoleMode(hStdin, fdwMode);
    FlushConsoleInputBuffer(hStdin); // Clear any pending input
}

void restore_input_buffering()
{
    SetConsoleMode(hStdin, fdwOldMode); // Restore original console input mode
}

/* ======== UTILITY: Check if a key was pressed ======== */
uint16_t check_key()
{
    return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}

/* ======== SIGNAL HANDLER ======== */
void handle_interrupt(int signal)
{
    restore_input_buffering(); // Reset terminal on Ctrl+C
    printf("\n");
    exit(-2);
}

/* ======== SIGN EXTENSION ======== */
uint16_t sign_extend(uint16_t x, int bit_count)
{
    // If the sign bit is set, fill the upper bits with 1's
    if ((x >> (bit_count - 1)) & 1)
    {
        x |= (0xFFFF << bit_count);
    }
    return x;
}

/* ======== BYTE SWAPPING FOR LITTLE-ENDIAN ======== */
uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8); // Swap upper and lower byte
}

/* ======== SET CONDITION FLAGS BASED ON REGISTER VALUE ======== */
void update_flags(uint16_t r)
{
    if (reg[r] == 0)
        reg[R_COND] = FL_ZRO;
    else if (reg[r] >> 15) // MSB is 1 → Negative
        reg[R_COND] = FL_NEG;
    else
        reg[R_COND] = FL_POS;
}

/* ======== IMAGE LOADING FROM .obj FILE ======== */
void read_image_file(FILE* file)
{
    // First word tells us the origin (where to load code in memory)
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    // Load rest of the file into memory from 'origin' address
    uint16_t max_read = MEMORY_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    // Fix endianness
    while (read-- > 0)
    {
        *p = swap16(*p);
        ++p;
    }
}

/* ======== IMAGE LOADING WRAPPER ======== */
int read_image(const char* image_path)
{
    FILE* file = fopen(image_path, "rb");
    if (!file) return 0;
    read_image_file(file);
    fclose(file);
    return 1;
}

/* ======== MEMORY ACCESS ======== */
void mem_write(uint16_t address, uint16_t val)
{
    memory[address] = val;
}

uint16_t mem_read(uint16_t address)
{
    // Keyboard memory-mapped I/O
    if (address == MR_KBSR)
    {
        if (check_key())
        {
            memory[MR_KBSR] = (1 << 15); // Key is ready
            memory[MR_KBDR] = getchar();
        }
        else
        {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}

/* ======== INSTRUCTION DECODING & EXECUTION ======== */
int running = 1;

template <unsigned op>
void ins(uint16_t instr)
{
    uint16_t r0, r1, r2, imm5, imm_flag;
    uint16_t pc_plus_off, base_plus_off;

    constexpr uint16_t opbit = (1 << op);

    if (0x4EEE & opbit) { r0 = (instr >> 9) & 0x7; }
    if (0x12F3 & opbit) { r1 = (instr >> 6) & 0x7; }
    if (0x0022 & opbit) // ADD, AND
    {
        imm_flag = (instr >> 5) & 0x1;
        if (imm_flag) { imm5 = sign_extend(instr & 0x1F, 5); }
        else { r2 = instr & 0x7; }
    }

    if (0x00C0 & opbit)
        base_plus_off = reg[r1] + sign_extend(instr & 0x3F, 6);

    if (0x4C0D & opbit)
        pc_plus_off = reg[R_PC] + sign_extend(instr & 0x1FF, 9);

    // Execute actual instructions
    if (0x0001 & opbit) // BR
    {
        uint16_t cond = (instr >> 9) & 0x7;
        if (cond & reg[R_COND]) { reg[R_PC] = pc_plus_off; }
    }
    if (0x0002 & opbit) // ADD
    {
        reg[r0] = imm_flag ? reg[r1] + imm5 : reg[r1] + reg[r2];
    }
    if (0x0020 & opbit) // AND
    {
        reg[r0] = imm_flag ? reg[r1] & imm5 : reg[r1] & reg[r2];
    }
    if (0x0200 & opbit) { reg[r0] = ~reg[r1]; } // NOT
    if (0x1000 & opbit) { reg[R_PC] = reg[r1]; } // JMP
    if (0x0010 & opbit) // JSR
    {
        reg[R_R7] = reg[R_PC];
        if ((instr >> 11) & 1)
            reg[R_PC] += sign_extend(instr & 0x7FF, 11);
        else
            reg[R_PC] = reg[r1];
    }
    if (0x0004 & opbit) { reg[r0] = mem_read(pc_plus_off); } // LD
    if (0x0400 & opbit) { reg[r0] = mem_read(mem_read(pc_plus_off)); } // LDI
    if (0x0040 & opbit) { reg[r0] = mem_read(base_plus_off); } // LDR
    if (0x4000 & opbit) { reg[r0] = pc_plus_off; } // LEA
    if (0x0008 & opbit) { mem_write(pc_plus_off, reg[r0]); } // ST
    if (0x0800 & opbit) { mem_write(mem_read(pc_plus_off), reg[r0]); } // STI
    if (0x0080 & opbit) { mem_write(base_plus_off, reg[r0]); } // STR

    // TRAP instructions
    if (0x8000 & opbit)
    {
        reg[R_R7] = reg[R_PC]; // Save return address
        switch (instr & 0xFF)
        {
        case TRAP_GETC:
            reg[R_R0] = getchar();
            update_flags(R_R0);
            break;
        case TRAP_OUT:
            putc((char)reg[R_R0], stdout);
            fflush(stdout);
            break;
        case TRAP_PUTS:
        {
            uint16_t* c = memory + reg[R_R0];
            while (*c) putc((char)*c++, stdout);
            fflush(stdout);
        } break;
        case TRAP_IN:
        {
            printf("Enter a character: ");
            char c = getchar();
            putc(c, stdout);
            fflush(stdout);
            reg[R_R0] = (uint16_t)c;
            update_flags(R_R0);
        } break;
        case TRAP_PUTSP:
        {
            uint16_t* c = memory + reg[R_R0];
            while (*c)
            {
                putc((char)(*c & 0xFF), stdout);
                char char2 = *c >> 8;
                if (char2) putc(char2, stdout);
                ++c;
            }
            fflush(stdout);
        } break;
        case TRAP_HALT:
            puts("HALT");
            fflush(stdout);
            running = 0;
            break;
        }
    }

    // Update condition flags if needed
    if (0x4666 & opbit) update_flags(r0);
}

/* ======== OPCODE DISPATCH TABLE ======== */
static void (*op_table[16])(uint16_t) = {
    ins<0>, ins<1>, ins<2>, ins<3>,
    ins<4>, ins<5>, ins<6>, ins<7>,
    NULL,   ins<9>, ins<10>, ins<11>,
    ins<12>,NULL,   ins<14>, ins<15>
};

/* ======== ENTRY POINT ======== */
int main(int argc, const char* argv[])
{
    // Load machine code image(s)
    /*if (argc < 2)
    {
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }

    for (int j = 1; j < argc; ++j)
    {
        if (!read_image(argv[j]))
        {
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }*/
	read_image("2048.obj"); // Load default image
    signal(SIGINT, handle_interrupt); // Ctrl+C handler
    disable_input_buffering();        // For raw input

    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;

    // Fetch-decode-execute loop
    while (running)
    {
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12;
        op_table[op](instr);
    }

    restore_input_buffering(); // Restore terminal state on exit
}
