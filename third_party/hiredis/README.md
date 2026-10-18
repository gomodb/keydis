# hiredis

- Upstream: <https://github.com/redis/hiredis>
- Version: **v1.5.0**
- Commit: `5bb7f030f6631b243438cf0fdd3be84352b905bb`
- License: BSD-3-Clause (see `../../LICENSES/BSD-3-Clause.txt`)
- Usage: reply parsing API only (`redisReader*`); KeyDis owns I/O, TLS,
  SSH, command encoding, lanes, and reconnection.
- Acquisition: CMake FetchContent pinned to the tag; system packages may be
  used in distribution builds if they provide a compatible version.

No hiredis source is vendored in this repository. Do not copy hiredis code
into KeyDis; use its public API and retain its license notice.
