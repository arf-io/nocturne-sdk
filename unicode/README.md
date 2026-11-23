# Unicode 17.0.0 data

The vendored files retain the Unicode Consortium's original bytes. The
[manifest](17.0.0/SOURCES.json) records their official URLs and SHA-256
digests. Official downloads were checked byte for byte against the
reference Git objects at `53191655ec77e41e0a3453afbc08f8dccfcdf4a5`
under `native/unicode/17.0.0`. The [Unicode License V3](17.0.0/LICENSE.txt)
accompanies the data and the installed package.

Grapheme segmentation uses the properties in `GraphemeBreakProperty.txt`,
`DerivedCoreProperties.txt`, and `emoji-data.txt`. The table generator
combines them into sorted non-default ranges. The generated C file is
committed so compilation needs only a C99 compiler and CMake. Width
classification adds East Asian Width, Unicode general categories, default
ignorables, grapheme extenders, emoji properties and registered presentation
sequences. The analysis generator adds break classes and predicates,
bidi defaults, brackets and mirrors, and primary and extension scripts.
CTest runs the complete pinned LineBreakTest, BidiTest and BidiCharacterTest
fixtures through the public UTF-8 analysis APIs.

This implementation follows [UAX #29 revision 47](https://www.unicode.org/reports/tr29/tr29-47.html)
and exercises the byte-offset API against every row of
`GraphemeBreakTest.txt`. The old `native/src/grapheme.c` was consulted for
cursor and rule ordering. The historical tree's
`native/src/text_width.c` and `native/src/text_identities.c` were consulted
for width classification and identity reconciliation. The current modules
use counted UTF-8, explicit two-axis width policy, edit-range provenance and
fresh public storage. The line-breaking, bidi and script algorithms use
rule logic from the corresponding MIT-licensed native modules at the
same reference commit. The port keeps the source copyright and replaces
scene-era limits and interleaved scalar buffers with standalone UTF-8
interfaces and allocator-backed storage. The [analysis contract](../docs/CONTRACTS.md#unicode-line-breaking-bidi-and-script-runs)
records the line and script policies. The [width contract](../docs/CONTRACTS.md#terminal-glyph-width)
records the terminal rules exercised by `tests/text_storage.c`.

Maintainer checks

```sh
python3 tools/generate_unicode_grapheme.py --check
python3 tools/generate_unicode_width.py --check
python3 tools/generate_unicode_analysis.py --check
python3 tools/generate_unicode_grapheme.py --check --verify-upstream
ctest --test-dir build -R 'unicode_(grapheme|analysis)' --output-on-failure -V
```

All three generators check pinned data hashes offline. The optional upstream check
compares the files to their official URLs at the time it runs, including the
live license URL.
