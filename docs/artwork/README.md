# GitHub artwork

The English and Arabic product pages use `hero-v2.png`: a typographic cover with
the existing white-and-blue Sukut mark and a restrained audio illustration.
It was generated with the built-in imagegen tool. The final prompt is preserved
in [hero-v2-prompt.txt](hero-v2-prompt.txt). No app icons or platform resources were changed.

`hero.svg` and `hero.png` are the previous filtering diagram, retained as editable
source and its export. `tools/render-github-artwork.cjs` only regenerates that
legacy `hero.png`; it does not overwrite the new banner. The renderer uses
`@resvg/resvg-js` and the bundled Readex Pro font under the SIL Open Font License.

The feature and platform icons are in `icons/`.
