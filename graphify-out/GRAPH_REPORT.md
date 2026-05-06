# Graph Report - .  (2026-05-06)

## Corpus Check
- 31 files · ~799,177 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 584 nodes · 1473 edges · 59 communities detected
- Extraction: 59% EXTRACTED · 41% INFERRED · 0% AMBIGUOUS · INFERRED: 611 edges (avg confidence: 0.5)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Community 0|Community 0]]
- [[_COMMUNITY_Community 1|Community 1]]
- [[_COMMUNITY_Community 2|Community 2]]
- [[_COMMUNITY_Community 3|Community 3]]
- [[_COMMUNITY_Community 4|Community 4]]
- [[_COMMUNITY_Community 5|Community 5]]
- [[_COMMUNITY_Community 6|Community 6]]
- [[_COMMUNITY_Community 7|Community 7]]
- [[_COMMUNITY_Community 8|Community 8]]
- [[_COMMUNITY_Community 9|Community 9]]
- [[_COMMUNITY_Community 10|Community 10]]
- [[_COMMUNITY_Community 11|Community 11]]
- [[_COMMUNITY_Community 12|Community 12]]
- [[_COMMUNITY_Community 13|Community 13]]
- [[_COMMUNITY_Community 14|Community 14]]
- [[_COMMUNITY_Community 15|Community 15]]
- [[_COMMUNITY_Community 16|Community 16]]
- [[_COMMUNITY_Community 17|Community 17]]
- [[_COMMUNITY_Community 18|Community 18]]
- [[_COMMUNITY_Community 19|Community 19]]
- [[_COMMUNITY_Community 20|Community 20]]
- [[_COMMUNITY_Community 21|Community 21]]
- [[_COMMUNITY_Community 22|Community 22]]
- [[_COMMUNITY_Community 23|Community 23]]
- [[_COMMUNITY_Community 24|Community 24]]
- [[_COMMUNITY_Community 25|Community 25]]
- [[_COMMUNITY_Community 26|Community 26]]
- [[_COMMUNITY_Community 27|Community 27]]
- [[_COMMUNITY_Community 28|Community 28]]
- [[_COMMUNITY_Community 29|Community 29]]
- [[_COMMUNITY_Community 30|Community 30]]
- [[_COMMUNITY_Community 31|Community 31]]
- [[_COMMUNITY_Community 32|Community 32]]
- [[_COMMUNITY_Community 33|Community 33]]
- [[_COMMUNITY_Community 34|Community 34]]
- [[_COMMUNITY_Community 35|Community 35]]
- [[_COMMUNITY_Community 36|Community 36]]
- [[_COMMUNITY_Community 37|Community 37]]
- [[_COMMUNITY_Community 38|Community 38]]
- [[_COMMUNITY_Community 39|Community 39]]
- [[_COMMUNITY_Community 40|Community 40]]
- [[_COMMUNITY_Community 41|Community 41]]
- [[_COMMUNITY_Community 42|Community 42]]
- [[_COMMUNITY_Community 43|Community 43]]
- [[_COMMUNITY_Community 44|Community 44]]
- [[_COMMUNITY_Community 45|Community 45]]
- [[_COMMUNITY_Community 46|Community 46]]
- [[_COMMUNITY_Community 47|Community 47]]
- [[_COMMUNITY_Community 48|Community 48]]
- [[_COMMUNITY_Community 49|Community 49]]
- [[_COMMUNITY_Community 50|Community 50]]
- [[_COMMUNITY_Community 51|Community 51]]
- [[_COMMUNITY_Community 52|Community 52]]
- [[_COMMUNITY_Community 53|Community 53]]
- [[_COMMUNITY_Community 54|Community 54]]
- [[_COMMUNITY_Community 55|Community 55]]
- [[_COMMUNITY_Community 56|Community 56]]
- [[_COMMUNITY_Community 57|Community 57]]
- [[_COMMUNITY_Community 58|Community 58]]

## God Nodes (most connected - your core abstractions)
1. `Hct` - 149 edges
2. `Color` - 95 edges
3. `TonalPalette` - 72 edges
4. `SchemeContent` - 58 edges
5. `SchemeMonochrome` - 58 edges
6. `SchemeTonalSpot` - 57 edges
7. `SchemeFruitSalad` - 57 edges
8. `SchemeRainbow` - 57 edges
9. `TemplateRenderer` - 48 edges
10. `Cam16` - 47 edges

## Surprising Connections (you probably didn't know these)
- `Color` --uses--> `Contrast calculation utilities (WCAG luminance and contrast).  This module provi`  [INFERRED]
  Scripts/python/src/theming/lib/color.py → Scripts/python/src/theming/lib/contrast.py
- `Color` --uses--> `Calculate relative luminance per WCAG 2.1.      The formula converts sRGB to lin`  [INFERRED]
  Scripts/python/src/theming/lib/color.py → Scripts/python/src/theming/lib/contrast.py
- `Color` --uses--> `Calculate WCAG contrast ratio between two colors.      Returns a value between 1`  [INFERRED]
  Scripts/python/src/theming/lib/color.py → Scripts/python/src/theming/lib/contrast.py
- `Color` --uses--> `Determine if a color is perceptually dark.`  [INFERRED]
  Scripts/python/src/theming/lib/color.py → Scripts/python/src/theming/lib/contrast.py
- `Color` --uses--> `Adjust foreground color to meet minimum contrast ratio against background.`  [INFERRED]
  Scripts/python/src/theming/lib/color.py → Scripts/python/src/theming/lib/contrast.py

## Communities

### Community 0 - "Community 0"
Cohesion: 0.12
Nodes (74): Color, Represents a color with RGB values (0-255)., Parse hex color string (#RRGGBB or RRGGBB)., Convert to hex string (#RRGGBB)., Create Color from HSL values., Create Color from HCT., Hct, A palette of tones for a single hue and chroma.      Material Design 3 uses spec (+66 more)

### Community 1 - "Community 1"
Cohesion: 0.05
Nodes (56): _are_in_cyclic_order(), argb_to_int(), _bisect_to_limit(), _bisect_to_segment(), _chromatic_adaptation(), _critical_plane_above(), _critical_plane_below(), _delinearize() (+48 more)

### Community 2 - "Community 2"
Cohesion: 0.07
Nodes (42): Cam16, CAM16 color appearance model representation., _argb_from_rgb(), Box, _difference_degrees(), extract_source_color(), _get_index(), _lab_distance_squared() (+34 more)

### Community 3 - "Community 3"
Cohesion: 0.12
Nodes (3): _split_words(), TemplateRenderer, VariableScope

### Community 4 - "Community 4"
Cohesion: 0.07
Nodes (36): adjust_lightness(), adjust_surface(), _delinearize(), find_closest_color(), from_hct(), from_hex(), from_hsl(), from_rgb() (+28 more)

### Community 5 - "Community 5"
Cohesion: 0.1
Nodes (28): contrast_ratio(), ensure_contrast(), get_contrasting_color(), is_dark(), Contrast calculation utilities (WCAG luminance and contrast).  This module provi, Get a contrasting foreground color (black or white variant)., Calculate relative luminance per WCAG 2.1.      The formula converts sRGB to lin, Calculate WCAG contrast ratio between two colors.      Returns a value between 1 (+20 more)

### Community 6 - "Community 6"
Cohesion: 0.11
Nodes (15): Convert HCT to sRGB, solving for the color., Convert HCT to hex string., Color temperature analysis for finding harmonious colors.      Based on Material, Generate HCT colors at regular hue intervals., Cache temperatures for all hue variants., Get HCT colors sorted by temperature., Calculate relative temperature (0-1) based on position in temperature-sorted lis, Get relative temperature of the input color. (+7 more)

### Community 7 - "Community 7"
Cohesion: 0.11
Nodes (25): _circular_hue_diff(), downsample_pixels(), extract_palette(), _family_center_hue(), find_error_color(), _hue_to_family(), kmeans_cluster(), Palette extraction using K-means clustering.  This module provides functions for (+17 more)

### Community 8 - "Community 8"
Cohesion: 0.15
Nodes (20): Exception, ImageReadError, _paeth_predictor(), _parse_ppm(), _png_unfilter(), Image reading utilities for PNG and JPEG files.  This module provides functions, Apply PNG filter reconstruction., Paeth predictor for PNG filter reconstruction. (+12 more)

### Community 9 - "Community 9"
Cohesion: 0.15
Nodes (19): build_scope_visibility(), build_tab_mappings(), extract_entries(), extract_widget_blocks(), get_subtab_info(), is_resolvable_condition(), main(), parse_component_declarations() (+11 more)

### Community 10 - "Community 10"
Cohesion: 0.17
Nodes (19): analyze_vibrant_faithful_muted(), compare_m3_schemes(), get_hct(), hex_to_rgb(), hue_diff(), hue_to_name(), main(), Analyze vibrant, faithful, dysfunctional, and muted mode outputs. (+11 more)

### Community 11 - "Community 11"
Cohesion: 0.26
Nodes (15): adjustLightness(), adjustLightnessAndSaturation(), adjustSaturation(), generateContainerColor(), generateOnColor(), generateSurfaceVariant(), getContrastRatio(), getLuminance() (+7 more)

### Community 12 - "Community 12"
Cohesion: 0.15
Nodes (17): expand_predefined_scheme(), _hex_to_color(), inject_terminal_colors(), _interpolate_color(), _make_container_dark(), _make_container_light(), _make_fixed_dark(), _make_fixed_light() (+9 more)

### Community 13 - "Community 13"
Cohesion: 0.18
Nodes (4): selectNext(), selectNextWrapped(), selectPrevious(), selectPreviousWrapped()

### Community 14 - "Community 14"
Cohesion: 0.27
Nodes (12): apply_gtk3_colors(), apply_gtk4_colors(), ensure_gtk_css_import(), get_config_dir(), main(), parse_args(), Push light/dark to org.gnome.desktop.interface (gsettings or dconf fallback)., Check if a GTK theme exists in common locations. (+4 more)

### Community 15 - "Community 15"
Cohesion: 0.48
Nodes (6): convert_event(), from_khal(), get_khal_date_format(), main(), Read the khal config and extract the longdatetimeformat., to_khal()

### Community 16 - "Community 16"
Cohesion: 0.33
Nodes (0): 

### Community 17 - "Community 17"
Cohesion: 0.47
Nodes (5): get_registry(), is_valid_format(), migrate(), Check if the scheme data has the new terminal format., Fetch the remote registry to get correct paths for schemes.

### Community 18 - "Community 18"
Cohesion: 0.83
Nodes (3): rightRotate(), sha256(), stringToUtf8Bytes()

### Community 19 - "Community 19"
Cohesion: 0.67
Nodes (3): main(), parse_args(), Parse command-line arguments.

### Community 20 - "Community 20"
Cohesion: 1.0
Nodes (2): serializer(), stringify()

### Community 21 - "Community 21"
Cohesion: 0.67
Nodes (0): 

### Community 22 - "Community 22"
Cohesion: 1.0
Nodes (2): log(), pair_fast()

### Community 23 - "Community 23"
Cohesion: 1.0
Nodes (0): 

### Community 24 - "Community 24"
Cohesion: 1.0
Nodes (0): 

### Community 25 - "Community 25"
Cohesion: 1.0
Nodes (1): Return new HCT with different hue.

### Community 26 - "Community 26"
Cohesion: 1.0
Nodes (1): Return new HCT with different tone.

### Community 27 - "Community 27"
Cohesion: 1.0
Nodes (1): Return new HCT with different chroma.

### Community 28 - "Community 28"
Cohesion: 1.0
Nodes (0): 

### Community 29 - "Community 29"
Cohesion: 1.0
Nodes (0): 

### Community 30 - "Community 30"
Cohesion: 1.0
Nodes (0): 

### Community 31 - "Community 31"
Cohesion: 1.0
Nodes (0): 

### Community 32 - "Community 32"
Cohesion: 1.0
Nodes (0): 

### Community 33 - "Community 33"
Cohesion: 1.0
Nodes (1): Ensure angle is in [0, 2π) range.

### Community 34 - "Community 34"
Cohesion: 1.0
Nodes (1): Delinearize RGB component (0-100) to (0-255).

### Community 35 - "Community 35"
Cohesion: 1.0
Nodes (1): Apply chromatic adaptation.

### Community 36 - "Community 36"
Cohesion: 1.0
Nodes (1): Calculate hue of linear RGB color in radians.

### Community 37 - "Community 37"
Cohesion: 1.0
Nodes (1): Check if a, b, c are in cyclic order.

### Community 38 - "Community 38"
Cohesion: 1.0
Nodes (1): Solve lerp equation: find t such that lerp(source, target, t) = mid.

### Community 39 - "Community 39"
Cohesion: 1.0
Nodes (1): Linear interpolation between two 3D points.

### Community 40 - "Community 40"
Cohesion: 1.0
Nodes (1): Find point on segment where axis equals coordinate.

### Community 41 - "Community 41"
Cohesion: 1.0
Nodes (1): Check if x is in [0, 100].

### Community 42 - "Community 42"
Cohesion: 1.0
Nodes (1): Get nth vertex of RGB cube intersection with Y plane.          Returns [-1, -1,

### Community 43 - "Community 43"
Cohesion: 1.0
Nodes (1): Find segment on RGB cube containing target hue.

### Community 44 - "Community 44"
Cohesion: 1.0
Nodes (1): Calculate midpoint of two 3D points.

### Community 45 - "Community 45"
Cohesion: 1.0
Nodes (1): Get critical plane index below x.

### Community 46 - "Community 46"
Cohesion: 1.0
Nodes (1): Get critical plane index above x.

### Community 47 - "Community 47"
Cohesion: 1.0
Nodes (1): Find color on RGB cube boundary with exact target hue.          This is the key

### Community 48 - "Community 48"
Cohesion: 1.0
Nodes (1): Inverse of chromatic adaptation.

### Community 49 - "Community 49"
Cohesion: 1.0
Nodes (1): Try to find exact color with given hue, chroma, and Y.          Returns None if

### Community 50 - "Community 50"
Cohesion: 1.0
Nodes (1): Solve HCT to RGB with proper gamut mapping.          If the exact color is out o

### Community 51 - "Community 51"
Cohesion: 1.0
Nodes (1): Create CAM16 from sRGB values.

### Community 52 - "Community 52"
Cohesion: 1.0
Nodes (1): Create CAM16 from J (lightness), chroma, and hue.

### Community 53 - "Community 53"
Cohesion: 1.0
Nodes (1): Create HCT from sRGB values.

### Community 54 - "Community 54"
Cohesion: 1.0
Nodes (1): Create HCT from ARGB integer.

### Community 55 - "Community 55"
Cohesion: 1.0
Nodes (1): Solve for RGB given HCT values using the Material HctSolver.          This uses

### Community 56 - "Community 56"
Cohesion: 1.0
Nodes (1): Calculate raw temperature of a color using Ou-Woodcock-Wright algorithm.

### Community 57 - "Community 57"
Cohesion: 1.0
Nodes (1): Create TonalPalette from HCT color.

### Community 58 - "Community 58"
Cohesion: 1.0
Nodes (1): Create TonalPalette from RGB color.

## Knowledge Gaps
- **95 isolated node(s):** `Parse Component declarations from SettingsContent.qml.      Returns: component_i`, `Parse updateTabsModel() to get the ordered list of (source_id, label_key) pairs.`, `Build mappings from QML type name to tabsModel index and label key.      Parses`, `Parse a parent tab file to get subtab order and labels.      Returns: (subtab_ty`, `Determine the tab index, tab label, sub-tab index, and sub-tab label for a QML f` (+90 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **Thin community `Community 23`** (2 nodes): `QtObj2JS.js`, `qtObjectToPlainObject()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 24`** (2 nodes): `vscode-helper.py`, `find_all_noctalia_themes()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 25`** (2 nodes): `.set_hue()`, `Return new HCT with different hue.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 26`** (2 nodes): `.set_tone()`, `Return new HCT with different tone.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 27`** (2 nodes): `.set_chroma()`, `Return new HCT with different chroma.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 28`** (1 nodes): `BluetoothUtils.js`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 29`** (1 nodes): `ColorList.js`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 30`** (1 nodes): `check-calendar.py`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 31`** (1 nodes): `list-calendars.py`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 32`** (1 nodes): `kde-apply-scheme.py`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 33`** (1 nodes): `Ensure angle is in [0, 2π) range.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 34`** (1 nodes): `Delinearize RGB component (0-100) to (0-255).`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 35`** (1 nodes): `Apply chromatic adaptation.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 36`** (1 nodes): `Calculate hue of linear RGB color in radians.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 37`** (1 nodes): `Check if a, b, c are in cyclic order.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 38`** (1 nodes): `Solve lerp equation: find t such that lerp(source, target, t) = mid.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 39`** (1 nodes): `Linear interpolation between two 3D points.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 40`** (1 nodes): `Find point on segment where axis equals coordinate.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 41`** (1 nodes): `Check if x is in [0, 100].`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 42`** (1 nodes): `Get nth vertex of RGB cube intersection with Y plane.          Returns [-1, -1,`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 43`** (1 nodes): `Find segment on RGB cube containing target hue.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 44`** (1 nodes): `Calculate midpoint of two 3D points.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 45`** (1 nodes): `Get critical plane index below x.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 46`** (1 nodes): `Get critical plane index above x.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 47`** (1 nodes): `Find color on RGB cube boundary with exact target hue.          This is the key`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 48`** (1 nodes): `Inverse of chromatic adaptation.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 49`** (1 nodes): `Try to find exact color with given hue, chroma, and Y.          Returns None if`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 50`** (1 nodes): `Solve HCT to RGB with proper gamut mapping.          If the exact color is out o`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 51`** (1 nodes): `Create CAM16 from sRGB values.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 52`** (1 nodes): `Create CAM16 from J (lightness), chroma, and hue.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 53`** (1 nodes): `Create HCT from sRGB values.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 54`** (1 nodes): `Create HCT from ARGB integer.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 55`** (1 nodes): `Solve for RGB given HCT values using the Material HctSolver.          This uses`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 56`** (1 nodes): `Calculate raw temperature of a color using Ou-Woodcock-Wright algorithm.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 57`** (1 nodes): `Create TonalPalette from HCT color.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 58`** (1 nodes): `Create TonalPalette from RGB color.`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Hct` connect `Community 0` to `Community 1`, `Community 2`, `Community 3`, `Community 4`, `Community 5`, `Community 6`, `Community 7`, `Community 10`, `Community 25`, `Community 26`, `Community 27`?**
  _High betweenness centrality (0.260) - this node is a cross-community bridge._
- **Why does `Color` connect `Community 0` to `Community 3`, `Community 4`, `Community 5`, `Community 7`, `Community 10`, `Community 12`?**
  _High betweenness centrality (0.106) - this node is a cross-community bridge._
- **Why does `TemplateRenderer` connect `Community 3` to `Community 0`, `Community 5`?**
  _High betweenness centrality (0.057) - this node is a cross-community bridge._
- **Are the 137 inferred relationships involving `Hct` (e.g. with `Calculate circular hue difference.` and `Convert hex to RGB tuple.`) actually correct?**
  _`Hct` has 137 INFERRED edges - model-reasoned connections that need verification._
- **Are the 89 inferred relationships involving `Color` (e.g. with `Calculate circular hue difference.` and `Convert hex to RGB tuple.`) actually correct?**
  _`Color` has 89 INFERRED edges - model-reasoned connections that need verification._
- **Are the 66 inferred relationships involving `TonalPalette` (e.g. with `Theming library - Color extraction and theme generation.  This package provides:` and `_BaseScheme`) actually correct?**
  _`TonalPalette` has 66 INFERRED edges - model-reasoned connections that need verification._
- **Are the 54 inferred relationships involving `SchemeContent` (e.g. with `Theming library - Color extraction and theme generation.  This package provides:` and `Hct`) actually correct?**
  _`SchemeContent` has 54 INFERRED edges - model-reasoned connections that need verification._