# GitHub artwork

The pages use `hero-dark-en.svg` and `hero-dark-ar.svg`: dark typography-led
banners with a flat audio-filter illustration, off-white text and blue accents.
The SVG containers embed the generated bitmap over an opaque dark backdrop and
clip the rounded outer corners, keeping the interior solid on any page theme.
The previous light PNG versions are retained.
The bitmap artwork was generated with the built-in imagegen tool. Dark-version
prompts are in [dark-artwork-prompts.txt](dark-artwork-prompts.txt). The final prompt is preserved
in [hero-typography-prompt.txt](hero-typography-prompt.txt) and
[hero-typography-ar-prompt.txt](hero-typography-ar-prompt.txt).

`hero.svg` and `hero.png` are the previous filtering diagram, retained as editable
source and its export. `tools/render-github-artwork.cjs` regenerates only that
legacy `hero.png`, using `@resvg/resvg-js` and the bundled Readex Pro font under
the SIL Open Font License. It does not overwrite the new banner.

The feature and platform icons are in `icons/`. App resources are unchanged.

## Store badges

Official, unmodified English and Arabic assets in `stores/`:

- Microsoft Store: `https://get.microsoft.com/images/{en-us,ar}%20dark.svg`
- Google Play: `https://play.google.com/intl/en_us/badges/static/images/badges/{en,ar}_badge_web_generic.png`
- Snap Store: `https://snapcraft.io/{en,ar}/dark/install.svg`

Google Play is marked coming soon while its listing is in review.
