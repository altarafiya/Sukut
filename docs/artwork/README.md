# GitHub artwork

`hero.svg` illustrates the Music Filter mode. The project README displays
its PNG export and the same gold app icon used by the desktop and Android apps.
The feature and platform icons are in `icons/`.

To regenerate `hero.png`, provide `@resvg/resvg-js` and run
`node tools/render-github-artwork.cjs` from the repository root. The renderer
uses the bundled Readex Pro font under the SIL Open Font License.
