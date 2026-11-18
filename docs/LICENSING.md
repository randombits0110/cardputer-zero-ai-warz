# Licensing and distribution

AI Wars' original code is licensed under [MIT](../LICENSE). Third-party
components retain their own licenses; see [THIRD_PARTY_NOTICES.txt](../THIRD_PARTY_NOTICES.txt)
and the complete texts in [LICENSES](../LICENSES/README.md).

This software is based in part on the work of the FreeType Team.
The project elects FreeType's FTL alternative. Separate notices for contributed
FreeType code remain in its upstream license collection.

## Release contents

Keep `LICENSE`, `THIRD_PARTY_NOTICES.txt`, and the complete `LICENSES/` directory
with source releases and portable binary distributions. The shared CMake install
rules put the third-party bundle under `share/doc/ai-wars/` in future packages.
The platform install rules already install the main `LICENSE` there. The font
and audio notices also remain beside those assets where the packaging rules
install them.

Adding these files does not update an existing `.deb`, Windows installer, or
source ZIP. Rebuild or repackage release artifacts before publishing them, and
regenerate their content inventories and checksums. In particular, the earlier
Windows installer predates the more recent changes made only in the extracted
CardputerZero source directory.

The checked-in `sbom.cdx.json` describes the default CardputerZero build. It is
not a Windows software inventory. Use the final Windows package's actual DLLs
and embedded components when reviewing its notices.

## Windows runtime redistribution

The Windows package includes Microsoft Visual C++ runtime DLLs. Their presence
on a developer machine is not proof of redistribution entitlement. Confirm that
the developer toolchain is properly licensed and that the shipped files are
permitted, unmodified, non-preview release redistributables. Do not distribute
debug runtimes. A developer's individual license eligibility cannot be
established by inspecting this source repository.

See the [Microsoft runtime notice](../LICENSES/Microsoft-VC-Runtime-Notice.txt),
[official redistribution list](https://learn.microsoft.com/en-us/visualstudio/releases/2026/redistribution),
and [Visual Studio license directory](https://visualstudio.microsoft.com/license-terms/).
Those official terms govern the Microsoft components; the project notice does
not grant additional Microsoft rights.

## Installer components

NSIS has component-specific licenses. Its upstream copying file, including the
LZMA special exception, is preserved in `LICENSES/NSIS-COPYING.txt`. The exception
allows linked application code to keep its separate license; changes to the LZMA
module remain subject to the CPL. If redistributing modified installer modules,
review the applicable source-availability and notice obligations separately.
Unmodified NSIS 3.13 source is available from the
[NSIS 3.13 source release](https://sourceforge.net/projects/nsis/files/NSIS%203/3.13/).

## Fonts and assets

Montserrat and the embedded Font Awesome font glyphs retain OFL licensing and
their copyright notices. Do not sell font software on its own or use a declared
Reserved Font Name for a modified font without permission. This does not change
the license of the game's original code.

See [asset provenance](ASSET-LICENSING.md) for the shipped asset categories and
the distinction between software copyright and third-party branding rights.

## Source of the notices

Upstream license files were copied byte for byte from the actual locally
available dependency sources/packages. `LICENSES/manifest.json` identifies
their public source references and SHA-256 hashes. Two notices are verbatim
extracts: TLSF's full copyright/license comment and FreeType's FTL section.
The JSON contributor-attribution file preserves the relevant comments from
the vendored header. The Microsoft notice and this guide are project-authored
summaries, not substitutes for an upstream license or legal opinion.
