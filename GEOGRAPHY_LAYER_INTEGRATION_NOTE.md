# Geography Layer Integration Note (2026-02-13)

## Investigation Findings
- Canonical world pixel size is driven by `map.png` (`baseImage`). Current assets are `1920x1080`.
- Canonical simulation grid is `m_countryGrid` / `m_isLandGrid` with:
  - `gridCellSize = 1` in both GUI (`src/main.cpp`) and CLI (`src/cli_main.cpp`)
  - so simulation tile coordinates currently map 1:1 to map pixels.
- Pixel/grid mapping path:
  - `Map::pixelToGrid`: `grid = floor(pixel / gridCellSize)`
  - multiple systems consume `m_isLandGrid` (roads, spawning, climate/field masks, sea nav, expansion checks).
- Previous terrain derivation assumption:
  - land/water was derived by comparing base map pixel to a single `landColor` in `Map` constructor.
- Resource semantics path:
  - `resource.png`, `coal.png`, `copper.png`, `tin.png`, `riverland.png` are loaded separately and consumed in `Map::initializeResourceGrid`.
  - resource logic is independent from base texture rendering and should remain unchanged.
- Spawn mask alignment behavior:
  - spawn color sampling in `initializeCountries` already used normalized sampling from grid -> image dimensions.
  - other older spawn helpers assumed equal dimensions.
- Projection/wrapping assumptions:
  - globe rendering and picking use equirectangular lon/lat UV mapping (`tu`, `tv`).
  - horizontal wrap is explicit in globe shader/pick path (`fract`/equivalent wrap of `u`).
  - simulation/topology logic itself does not wrap neighbors across left/right world edges.

## Integration Plan
1. Replace base-color terrain detection with strict `landmask.png` ingestion.
2. Add `heightmap.png` ingestion into a dedicated elevation grid (`0..1`, raw grayscale).
3. Keep base visual map (`map.png`) for rendering unchanged.
4. Keep resource layers and semantics unchanged.
5. Use one shared nearest-neighbor grid->image sampling path for geography layers (and spawn sampling) to support dimension mismatches without shifting world coordinates.
6. Keep `spawn.png` interpretation unchanged; enforce startup alignment check between spawn/resource dimensions.
7. Add startup dimension logs for `map`, `landmask`, `heightmap`, `resource`, `spawn`.
8. Add debug overlays:
  - landmask overlay
  - heightmap grayscale overlay
9. Add CLI debug sampling mode to print at fixed coordinates:
  - `is_land`
  - `elevation`
  - resource presence summary
  - spawn flag

## Sea Level Handling
- Land/water authority is `landmask.png`, not `heightmap.png`.
- Elevation is stored as raw normalized grayscale from heightmap (`(r+g+b)/3 / 255`).
- If sea-level thresholding is needed for future diagnostics, `0.5` (mid-gray) is the default reference threshold; it is not used to override landmask decisions.

## Coordinate and Color Rules
- Landmask interpretation is strict:
  - land: `RGB(255,255,255)`
  - water: `RGB(0,0,0)`
  - any other RGB: warning; non-zero RGB treated as land.
- No Y-flip is applied; origin remains top-left for all image layers.
- No sRGB/linear transforms are applied; raw channel bytes are used.
