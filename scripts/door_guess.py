#!/usr/bin/env python3
"""A stdio-only native door example; no BBS database access."""
import os, secrets
print("Liner notes: number guessing (1..100)", flush=True)
print("Hello, " + os.environ.get("LINER_USER", "player") + ". Q returns to the BBS.", flush=True)
answer = secrets.randbelow(100) + 1
while True:
    try:
        value = input("Your guess> ").strip()
    except (EOFError, KeyboardInterrupt):
        break
    if value.upper() == "Q":
        break
    try:
        guess = int(value)
    except ValueError:
        print("Enter a number or Q.", flush=True)
        continue
    if guess == answer:
        print("Correct!", flush=True)
        break
    print("Higher." if guess < answer else "Lower.", flush=True)
