# Arch package

```bash
cd packaging/arch
makepkg -si
```

The PKGBUILD uses the system `hiredis` (`KEYDIS_USE_SYSTEM_HIREDIS=ON`) so
the package build does not fetch sources from the network. It references the
`v0.4.16` tag and pins its tarball SHA-256.

Dependencies use Arch's KF6 package names (`kconfig`, `kxmlgui`, `krunner`,
`kwallet`, ...), not the upstream `kf6-*` names. `krunner` provides both the
plugin headers and the runtime framework; `qt6-tools` is only needed for
`lrelease` during the build.
