# cuda-db

A time-series database written from scratch in C++20, with queries executed on the GPU using CUDA.

It ingests data in the [InfluxDB line protocol](https://docs.influxdata.com/influxdb/v2/reference/syntax/line-protocol/),
stores it in columnar form in memory and answers aggregation queries (`MIN`, `MAX`, `AVG`, `COUNT`) filtered by tags and time,
either on the CPU (multi-threaded) or on the GPU.

## Results

5 million randomly generated lines (12.5 million rows, 4 datasets, 300 series each), median of 50 runs.

| Query | CPU, multi-threaded | GPU | Speedup |
|---|---:|---:|---:|
| `AVG(usage_user) FROM cpu` | 2.68 ms | 0.27 ms | **9.8×** |
| `MAX(usage_user) FROM cpu`, 1 tag | 2.24 ms | 0.29 ms | **7.7×** |
| `AVG(temperature) FROM sensor`, 2 tags | 2.43 ms | 0.30 ms | **8.2×** |
| `MIN(temperature) FROM sensor`, 3 tags | 2.29 ms | 0.29 ms | **8.0×** |
| `AVG(read_mb) FROM disk`, time range | 1.37 ms | 0.17 ms | **8.0×** |
| `COUNT(used_percent) FROM memory`, 1 tag + time range | 1.28 ms | 0.18 ms | **7.2×** |

Hardware: Intel Core i9-10850K (10 cores / 20 threads), NVIDIA GeForce RTX 3060 Ti (8 GB), WSL2.

Data is copied to GPU memory once, before the queries: **27.9 ms** for all datasets (148.8 MB, 5.3 GB/s).
Every query after that only launches the kernel. Column compression cuts the copied data **2.0×** (300 MB → 148.8 MB),
which brings the copy down from about 52 ms to 28 ms.

Run the benchmark yourself with `./benchmark [line_count] [seed]`.

## Features

- **Zero Copy Parser**: (`std::string_view` into the input buffer)

- **Queries**: `MIN`, `MAX`, `AVG`, `COUNT` of a field, filtered by any number of tags (AND) and a time range `[from, to)`.
- **multi-threaded CPU executor**
- **GPU executor**: data kept in GPU memory, one CUDA kernel that filters and aggregates in a single pass,
  block-level tree reduction in shared memory.
- **Column compression**: frame of reference + bit-packing of series ids, field ids and timestamps, decoded on the
  fly inside the kernel; halves the data copied to the GPU.
- **Tests**: 31 test cases with 250+ sections, including GPU results checked against the CPU executor.

## Architecture

```
insert:  line protocol ──► parse_line ──► MemTable::prepare ──► MemTable::commit

                                   ┌──► CPU executor ──┐
query:   Query ──► resolve_query ──┤                   ├──► AggState ──► finalize ──► result
                                   └──► GPU executor ──┘
```

## Building

Requirements: CMake 3.24+, a C++20 compiler. CUDA Toolkit is optional: without it only the CPU executor is built.

```bash
mkdir build-release && cd build-release
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)

./unit_tests      # tests (GPU tests are skipped when no GPU is available)
./benchmark       # benchmark, 5 million lines by default
```

CUDA is detected automatically; disable it with `-DGPU_DB_ENABLE_CUDA=OFF`.

## Limitations

- Data lives only in RAM (and in VRAM for GPU queries), nothing is written to disk yet.
- Field values are stored as `double`, integers, booleans and strings are rejected.
- Tags are compared as raw text, so `a=1,b=2` and `b=2,a=1` are two different series.
- The GPU works on a snapshot: rows inserted after `upload()` are not visible until the next upload.
- The largest `uint64` timestamp is reserved (used as the open end of time ranges) and rejected on insert.

## Roadmap

- [x] Line protocol parser
- [x] Columnar memtable, one per dataset
- [x] Inverted tag index
- [x] Query engine: `MIN`, `MAX`, `AVG`, `COUNT` with tag and time filters
- [x] Multi-threaded CPU executor
- [x] CUDA executor, GPU vs CPU tests, benchmark
- [x] Column compression decoded inside the kernel
- [ ] Multi-threaded CPU compression
- [ ] Incremental upload: send only rows added since the last upload
- [ ] `GROUP BY time` (aggregation in time windows), on CPU and GPU
- [ ] Flush of memtables into immutable blocks sorted by (field, series, time)
- [ ] persistence on disk
- [ ] Integer fields
- [ ] Comparison with InfluxDB
