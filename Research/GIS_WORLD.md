# GIS world integration — 2026-09-19

The installed world uses a 1920 x 960 WGS84 / EPSG:4326 grid, with bounds
(-180, -90, 180, 90), north at the top, pixel centers at half-cell offsets,
and 0.1875-degree cells. The renderer letterboxes this into 1920 x 1080.
All downloads and GIS processing happen offline; the executable needs no network.

## Sources and licensing

| Source | Use | Terms |
| --- | --- | --- |
| [Natural Earth II](https://www.naturalearthdata.com/downloads/10m-natural-earth-2/10m-natural-earth-ii-with-shaded-relief-and-water/) | Downsampled visible terrain | Public domain |
| [Natural Earth physical vectors](https://www.naturalearthdata.com/downloads/10m-physical-vectors/) | Land polygons and natural lakes; reservoir features excluded | Public domain |
| [Natural Earth country polygons](https://www.naturalearthdata.com/downloads/10m-cultural-vectors/) | Geographic grouping for the existing scenario spawn regions only; does not prescribe simulated countries | Public domain |
| [NOAA ETOPO 2022](https://www.ncei.noaa.gov/products/etopo-global-relief-model) | 60-arc-second surface elevation/bathymetry, aggregated to game grid | NOAA public data; cite DOI 10.25921/fd45-gt74 |
| [HydroRIVERS v1](https://www.hydrosheds.org/products/hydrorivers) | River corridors with mean discharge >=10 m3/s; display only major reaches >=1000 m3/s | Free scientific/educational/commercial use under HydroSHEDS terms; Lehner & Grill (2013) |
| [USGS MRDS](https://mrdata.usgs.gov/mrds/) | Copper, tin, iron, gold, salt and coal occurrence coordinates | USGS public data |
| [USGS World Coal Quality Inventory](https://pubs.usgs.gov/of/2010/1196/) | Additional coal occurrence coordinates | USGS public data, retain source caveats |
| [ISRIC SoilGrids 2.0](https://docs.isric.org/globaldata/soilgrids/SoilGrids_faqs.html) | Clay content and pH, top 0–5 cm | CC BY 4.0; Poggio et al. (2021), SOIL 7, 217–240 |
| [CHELSA-TraCE21k bioclim](https://www.chelsa-climate.org/datasets/chelsa-trace21k-centennial-bioclim) | Annual mean temperature and annual precipitation | CC0; Karger et al. (2023), doi:10.5194/cp-19-439-2023; dataset doi:10.16904/envidat.211 |

Exact download URLs, output hashes, coverage counts, and assumptions are in
`assets/images/manifest.json`. Raw files and original asset backups are in the
ignored `out/gis_sources/` directory. Source rasters are retained there; their
multi-gigabyte native grids are never allocated by the game.

## Runtime files and behavior

- `map.png`: Natural Earth II with a consistent ocean/lake mask and major rivers.
- `landmask.png`: binary land/water; lakes are water, reservoirs omitted.
- `heightmap.png`: ETOPO elevation clipped to 0–9000 m, encoded 0–255 to preserve
  the existing normalized elevation API. `elevation_m.tif` retains actual meters
  and negative bathymetry as a georeferenced master at the game resolution.
- `riverland.png`: categorical HydroRIVERS mask driving existing riverland food,
  clay and corridor mechanics. River presence is not a measured floodplain width
  or a navigability guarantee. Sub-cell rivers remain line/corridor features;
  they do not turn an entire 20-km cell into uninhabitable water.
- `coal.png`, `copper.png`, `tin.png`, `iron.png`, `gold.png`, `salt.png`: independent
  categorical occurrence layers. Overlapping minerals are retained. Points mark
  their containing cells; only coastal mismatches within two cells snap to land.
  Existing deterministic resource-amount formulas remain gameplay abstractions.
- `resource.png`: modeled Eurasian steppe horse habitat; **not** a sourced ancient
  animal-distribution map. Mineral data no longer share this single-color layer.
- `spawn.png`: regenerated for the new coordinates, retaining all 21 existing
  scenario regions and their configured population shares. Geographic region
  boundaries are scenario approximations, not reconstructed 5000 BCE borders.
- `soil.png`: R=clay fraction x255, G=pH/14 x255, B=modeled agricultural suitability
  x255, A=valid coverage. The suitability equation is explicitly a gameplay proxy,
  not measured ancient yield. Unknown cells use the engine's neutral fallback.
- `climate.bin`: magic `WSCLIM01`, little-endian uint32 width/height/frame count,
  then per frame int32 nominal year, float32 temperature Celsius array, float32
  annual precipitation mm array. Six 320x160 frames use source century identifiers
  -200, -150, -100, -050, 0000 and 0020 as nominal game years -20000 to 2000.
  These are coarse century labels, not exact yearly dates. The engine linearly
  interpolates, clamps beyond the endpoints, converts precipitation to the
  existing 0–1 moisture scale with mm/(mm+600), and adds procedural weather.
  Missing cells use the old procedural baseline. The old global synthetic paleo
  adjustment is not added again to valid reconstructed climate. Settlement
  forcing uses the GIS climate instead of double-counting synthetic paleoclimate.

The manifest opts into the complete pack. Required GIS image sizes, climate
header dimensions, dates, finite ranges and payload length are validated before
use. Legacy packs without a manifest keep the old behavior.

## Rebuild assets

Python dependencies: numpy, pillow, requests, rasterio, geopandas, pandas, scipy,
xlrd. This workstation already provided the GIS dependencies; xlrd is installed
only under `out/gis_sources/python_deps` (the script includes that path).

```powershell
python tools/prepare_gis_world.py
python tools/prepare_gis_world.py --environment
python tools/prepare_gis_world.py --prepare
python tools/prepare_gis_world.py --install
python tools/validate_gis_world.py
build.bat release
```

The preparation stage validates before installing and backs up replaced images.
The climate downloader reads remote raster overviews into cached local arrays.
ISRIC WCS provides coarse global clay/pH extracts; those are samples, not an
exhaustive average of every original 250-m observation.

## Important limits

This is a modern geographic foundation with historical climate estimates. It
does not reconstruct changing ancient coasts, ice sheets, river courses, or
biome coloring over time. The image stays visually static as climate evolves.
Mineral surveys are incomplete, especially outside the US, and unknown deposits
are not generated automatically. Deposit richness and ancient accessibility are
not inferred from modern production. The game still uses legacy pixel-based
territory, distance, transport and capacity formulas; geodesic distances and
equal-area economic accounting are a separate engine change.

## Startup and validation

`WorldSimulation.exe --smoke-test` runs the real GUI initialization with a hidden
1920x1080 window, draws the startup interface, spawns 100 countries, renders the
world, writes `out/gis_validation/startup.png` and exits normally. It requires a
working graphics driver, like the normal game. It is not a substitute for long
simulation tests. The game locates assets beside the EXE when the launch working
directory has no world, and falls back to windowed mode if fullscreen is absent.

Geographic validation checks output hashes, every categorical layer's colors
and land alignment, known ocean/land/lake coordinates, Tibet/Pacific elevations,
all spawn regions, and climate orientation/ranges. Run results and build logs
are retained under `out/gis_validation/`.

Verified on this workstation: release build succeeded; packaged GUI, root
`WorldSimulation.exe`, and the existing `x64/Release/sfmltest.exe` launch location
all passed from an unrelated working directory. Seed 42 completed 50 years with
100 countries and no invariant failures; an ice-age start completed five years.
A deliberately truncated climate file produced a controlled diagnostic exit.
Original images and the old `sfmltest.exe` are backed up under `out/gis_sources/`.
These checks establish tested startup/run stability, not a guarantee against
every possible long-run simulation fault.
