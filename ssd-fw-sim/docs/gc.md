# GC Notes

Victim selection uses greedy policy:
- Choose the block with the highest invalid page count.

Migration flow:
- Copy valid pages to new locations.
- Update mapping after programming.
- Erase victim block.
- Return block to free pool.

GC latency is included in the device timeline.
