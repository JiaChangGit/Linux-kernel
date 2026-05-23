# Architecture Notes

This simulator uses a modular firmware-like structure:

- `nvme.c`: model NVMe submission/completion queues and command dispatch
- `request.c`: circular queue
- `scheduler.c`: dispatch ordering and latency accounting
- `ftl.c`: LPN to PPA translation and write path orchestration
- `mapping.c`: L2P table
- `nand.c`: NAND block/page model
- `gc.c`: greedy garbage collection
- `stats.c`: telemetry
- `block_manager.c`: free block pool

The project intentionally keeps each responsibility narrow to resemble production firmware boundaries.
