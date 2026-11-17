# GitHub artwork

The English and Arabic product pages use `hero-typography.png`: a typography-led
banner with a flat audio-filter illustration, navy and cobalt on off-white.
It was generated with the built-in imagegen tool. The final prompt is preserved
in [hero-typography-prompt.txt](hero-typography-prompt.txt).

`hero.svg` and `hero.png` are the previous filtering diagram, retained as editable
source and its export. `tools/render-github-artwork.cjs` regenerates only that
legacy `hero.png`, using `@resvg/resvg-js` and the bundled Readex Pro font under
the SIL Open Font License. It does not overwrite the new banner.

The feature and platform icons are in `icons/`. App resources are unchanged.
