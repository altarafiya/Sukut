# GitHub artwork

`hero.svg` illustrates the Music Filter mode. The English and Arabic project
READMEs display its PNG export. The feature and platform icons are in `icons/`.

To regenerate `hero.png`, provide `@resvg/resvg-js` and run
`node tools/render-github-artwork.cjs` from the repository root. The renderer
uses the bundled Readex Pro font under the SIL Open Font License.
