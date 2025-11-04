# Wordle Server (Local)

Host a Wordle-style game via a lightweight C server running on your machine.

---

## Features
- Deterministic word sequence via a user-provided seed
- Custom dictionaries (any newline-separated word list)
- Simple CLI interface (no external dependencies beyond a C compiler)

---

## Requirements
- A C compiler (e.g., `gcc` or `clang`)
- A dictionary file (provided by valid-words.txt or generate your own)

---

## Quick Start

```bash
# Build
gcc -O2 wordle-server.c -o server.out

# Run (see argument details below)
./server.out <listener-port> <seed> <dictionary-filename> <num-words>

| Argument                | Description                                                             | Example           |
| ----------------------- | ----------------------------------------------------------------------- | ----------------- |
| `<listener-port>`       | TCP port to listen on. Use any available port.                          | `8080`            |
| `<seed>`                | Integer seed for reproducibility of the word order.                     | `42`              |
| `<dictionary-filename>` | Path to a newline-separated word list to load.                          | `valid-words.txt` |
| `<num-words>`           | Number of words to load from the dictionary (from the top of the file). | `5000`            |

