# NVMe SSD Firmware Write Path Simulator

A modular C11 project that simulates a simplified NVMe SSD firmware write path with NVMe submission/completion queues, an internal firmware request queue, page-level FTL, NAND block/page state management, greedy garbage collection, and performance statistics.

## SSD Architecture Overview

```text
Host Trace
   |
   v
NVMe Submission Queue
   |
   v
NVMe Command Fetch
   |
   v
Firmware Request Queue
   |
   v
Scheduler
   |
   v
FTL (L2P, out-of-place update)
   |
   +--> Mapping Table
   +--> Garbage Collection
   |
   v
NAND Simulator
   |
   v
NVMe Completion Queue
   |
   +--> Statistics / Latency Accounting
   |
   v
Host Completion Reap
```

## NVMe Queue Model

- Host writes are first posted into an NVMe submission queue (SQ).
- Firmware fetches SQ entries into an internal request queue before dispatch.
- `request_queue_depth` is used as the depth for the SQ, CQ, and internal firmware queue in this simulator.
- Each request carries an NVMe command identifier through the FTL path.
- Completed requests are posted into a completion queue (CQ) with queue head and phase metadata.
- The simulator reaps CQ entries to model host-side completion polling.

## NVMe Write Path Flow

1. Host submits `WRITE <LBA> <SIZE>` into the NVMe SQ.
2. Controller fetches SQ entries into the internal firmware request queue.
3. Scheduler dequeues and dispatches each request.
4. FTL translates LPN to PPA.
5. NAND allocates a new page and programs data out-of-place.
6. Old page is invalidated.
7. Mapping table is updated.
8. GC is triggered when free blocks fall below threshold.
9. A completion entry is posted to the NVMe CQ.
10. The host reaps completions from the CQ.

## FTL Design

- Page-level mapping.
- Out-of-place updates only.
- Sequential NAND programming inside a block.
- Physical pages store their owning LPN in simulated OOB metadata.
- Greedy GC selects the block with the most invalid pages.

## GC Flow

```text
Victim Selection -> Migrate Valid Pages -> Erase Block -> Return Free Block
```

GC latency is accumulated using:
- Read: 50us
- Program: 200us
- Erase: 1500us

## Workload Example

Trace format:

```text
WRITE 100 4
WRITE 200 8
WRITE 100 2
```

## Build Instruction

```bash
make
./ssd_fw_sim traces/sample.trace
```

Regression tests:

```bash
make test
```

Optional CSV export:

```bash
./ssd_fw_sim --csv stats.csv traces/sample.trace
```

Optional config override:

```bash
./ssd_fw_sim --config ssd.conf traces/sample.trace
```

Clean:

```bash
make clean
```

Config file rules:

- `gc_free_block_threshold` must be between `1` and `total_blocks - 1`
- unknown keys, malformed lines, and non-numeric values fail fast during config load

## Performance Metrics

The simulator reports:

- Host requests
- Host pages
- NAND writes
- NAND reads
- NAND erases
- GC count
- Migrated pages
- Write amplification
- Average / max queue latency
- Average / max service latency
- Average / max total latency
- NVMe submission/completion queue activity is validated by the regression suite

Sequential vs random request classification:
- Sequential write: the request starts exactly at the first LPN after the previous non-empty host write.
- Random write: any other host write request.

## Example Output

```text
=== SSD Configuration ===
total_blocks           : 128
pages_per_block        : 64
logical_pages          : 4096
request_queue_depth    : 256
gc_free_block_threshold: 8
read_latency_us        : 50
program_latency_us     : 200
erase_latency_us       : 1500
trace_inter_arrival_us : 10

=== SSD Statistics ===
Host Requests          : 7
Host Pages             : 46
NAND Writes            : 46
NAND Reads             : 0
NAND Erases            : 0
GC Count               : 0
Migrated Pages         : 0
Foreground GC Count    : 0
Background GC Count    : 0
Sequential Writes      : 0
Random Writes          : 7
Write Amplification    : 1.00
Avg Queue Latency(us)  : 3455
Avg Service Latency(us): 1314
Avg Latency(us)        : 4770
Max Queue Latency(us)  : 8340
Max Service Latency(us): 3200
Max Latency(us)        : 9140
```

## Future Improvements

- Hot/cold data placement
- Wear leveling
- Multi-channel NAND model
- Background GC thread
- Power-loss consistency checkpointing
- Metadata journaling
- Better victim selection policies
- Read-modify-write support for partial page updates
- NVMe admin command simulation
