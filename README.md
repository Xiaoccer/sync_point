# Sync Point

Modified from rocksdb SyncPoint.

## Usage

Copy `sync_point.h` and `sync_point.cc` into your project. To use `SyncPoint` for testing, add the `UNIT_TEST` macro to your project.

## Run test

```
cmake -S . -B build  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build build

cd build && ./sync_point_test

```
