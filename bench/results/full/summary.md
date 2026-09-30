# Benchmark summary

```
mode:        full
date:        2026-09-30T17:35:49+10:00
cpu:         Intel(R) Core(TM) Ultra 7 258V
cpus:        8
memory:      15Gi
kernel:      6.6.87.2-microsoft-standard-WSL2
os:          Ubuntu 26.04.1 LTS
gcc:         gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
cmake:       cmake version 4.2.3
grep:        grep (GNU grep) 3.12
rg:          ripgrep 15.1.0
hyperfine:   hyperfine 1.19.0
python:      Python 3.14.4
ahogrep git: 1ead5ac
build type:  Release
sizes (MB):  1 10 100
patterns:    1 10 100 1000 10000 100000
grep loop:   up to 100 patterns only
grep locale: LC_ALL=C for every grep run except the separate locale comparison
natural_source: gutenberg
3f6bb9d6f78e0293b56acd4714dd68cb7d6d1d293402031ce9d5a216bcaf9d75  /home/ethan/ahogrep/bench/data/gutenberg/pg1342.txt
907420db6c4b68c70e2988cd2ad9c8cf79138667a01b63376d18dd17fef1a18b  /home/ethan/ahogrep/bench/data/gutenberg/pg2701.txt
7810cd483cffcf2cc8a1d8f0d5807931e69d4f48cd14149b8c76f88af82fead3  /home/ethan/ahogrep/bench/data/gutenberg/pg84.txt
922e2a12ccb43a4c9544c260b2166c6ad2097aeb5957faeee113f173bb857cd0  /home/ethan/ahogrep/bench/data/gutenberg/pg1661.txt
d54c2b80d40a40b982cd88852c6180bb944d95acdb028af3d0e01a1750681784  /home/ethan/ahogrep/bench/data/gutenberg/pg98.txt
96cd16eacdbfebae8fdda5591f66e0cc8ee76be18e0cd1aca02bc00615782d28  /home/ethan/ahogrep/bench/data/gutenberg/pg345.txt
13414dee2951c3ee731d76d2ffd822016b2479c892162760c5d0eb2aa5fa7631  /home/ethan/ahogrep/bench/data/gutenberg/pg1260.txt
d617a37aa7ae1e1a93dcde2634db2bccb86824e31e29a55230bdbf77d6872d59  /home/ethan/ahogrep/bench/data/gutenberg/pg76.txt
01b38ea4c710a84bc18d0bd41271a5a1a92b94e97b2812f4dece97d4a694725e  /home/ethan/ahogrep/bench/data/gutenberg/pg11.txt
74d77384b123a6360db9ab58463cff8b38df8525fbdc6000c81ee388e1f3cf10  /home/ethan/ahogrep/bench/data/gutenberg/pg74.txt
elapsed:     14 min 3 s
```

## Wall time (median of runs) and throughput

grep-loop is only run up to 100 patterns.

| text | size | patterns | ahogrep | grep-F | rg-F | grep-loop |
|---|---:|---:|---:|---:|---:|---:|
| natural | 1 MB | 1 | 2.9 ms (340 MB/s) | 1.3 ms (796 MB/s) | 1.2 ms (866 MB/s) | 2.1 ms (486 MB/s) |
| natural | 1 MB | 10 | 5.3 ms (190 MB/s) | 3.8 ms (260 MB/s) | 1.2 ms (801 MB/s) | 12.3 ms (81 MB/s) |
| natural | 1 MB | 100 | 8.7 ms (115 MB/s) | 9.2 ms (108 MB/s) | 3.5 ms (286 MB/s) | 115.4 ms (9 MB/s) |
| natural | 1 MB | 1,000 | 15.7 ms (64 MB/s) | 10.0 ms (100 MB/s) | 7.1 ms (141 MB/s) | - |
| natural | 1 MB | 10,000 | 67.5 ms (15 MB/s) | 9.5 ms (106 MB/s) | 14.8 ms (68 MB/s) | - |
| natural | 1 MB | 100,000 | 1.07 s (1 MB/s) | 148.5 ms (7 MB/s) | 136.7 ms (7 MB/s) | - |
| natural | 10 MB | 1 | 14.1 ms (711 MB/s) | 6.5 ms (1,532 MB/s) | 2.2 ms (4,525 MB/s) | 7.4 ms (1,357 MB/s) |
| natural | 10 MB | 10 | 40.5 ms (247 MB/s) | 34.1 ms (294 MB/s) | 2.6 ms (3,823 MB/s) | 54.7 ms (183 MB/s) |
| natural | 10 MB | 100 | 59.6 ms (168 MB/s) | 92.7 ms (108 MB/s) | 14.3 ms (701 MB/s) | 533.4 ms (19 MB/s) |
| natural | 10 MB | 1,000 | 91.3 ms (110 MB/s) | 96.0 ms (104 MB/s) | 22.2 ms (451 MB/s) | - |
| natural | 10 MB | 10,000 | 171.7 ms (58 MB/s) | 54.9 ms (182 MB/s) | 42.0 ms (238 MB/s) | - |
| natural | 10 MB | 100,000 | 1.79 s (6 MB/s) | 171.7 ms (58 MB/s) | 160.6 ms (62 MB/s) | - |
| natural | 100 MB | 1 | 130.0 ms (769 MB/s) | 62.1 ms (1,611 MB/s) | 10.6 ms (9,434 MB/s) | 72.2 ms (1,386 MB/s) |
| natural | 100 MB | 10 | 387.1 ms (258 MB/s) | 335.7 ms (298 MB/s) | 16.3 ms (6,121 MB/s) | 464.1 ms (215 MB/s) |
| natural | 100 MB | 100 | 574.3 ms (174 MB/s) | 927.8 ms (108 MB/s) | 124.8 ms (801 MB/s) | 4.46 s (22 MB/s) |
| natural | 100 MB | 1,000 | 831.2 ms (120 MB/s) | 946.8 ms (106 MB/s) | 153.7 ms (651 MB/s) | - |
| natural | 100 MB | 10,000 | 1.18 s (85 MB/s) | 468.2 ms (214 MB/s) | 305.6 ms (327 MB/s) | - |
| natural | 100 MB | 100,000 | 8.41 s (12 MB/s) | 694.4 ms (144 MB/s) | 457.9 ms (218 MB/s) | - |
| random | 1 MB | 1 | 4.2 ms (237 MB/s) | 1.5 ms (683 MB/s) | 1.4 ms (712 MB/s) | 2.1 ms (485 MB/s) |
| random | 1 MB | 10 | 9.5 ms (105 MB/s) | 8.8 ms (114 MB/s) | 2.0 ms (498 MB/s) | 17.4 ms (58 MB/s) |
| random | 1 MB | 100 | 7.1 ms (140 MB/s) | 17.5 ms (57 MB/s) | 3.5 ms (283 MB/s) | 160.3 ms (6 MB/s) |
| random | 1 MB | 1,000 | 27.9 ms (36 MB/s) | 29.2 ms (34 MB/s) | 23.5 ms (43 MB/s) | - |
| random | 1 MB | 10,000 | 140.0 ms (7 MB/s) | 45.5 ms (22 MB/s) | 38.7 ms (26 MB/s) | - |
| random | 1 MB | 100,000 | 1.19 s (1 MB/s) | 197.8 ms (5 MB/s) | 201.5 ms (5 MB/s) | - |
| random | 10 MB | 1 | 19.2 ms (521 MB/s) | 6.4 ms (1,556 MB/s) | 2.8 ms (3,597 MB/s) | 6.4 ms (1,557 MB/s) |
| random | 10 MB | 10 | 57.9 ms (173 MB/s) | 59.4 ms (168 MB/s) | 2.9 ms (3,416 MB/s) | 59.7 ms (168 MB/s) |
| random | 10 MB | 100 | 44.6 ms (224 MB/s) | 146.9 ms (68 MB/s) | 15.0 ms (668 MB/s) | 571.8 ms (17 MB/s) |
| random | 10 MB | 1,000 | 88.7 ms (113 MB/s) | 196.1 ms (51 MB/s) | 31.8 ms (314 MB/s) | - |
| random | 10 MB | 10,000 | 208.7 ms (48 MB/s) | 276.3 ms (36 MB/s) | 103.5 ms (97 MB/s) | - |
| random | 10 MB | 100,000 | 1.52 s (7 MB/s) | 549.1 ms (18 MB/s) | 260.9 ms (38 MB/s) | - |
| random | 100 MB | 1 | 163.2 ms (613 MB/s) | 42.0 ms (2,381 MB/s) | 12.2 ms (8,177 MB/s) | 44.0 ms (2,273 MB/s) |
| random | 100 MB | 10 | 567.4 ms (176 MB/s) | 563.7 ms (177 MB/s) | 15.3 ms (6,527 MB/s) | 489.3 ms (204 MB/s) |
| random | 100 MB | 100 | 422.8 ms (236 MB/s) | 1.40 s (71 MB/s) | 122.9 ms (814 MB/s) | 4.81 s (21 MB/s) |
| random | 100 MB | 1,000 | 820.6 ms (122 MB/s) | 1.90 s (53 MB/s) | 186.2 ms (537 MB/s) | - |
| random | 100 MB | 10,000 | 1.55 s (65 MB/s) | 3.61 s (28 MB/s) | 928.4 ms (108 MB/s) | - |
| random | 100 MB | 100,000 | 5.06 s (20 MB/s) | 4.46 s (22 MB/s) | 1.03 s (97 MB/s) | - |

## Construction (empty input, median of runs)

| text | patterns | ahogrep | grep-F | rg-F |
|---|---:|---:|---:|---:|
| natural | 1 | 1.8 ms | 0.5 ms | 1.1 ms |
| natural | 10 | 1.9 ms | 0.5 ms | 1.2 ms |
| natural | 100 | 2.6 ms | 0.5 ms | 1.9 ms |
| natural | 1,000 | 8.0 ms | 1.1 ms | 3.7 ms |
| natural | 10,000 | 56.9 ms | 5.9 ms | 13.0 ms |
| natural | 100,000 | 1.04 s | 135.0 ms | 141.3 ms |
| random | 1 | 1.8 ms | 0.4 ms | 1.1 ms |
| random | 10 | 2.0 ms | 0.5 ms | 1.2 ms |
| random | 100 | 2.6 ms | 0.6 ms | 1.7 ms |
| random | 1,000 | 8.8 ms | 1.1 ms | 4.0 ms |
| random | 10,000 | 70.1 ms | 8.0 ms | 13.8 ms |
| random | 100,000 | 1.05 s | 140.1 ms | 172.0 ms |

## Peak memory

| text | patterns | nodes | table | ahogrep RSS | grep-F RSS | rg-F RSS |
|---|---:|---:|---:|---:|---:|---:|
| natural | 1 | 8 | 0.0 MiB | 5.6 MiB | 2.4 MiB | 7.0 MiB |
| natural | 10 | 80 | 0.1 MiB | 5.8 MiB | 2.4 MiB | 7.0 MiB |
| natural | 100 | 668 | 0.7 MiB | 6.4 MiB | 2.4 MiB | 7.4 MiB |
| natural | 1,000 | 5,743 | 5.6 MiB | 11.9 MiB | 2.9 MiB | 9.0 MiB |
| natural | 10,000 | 45,623 | 44.6 MiB | 69.7 MiB | 6.8 MiB | 12.5 MiB |
| natural | 100,000 | 567,825 | 554.5 MiB | 1,059.2 MiB | 56.1 MiB | 67.0 MiB |
| random | 1 | 10 | 0.0 MiB | 5.6 MiB | 2.4 MiB | 7.0 MiB |
| random | 10 | 83 | 0.1 MiB | 5.8 MiB | 2.5 MiB | 7.0 MiB |
| random | 100 | 754 | 0.7 MiB | 6.4 MiB | 2.4 MiB | 7.2 MiB |
| random | 1,000 | 7,164 | 7.0 MiB | 12.9 MiB | 3.0 MiB | 9.4 MiB |
| random | 10,000 | 64,436 | 62.9 MiB | 72.4 MiB | 8.5 MiB | 12.7 MiB |
| random | 100,000 | 589,016 | 575.2 MiB | 1,058.9 MiB | 58.0 MiB | 70.8 MiB |

## Locale effect (grep -F, natural text, 10 MB, 1,000 patterns)

| locale | median |
|---|---:|
| grep-F LC_ALL=C | 107.4 ms |
| grep-F LC_ALL=C.utf8 | 100.1 ms |
