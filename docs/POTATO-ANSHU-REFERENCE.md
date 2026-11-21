# ANSHU Potato archive: exact comparison with the earlier reference

## Result

**This is a different RAR package containing the same six complete DLL files
(five unique byte sequences) already analyzed and tested.** Every extracted
member was checked with SHA-256 **and a complete byte-for-byte comparison**
against the earlier archive, including duplicate multiplicity. This is not an
inference from a filename, an identical code section or the archive's size.

No supplied DLL was executed again for this review. The earlier
[isolated Windows WGL experiment](POTATO-DLL-PROBE.md) already executed all five
exact identities, in both texture-object and sampler-object modes. Those
measurements remain evidence about that controlled software fixture, **not**
about the video author's PC or successful installed-game operation.

## Inputs and measured inventory

| | Earlier package | Newly supplied package |
| --- | --- | --- |
| User's Drive file | [7FARFF archive](https://drive.google.com/file/d/1QGBvjTpW0mRNPwv2eqj77UEsLQtNDV7Y/view) | [ANSHU archive](https://drive.google.com/file/d/1Ex-E3YCwkq5wWschqaFMdNv9abB0MV9-/view) |
| Name | `POTATO FF DLL 7FARFF.rar` | `POTATO FF DLL BY ANSHU.rar` |
| Actual archive bytes | 344,442 | 345,058 |
| SHA-256 | `80751bed1151e7f5bbcf186b51b8aada3b446af0278e5575384de7fa9eeb8b17` | `ed56050e890d9bf9efa8df08cd5eee2f143041cb49b983760c231e2c1fbca449` |
| Entries | 17: 11 directories + 6 DLLs | 18: 12 directories + 6 DLLs |
| Expanded file bytes | 1,465,344 | 1,465,344 |
| Unique DLL byte sequences | 5 | The same 5 |

The new download came through the connected Drive's read-only download tool.
Its size, SHA-256 and MD5 (`abeb985c7db2c9618d880b7c203b108e`) matched the
previously retrieved Drive metadata. Password `ANSHUXPOTATO` successfully
opened the archive. Its extra wrapper directory and renamed strength folders
do not change any DLL byte. All six remain AMD64 PE DLLs with no populated PE
certificate table; that is not a safety certification. There are no source,
license, configuration or installer files in either archive.

New paths below omit the repeated prefix
`POTATO FF DLL BY ANSHU/POTATO FF DLL BY ANSHU/`. Old paths omit
`POTATO FF DLL 7FARFF/`. Identities A–E are the
[previously documented full DLL hashes](POTATO-DLL-REVIEW.md#dll-sha-256-identities).

| New member | Byte-identical earlier member(s) | Identity | Previously measured LOD mechanism |
| --- | --- | --- | --- |
| `Intel/LOW/dinput8.dll` | `Intel/Bajo/dinput8.dll` | A | Bias 1.5 |
| `Intel/STRONG/dinput8.dll` | `Intel/Fuerte/dinput8.dll`, `Nvidia  AMD/BS MSI 4/forte/dinput8.dll` | B | Bias 3.0 |
| `Nvidia  AMD/BS MSI 4/forte/dinput8.dll` | The same two B members | B | Bias 3.0 |
| `Nvidia  AMD/BS MSI 4/leve/dinput8.dll` | Same relative old path | C | Bias 1.5 + minimum LOD 1.5 |
| `Nvidia  AMD/BS MSI 5/LIGHT/version.dll` | `Nvidia  AMD/BS MSI 5/leve/version.dll` | E | Bias 1.5 + minimum LOD 1.5 |
| `Nvidia  AMD/BS MSI 5/STRONG/version.dll` | `Nvidia  AMD/BS MSI 5/forte/version.dll` | D | Bias 3.0 + minimum LOD 3.0 |

The [machine-readable comparison](evidence/potato-anshu-reference-comparison.json)
contains all six full paths, sizes, complete SHA-256 hashes and matching earlier
paths. The binary archive, extracted members, strings and disassembly stay in
ignored local analysis storage, not Git, artifacts or releases.

## What the accompanying video adds—and does not add

Source: [Free Fire Potato Graphics — BOBBY LIVE 1M](https://www.youtube.com/watch?v=WD1vwPoxt64).
The fetched description links the **exact new Drive ID** above. The retrieved
Hindi transcript describes choosing the hardware/emulator folder, selecting
Light/Strong (Strong is demonstrated), copying `version.dll` into the emulator's
installation directory and starting a training session.

This establishes the tutorial's intended **system-library-proxy installation
route**, not a second graphics algorithm. Video frames were not inspected;
there is no measured frame-time/FPS, texture-pixel comparison, renderer/API
identification or account-safety validation from this review. Narrator claims
about smoothness, account safety or extended testing are not our findings.

The known DLL bytes implement **desktop OpenGL/WGL bind/draw-time texture LOD**
changes. The archive/video does not establish DirectX/Vulkan texture interception,
a different graphics API selected in the demonstrated player, or which actual
player contexts/resources were modified. A proxy name is a loading strategy;
copying it into the install folder is not evidence that an independently built
regular module needs to adopt that startup contract. Original-name/player
startup and physical-driver compatibility remain unverified.

## Consequence for the independent implementation

The useful mechanism is already the one we independently implemented:

- Desktop GL: bounded positive sampler LOD **bias plus a minimum-LOD floor**;
  floor-only and additive bias are demonstrably different under minification.
- Saved Potato On: **depth-independent** inspection on the selected owned
  drawable. Depth testing is not a prerequisite for selecting existing mips.
- Private draw-scoped samplers, exact binding restoration and untouched source
  descriptors, rather than the originals' persistent process-wide mutations.
- GLES: a legal **floor-only** path, not an unsupported desktop bias enum.
  This is explicitly not claimed to be equivalent to the desktop original.

See [production compatibility behavior and pixel evidence](POTATO-DEPTH-COMPATIBILITY.md)
and [desktop bias versus floor](POTATO-DESKTOP-BIAS.md). The new package supplies
**no different DLL, new backend, missing constant or fixed proxy** to transplant.
Repeating execution of identical binaries would not answer a new technical
question, so none was scheduled. No production gate is removed, no proxy is
installed and no original code is incorporated because of this repack.

This follow-up changes **reference evidence and its reproducibility, not
production rendering code**. Existing software-context pixel/restoration tests
show actual coarser-mip sampling; installed-game changed textures, in-place game
Off, physical-driver/startup compatibility and an FPS benefit are still not
established. Positive LOD selects existing mips; it does not shrink allocations,
change geometry or prove safe anti-cheat/account behavior. Stock native Settings,
Save/Discard/restart semantics and Shaders-before-About are unchanged.

## Reproduce without running a supplied DLL

`tests/potato-dll/compare_references.py` reads both fixed-hash local archives as
data. It verifies bounded inventories, normalized paths, duplicate names,
symlinks, file sizes, AMD64 DLL headers, full bytes and duplicate multiplicity.
It never loads a DLL or writes extracted files, performs no network request,
and only optionally writes a **new JSON metadata report**. A differing payload
returns failure; bad identity/unsafe input fails before a report is written.

Use `rarfile==4.2` and a trusted upstream UnRAR executable. The actual review
used the same independently built upstream UnRAR commit
[`cacdcb8cbe1813b6151e3f92f2ccec7afe4d6a41`](https://github.com/pmachapman/unrar/commit/cacdcb8cbe1813b6151e3f92f2ccec7afe4d6a41)
as the previous review, never an executable from an archive.
With the two user-provided archives downloaded locally:

```sh
BSI_POTATO_PREVIOUS_PASSWORD=70K BSI_POTATO_NEW_PASSWORD=ANSHUXPOTATO \
python tests/potato-dll/compare_references.py \
  --previous /path/to/previous.rar --new /path/to/anshu.rar \
  --unrar /path/to/trusted/unrar --report /path/to/new-comparison.json
```

The real local run produced the committed comparison with
`identicalDllContents: true`, six files, five unique byte sequences and all six
complete-byte matches. Independently parsing the new PE files with `pefile`
also confirmed the same import/export/section identities by complete-file match.

**26 offline tests passed**: 20 comparison/evidence tests plus the existing six
preparation tests. They use synthetic data or committed JSON only, not the
private archives, decompressor, game, network or original DLL execution. They
cover renamed matches, full-byte mismatch despite a forced equal hash, missing
variants/duplicates, pre-decompression bounds, bad PE headers and unsafe paths.
Ordinary CI now runs them without opting in to the separate execution workflow:

```sh
python -m unittest discover -s tests/potato-dll -p '*_test.py' -v
```
