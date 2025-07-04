# LC-3 Virtual Machine

A full implementation of the [LC-3 architecture](https://en.wikipedia.org/wiki/Little_Computer_3) in C, complete with instruction decoding, condition flags, keyboard I/O, and TRAP routines — built to execute `.obj` binary images like the classic **2048** game compiled for LC-3.

## 🧠 What This Is

This is a 16-bit virtual CPU emulator for the LC-3 instruction set.
- Emulates memory-mapped I/O (keyboard input)
- Executes compiled LC-3 binaries (`.obj` format)
- Implements the full LC-3 instruction set: arithmetic, logic, control, memory, and system calls

## 🔧 Features

- 100% instruction coverage (BR, ADD, AND, NOT, LD, ST, LDI, STI, LDR, STR, JSR, JMP, LEA, TRAP)
- Full TRAP support (`GETC`, `OUT`, `PUTS`, `PUTSP`, `IN`, `HALT`)
- Sign extension and condition flag updates
- Memory-mapped keyboard input (via polling KBSR/KBDR)
- Windows raw input mode using `conio.h` and Win32 API
- Fast instruction dispatch using function pointer table (`op_table`)
- Supports multiple `.obj` binaries as command-line arguments

## 🚀 Running the VM

- Open the solution file in Visual Studio and run
or
- Build and run — it will execute `2048.obj` by default:

