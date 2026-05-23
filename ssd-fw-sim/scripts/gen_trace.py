#!/usr/bin/env python3
import argparse
import random

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["sequential", "random", "mixed"], default="mixed")
    parser.add_argument("--count", type=int, default=10000)
    parser.add_argument("--max-lba", type=int, default=4096)
    parser.add_argument("--max-size", type=int, default=8)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    lba = 0
    with open(args.output, "w", encoding="utf-8") as fp:
        for _ in range(args.count):
            if args.mode == "sequential":
                size = random.randint(1, args.max_size)
                fp.write(f"WRITE {lba} {size}\n")
                lba = (lba + size) % args.max_lba
            elif args.mode == "random":
                fp.write(f"WRITE {random.randint(0, args.max_lba - 1)} {random.randint(1, args.max_size)}\n")
            else:
                if random.random() < 0.7:
                    size = random.randint(1, args.max_size)
                    fp.write(f"WRITE {lba} {size}\n")
                    lba = (lba + size) % args.max_lba
                else:
                    fp.write(f"WRITE {random.randint(0, args.max_lba - 1)} {random.randint(1, args.max_size)}\n")

if __name__ == "__main__":
    main()
