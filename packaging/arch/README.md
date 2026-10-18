# Arch package

```bash
cd packaging/arch
makepkg -si
```

The PKGBUILD uses the system `hiredis` (`KEYDIS_USE_SYSTEM_HIREDIS=ON`) so
the package build does not fetch sources from the network. Replace the
`sha256sums=('SKIP')` entry with the release tarball checksum before
submitting to the AUR.
