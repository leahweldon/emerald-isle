# Credits

## Terrain textures

From [ambientCG](https://ambientcg.com/), released under [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/). Only the 1K JPG colour and OpenGL normal maps are included, in `assets/textures/materials/`.

| Use | ambientCG asset | URL |
|---|---|---|
| Lowland grass | Grass 004 | https://ambientcg.com/view?id=Grass004 |
| Steep rock | Rock 058 | https://ambientcg.com/view?id=Rock058 |
| Sand and beach | Ground 054 | https://ambientcg.com/view?id=Ground054 |
| Upland heath and bog (tinted darker in the shader) | Ground 037 | https://ambientcg.com/view?id=Ground037 |

## Water normal maps

`assets/textures/water/` is generated from a random wave spectrum by `tools/make_water_normals.py`; no third-party assets.

## Elevation data

Contains modified Copernicus DEM data, © DLR e.V. 2010–2014 and © Airbus Defence and Space GmbH 2014–2018, provided under COPERNICUS by the European Union and ESA. See [tools/README.md](tools/README.md).

## Panel font

The in-app panel uses Roboto Medium by Christian Robertson, released under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0). It ships with Dear ImGui (`misc/fonts/Roboto-Medium.ttf`) and is copied next to the executable at build time.
