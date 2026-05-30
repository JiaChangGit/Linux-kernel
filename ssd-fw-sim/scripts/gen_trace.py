#!/usr/bin/env python3
"""產生 SSD 模擬器可讀取的 WRITE trace。"""

import argparse
import random

def main() -> None:
    parser = argparse.ArgumentParser(
        description="產生 WRITE <LBA> <SIZE> 格式的 SSD 測試 trace"
    )
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
                # Sequential 模式從目前 LBA 往後寫，超過 max-lba 後回到 0。
                size = random.randint(1, args.max_size)
                fp.write(f"WRITE {lba} {size}\n")
                lba = (lba + size) % args.max_lba
            elif args.mode == "random":
                # Random 模式每筆 request 都重新抽 LBA 與大小。
                fp.write(f"WRITE {random.randint(0, args.max_lba - 1)} {random.randint(1, args.max_size)}\n")
            else:
                # Mixed 模式以 70% sequential + 30% random 模擬較常見的混合 workload。
                if random.random() < 0.7:
                    size = random.randint(1, args.max_size)
                    fp.write(f"WRITE {lba} {size}\n")
                    lba = (lba + size) % args.max_lba
                else:
                    fp.write(f"WRITE {random.randint(0, args.max_lba - 1)} {random.randint(1, args.max_size)}\n")

if __name__ == "__main__":
    main()
