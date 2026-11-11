# The work budget: the "before" (2026-11-05)

What one live frame costs on **Phone** quality (374 x 811 pixels: an iPhone 15 Pro Max held upright, 1 pixel
per point, 87%), counted by `loop budget projects` (`engine/render/frame_cost.*`) the way the live renderer
draws today. "today > seen": what's drawn now > what's left if off-screen things are skipped. "lamps": lamps each
surface works out > lamps giving 95% of its light. "lamp work": surfaces shaded x lamps, per pixel. The budget's
numbers are first guesses (`budgetFor` in `engine/render/frame_cost.cpp`) until the phone's benchmarks set them.
Run it again after each speed change; `--quality high` for High; a scene alone for the full breakdown.

```
scene                           things drawn   triangles (M)  draw calls  shaded   lamps (95%)   lamp work  over the budget on
                                today > seen    today > seen today > seen  /pixel   each > need      /pixel
bar                               1894 > 443     0.61 > 0.24   651 > 218    2.06    103 > 51.0         213  triangles, draw calls, surfaces, lamp work
ardane_01_home                      117 > 69     0.10 > 0.06     85 > 52    1.56       3 > 1.1           5  fits
ardane_02_market                   592 > 325     0.79 > 0.45     73 > 51    1.58      21 > 3.2          33  triangles, lamp work
ardane_03_alley                    116 > 107     0.08 > 0.08     54 > 52    1.60       3 > 2.1           5  fits
ardane_04_velasse                  243 > 123     0.22 > 0.22    200 > 80    2.18      10 > 2.3          22  surfaces
ardane_05_canals                   311 > 134     0.48 > 0.25    212 > 82    2.46       8 > 1.2          20  surfaces
ardane_06_sluice                   219 > 153     0.15 > 0.13   168 > 102    2.16       4 > 0.0           9  surfaces
ardane_07_qadesh                   232 > 133     0.12 > 0.11    180 > 95    1.70       1 > 0.4           2  fits
ardane_08_palace                   458 > 294     0.49 > 0.33     90 > 63    1.62       6 > 0.9          10  fits
ardane_09_cisterns                 155 > 130     0.16 > 0.12     15 > 15    1.90      11 > 4.0          21  fits
ardane_10_well                       45 > 32     0.03 > 0.01     22 > 13    1.85       2 > 1.0           4  fits
ardane_11_aelor                     393 > 98     3.53 > 1.26    175 > 30    1.99       1 > 0.0           2  triangles
ardane_12_garden                   896 > 513     3.94 > 1.63    110 > 81    2.06     28 > 16.9          58  triangles, surfaces, lamp work
ardane_13_roots                    601 > 499     0.42 > 0.37   323 > 239    1.77       9 > 4.4          16  fits
ardane_14_nexus                    474 > 171     0.49 > 0.34    113 > 98    1.60      18 > 6.5          29  lamp work
ardane_15_undercity                526 > 145     0.49 > 0.19     40 > 18    1.55     21 > 11.4          33  lamp work
ardane_16_core                      143 > 94     0.13 > 0.12     48 > 46    1.70      12 > 2.3          20  fits
ardane_17_return                    152 > 84     0.40 > 0.24     92 > 51    1.34       7 > 6.3           9  fits
ardane_18_hollow                  1430 > 684     2.35 > 0.72    240 > 90    1.68       0 > 0.0           0  triangles
ardane_19_scar                      172 > 32     0.10 > 0.02    146 > 32    1.31       2 > 0.3           3  fits
ardane_20_convergence              754 > 510     1.43 > 0.64   207 > 149    1.62      16 > 4.4          26  triangles, lamp work
ardane_21_battle                   613 > 199     0.69 > 0.36    227 > 97    1.95      14 > 6.4          27  triangles, lamp work
ardane_22_mind                     355 > 267     0.83 > 0.73     45 > 44    1.69      22 > 6.1          37  triangles, lamp work
ardane_23_after                    270 > 219     0.33 > 0.31   141 > 129    1.22       9 > 0.7          11  fits
ardane_24_epilogue                  178 > 92     0.24 > 0.11    128 > 66    1.83       6 > 0.1          11  fits
courtyard                            59 > 42     0.08 > 0.06     43 > 31    1.54       1 > 0.2           2  fits
cabin                                59 > 52     0.04 > 0.04     39 > 34    1.54       2 > 1.2           3  fits
the_gate                             32 > 30     0.02 > 0.02     22 > 22    1.20       0 > 0.0           0  fits
the_woods                         1766 > 600   33.33 > 10.73     34 > 34    2.49       0 > 0.0           0  triangles, surfaces
showcase_glade                  13082 > 7548     9.13 > 5.37   264 > 223    2.64       0 > 0.0           0  triangles, surfaces
showcase_hall                        79 > 48     0.00 > 0.00     10 > 10    1.55      22 > 6.5          34  lamp work
showcase_materials                  162 > 43     0.79 > 0.20      10 > 5    1.72       3 > 2.0           5  triangles
showcase_light                        18 > 5     0.02 > 0.01      12 > 4    1.52       6 > 2.8           9  fits
showcase_sky                         26 > 16     0.11 > 0.08      12 > 9    1.13       0 > 0.0           0  fits
showcase_fog                       350 > 222     6.34 > 3.76     54 > 46    1.78       0 > 0.0           0  triangles
showcase_physics                     31 > 21     0.06 > 0.03      11 > 5    1.62       1 > 0.9           2  fits
showcase_people                      47 > 26     0.04 > 0.02     24 > 19    1.52       1 > 0.8           2  fits
showcase_sound                        24 > 5     0.02 > 0.01      14 > 4    1.62       2 > 1.3           3  fits
showcase_film                        22 > 15     0.04 > 0.03     16 > 11    1.53       3 > 2.5           5  fits
showcase_story                       22 > 15     0.02 > 0.01     15 > 10    1.52       1 > 0.8           2  fits
showcase_settings                     16 > 7     0.06 > 0.04       9 > 4    1.51       1 > 1.0           2  fits
18 of 41 scenes over the Phone budget
```

**What it says:** two kinds of heavy. The forests draw far too many triangles (The Woods 33 M, the Glade 9 M,
the fog room 6 M, Ardane's garden 4 M): skipping what's off screen cuts that about 3x, and distant trees need
simpler versions (levels of detail). The bar is heavy on lamps: each pixel works out all 103 and half of them
really light it (51 give 95% of the light, 68 give 99%), because the bar's light is many small bulbs adding up;
lighting only the nearest lamps won't be enough there: many small lamps must be grouped or sampled a few per
frame and blended over frames.

## After skipping what's off screen (2026-11-06, `render.cull` on)

Each pass now draws only what its view can see (the camera, or the sun's shadow area). Pictures held at the
same moment are identical with it off and on (0.00% of pixels differ: the bar, The Woods at second 10), and the
test GPU's time per frame of The Woods fell 28% (5.0 s to 3.6 s, software graphics). 17 of 41 scenes are still
over the Phone budget: the forests by triangles even when only what's seen is drawn (simpler distant trees
next), the bar by lamps.

```
scene                           things drawn   triangles (M)  draw calls  shaded   lamps (95%)   lamp work  over the budget on
                                today > seen    today > seen today > seen  /pixel   each > need      /pixel
bar                                443 > 443     0.24 > 0.24   218 > 218    2.06    103 > 51.0         213  surfaces, lamp work
ardane_01_home                       69 > 69     0.06 > 0.06     52 > 52    1.56       3 > 1.1           5  fits
ardane_02_market                   325 > 325     0.45 > 0.45     51 > 51    1.58      21 > 3.2          33  lamp work
ardane_03_alley                    107 > 107     0.08 > 0.08     52 > 52    1.60       3 > 2.1           5  fits
ardane_04_velasse                  123 > 123     0.22 > 0.22     80 > 80    2.18      10 > 2.3          22  surfaces
ardane_05_canals                   134 > 134     0.25 > 0.25     82 > 82    2.46       8 > 1.2          20  surfaces
ardane_06_sluice                   153 > 153     0.13 > 0.13   102 > 102    2.16       4 > 0.0           9  surfaces
ardane_07_qadesh                   133 > 133     0.11 > 0.11     95 > 95    1.70       1 > 0.4           2  fits
ardane_08_palace                   294 > 294     0.33 > 0.33     63 > 63    1.62       6 > 0.9          10  fits
ardane_09_cisterns                 130 > 130     0.12 > 0.12     15 > 15    1.90      11 > 4.0          21  fits
ardane_10_well                       32 > 32     0.01 > 0.01     13 > 13    1.85       2 > 1.0           4  fits
ardane_11_aelor                      98 > 98     1.26 > 1.26     30 > 30    1.99       1 > 0.0           2  triangles
ardane_12_garden                   513 > 513     1.63 > 1.63     81 > 81    2.06     28 > 16.9          58  triangles, surfaces, lamp work
ardane_13_roots                    499 > 499     0.37 > 0.37   239 > 239    1.77       9 > 4.4          16  fits
ardane_14_nexus                    171 > 171     0.34 > 0.34     98 > 98    1.60      18 > 6.5          29  lamp work
ardane_15_undercity                145 > 145     0.19 > 0.19     18 > 18    1.55     21 > 11.4          33  lamp work
ardane_16_core                       94 > 94     0.12 > 0.12     46 > 46    1.70      12 > 2.3          20  fits
ardane_17_return                     84 > 84     0.24 > 0.24     51 > 51    1.34       7 > 6.3           9  fits
ardane_18_hollow                   684 > 684     0.72 > 0.72     90 > 90    1.68       0 > 0.0           0  triangles
ardane_19_scar                       32 > 32     0.02 > 0.02     32 > 32    1.31       2 > 0.3           3  fits
ardane_20_convergence              510 > 510     0.64 > 0.64   149 > 149    1.62      16 > 4.4          26  triangles, lamp work
ardane_21_battle                   199 > 199     0.36 > 0.36     97 > 97    1.95      14 > 6.4          27  lamp work
ardane_22_mind                     267 > 267     0.73 > 0.73     44 > 44    1.69      22 > 6.1          37  triangles, lamp work
ardane_23_after                    219 > 219     0.31 > 0.31   129 > 129    1.22       9 > 0.7          11  fits
ardane_24_epilogue                   92 > 92     0.11 > 0.11     66 > 66    1.83       6 > 0.1          11  fits
courtyard                            42 > 42     0.06 > 0.06     31 > 31    1.54       1 > 0.2           2  fits
cabin                                52 > 52     0.04 > 0.04     34 > 34    1.54       2 > 1.2           3  fits
the_gate                             30 > 30     0.02 > 0.02     22 > 22    1.20       0 > 0.0           0  fits
the_woods                          600 > 600   10.73 > 10.73     34 > 34    2.49       0 > 0.0           0  triangles, surfaces
showcase_glade                   7548 > 7548     5.37 > 5.37   223 > 223    2.64       0 > 0.0           0  triangles, surfaces
showcase_hall                        48 > 48     0.00 > 0.00     10 > 10    1.55      22 > 6.5          34  lamp work
showcase_materials                   43 > 43     0.20 > 0.20       5 > 5    1.72       3 > 2.0           5  fits
showcase_light                         5 > 5     0.01 > 0.01       4 > 4    1.52       6 > 2.8           9  fits
showcase_sky                         16 > 16     0.08 > 0.08       9 > 9    1.13       0 > 0.0           0  fits
showcase_fog                       222 > 222     3.76 > 3.76     46 > 46    1.78       0 > 0.0           0  triangles
showcase_physics                     21 > 21     0.03 > 0.03       5 > 5    1.62       1 > 0.9           2  fits
showcase_people                      26 > 26     0.02 > 0.02     19 > 19    1.52       1 > 0.8           2  fits
showcase_sound                         5 > 5     0.01 > 0.01       4 > 4    1.62       2 > 1.3           3  fits
showcase_film                        15 > 15     0.03 > 0.03     11 > 11    1.53       3 > 2.5           5  fits
showcase_story                       15 > 15     0.01 > 0.01     10 > 10    1.52       1 > 0.8           2  fits
showcase_settings                      7 > 7     0.04 > 0.04       4 > 4    1.51       1 > 1.0           2  fits
17 of 41 scenes over the Phone budget
```

## After the depth pre-pass (2026-11-06, `render.prepass` automatic: on from 6 lamps)

Every pixel is lit once instead of once per surface drawn over it: the bar's lamp work per pixel 213 → 103,
Ardane's market 33 → 21. Both passes land on the same depth (`@invariant`), and the depth pass cuts the same
holes as the lit pass (model pictures with alpha). The bar's picture with it off and on: 0.1% of pixels differ,
only where two surfaces lie at the same depth (which one shows there was arbitrary before too). The software
GPU can't show the gain (it has no early depth test): the phone judges it (`?prepass=0` to compare). 10 of 41
scenes over the Phone budget.

```
scene                           things drawn   triangles (M)  draw calls  shaded   lamps (95%)   lamp work  over the budget on
                                today > seen    today > seen today > seen  /pixel   each > need      /pixel
bar                                443 > 443     0.45 > 0.24   404 > 218    1.00    103 > 51.0         103  draw calls, lamp work
ardane_01_home                       69 > 69     0.06 > 0.06     52 > 52    1.56       3 > 1.1           5  fits
ardane_02_market                   325 > 325     0.50 > 0.45     65 > 51    1.00      21 > 3.2          21  fits
ardane_03_alley                    107 > 107     0.08 > 0.08     52 > 52    1.60       3 > 2.1           5  fits
ardane_04_velasse                  123 > 123     0.33 > 0.22    111 > 80    1.00      10 > 2.3          10  fits
ardane_05_canals                   134 > 134     0.26 > 0.25     89 > 82    1.00       8 > 1.2           8  fits
ardane_06_sluice                   153 > 153     0.13 > 0.13   102 > 102    2.16       4 > 0.0           9  surfaces
ardane_07_qadesh                   133 > 133     0.11 > 0.11     95 > 95    1.70       1 > 0.4           2  fits
ardane_08_palace                   294 > 294     0.42 > 0.33     80 > 63    1.00       6 > 0.9           6  fits
ardane_09_cisterns                 130 > 130     0.25 > 0.12     28 > 15    1.00      11 > 4.0          11  fits
ardane_10_well                       32 > 32     0.01 > 0.01     13 > 13    1.85       2 > 1.0           4  fits
ardane_11_aelor                      98 > 98     1.26 > 1.26     30 > 30    1.99       1 > 0.0           2  triangles
ardane_12_garden                   513 > 513     1.95 > 1.63    111 > 81    1.00     28 > 16.9          28  triangles, lamp work
ardane_13_roots                    499 > 499     0.54 > 0.37   324 > 239    1.00       9 > 4.4           9  fits
ardane_14_nexus                    171 > 171     0.45 > 0.34    139 > 98    1.00      18 > 6.5          18  fits
ardane_15_undercity                145 > 145     0.38 > 0.19     35 > 18    1.00     21 > 11.4          21  fits
ardane_16_core                       94 > 94     0.24 > 0.12     92 > 46    1.00      12 > 2.3          12  fits
ardane_17_return                     84 > 84     0.27 > 0.24     57 > 51    1.00       7 > 6.3           7  fits
ardane_18_hollow                   684 > 684     0.72 > 0.72     90 > 90    1.68       0 > 0.0           0  triangles
ardane_19_scar                       32 > 32     0.02 > 0.02     32 > 32    1.31       2 > 0.3           3  fits
ardane_20_convergence              510 > 510     0.80 > 0.64   204 > 149    1.00      16 > 4.4          16  triangles
ardane_21_battle                   199 > 199     0.41 > 0.36    124 > 97    1.00      14 > 6.4          14  fits
ardane_22_mind                     267 > 267     1.46 > 0.73     86 > 44    1.00      22 > 6.1          22  triangles
ardane_23_after                    219 > 219     0.46 > 0.31   187 > 129    1.00       9 > 0.7           9  fits
ardane_24_epilogue                   92 > 92     0.12 > 0.11     77 > 66    1.00       6 > 0.1           6  fits
courtyard                            42 > 42     0.06 > 0.06     31 > 31    1.54       1 > 0.2           2  fits
cabin                                52 > 52     0.04 > 0.04     34 > 34    1.54       2 > 1.2           3  fits
the_gate                             30 > 30     0.02 > 0.02     22 > 22    1.20       0 > 0.0           0  fits
the_woods                          600 > 600   10.73 > 10.73     34 > 34    2.49       0 > 0.0           0  triangles, surfaces
showcase_glade                   7548 > 7548     5.37 > 5.37   223 > 223    2.64       0 > 0.0           0  triangles, surfaces
showcase_hall                        48 > 48     0.00 > 0.00     20 > 10    1.00      22 > 6.5          22  fits
showcase_materials                   43 > 43     0.20 > 0.20       5 > 5    1.72       3 > 2.0           5  fits
showcase_light                         5 > 5     0.03 > 0.01       8 > 4    1.00       6 > 2.8           6  fits
showcase_sky                         16 > 16     0.08 > 0.08       9 > 9    1.13       0 > 0.0           0  fits
showcase_fog                       222 > 222     3.76 > 3.76     46 > 46    1.78       0 > 0.0           0  triangles
showcase_physics                     21 > 21     0.03 > 0.03       5 > 5    1.62       1 > 0.9           2  fits
showcase_people                      26 > 26     0.02 > 0.02     19 > 19    1.52       1 > 0.8           2  fits
showcase_sound                         5 > 5     0.01 > 0.01       4 > 4    1.62       2 > 1.3           3  fits
showcase_film                        15 > 15     0.03 > 0.03     11 > 11    1.53       3 > 2.5           5  fits
showcase_story                       15 > 15     0.01 > 0.01     10 > 10    1.52       1 > 0.8           2  fits
showcase_settings                      7 > 7     0.04 > 0.04       4 > 4    1.51       1 > 1.0           2  fits
10 of 41 scenes over the Phone budget
```

## After simpler distant trees (2026-11-06, levels of detail)

Each tree has a simpler and a simplest version of itself: the same branches (grown from the same random
numbers, then thinned), fewer and bigger leaf cards so the crown keeps its leaf area, and the farthest leaves
out the twigs' bark. Each copy is drawn as the version its size on screen asks for (`world.lodBias`, `?lod=`),
and the sun's shadow pass draws one version simpler (its 5 cm texels can't show twigs). Full-detail trees are
the trees they were (largest change anywhere: 0.005 mm, rounding). The Woods held at second 10 with near-full
detail and with this: 0.35% of pixels differ. The Woods 10.7 M → 3.7 M triangles a frame (33 M at the start),
the fog room 3.8 M → 1.75 M, Ardane's garden 1.6 M → 1.2 M. 9 of 41 scenes over the Phone budget.

```
scene                           things drawn   triangles (M)  draw calls  shaded   lamps (95%)   lamp work  over the budget on
                                today > seen    today > seen today > seen  /pixel   each > need      /pixel
bar                                443 > 443     0.45 > 0.24   404 > 218    1.00    103 > 51.0         103  draw calls, lamp work
ardane_01_home                       69 > 69     0.06 > 0.06     52 > 52    1.56       3 > 1.1           5  fits
ardane_02_market                   325 > 325     0.50 > 0.45     65 > 51    1.00      21 > 3.2          21  fits
ardane_03_alley                    107 > 107     0.08 > 0.08     52 > 52    1.60       3 > 2.1           5  fits
ardane_04_velasse                  123 > 123     0.33 > 0.22    111 > 80    1.00      10 > 2.3          10  fits
ardane_05_canals                   134 > 134     0.15 > 0.14     89 > 82    1.00       8 > 1.2           8  fits
ardane_06_sluice                   153 > 153     0.13 > 0.13   102 > 102    2.16       4 > 0.0           9  surfaces
ardane_07_qadesh                   133 > 133     0.11 > 0.11     95 > 95    1.70       1 > 0.4           2  fits
ardane_08_palace                   294 > 294     0.42 > 0.33     80 > 63    1.00       6 > 0.9           6  fits
ardane_09_cisterns                 130 > 130     0.25 > 0.12     28 > 15    1.00      11 > 4.0          11  fits
ardane_10_well                       32 > 32     0.01 > 0.01     13 > 13    1.85       2 > 1.0           4  fits
ardane_11_aelor                      98 > 98     0.19 > 0.19     32 > 32    1.99       1 > 0.0           2  fits
ardane_12_garden                   513 > 513     1.16 > 0.97    118 > 86    1.00     28 > 16.9          28  triangles, lamp work
ardane_13_roots                    499 > 499     0.54 > 0.37   324 > 239    1.00       9 > 4.4           9  fits
ardane_14_nexus                    171 > 171     0.45 > 0.34    139 > 98    1.00      18 > 6.5          18  fits
ardane_15_undercity                145 > 145     0.38 > 0.19     35 > 18    1.00     21 > 11.4          21  fits
ardane_16_core                       94 > 94     0.24 > 0.12     92 > 46    1.00      12 > 2.3          12  fits
ardane_17_return                     84 > 84     0.27 > 0.24     57 > 51    1.00       7 > 6.3           7  fits
ardane_18_hollow                   684 > 684     0.72 > 0.72     90 > 90    1.68       0 > 0.0           0  triangles
ardane_19_scar                       32 > 32     0.02 > 0.02     32 > 32    1.31       2 > 0.3           3  fits
ardane_20_convergence              510 > 510     0.64 > 0.48   204 > 149    1.00      16 > 4.4          16  triangles
ardane_21_battle                   199 > 199     0.25 > 0.20    124 > 97    1.00      14 > 6.4          14  fits
ardane_22_mind                     267 > 267     1.46 > 0.73     86 > 44    1.00      22 > 6.1          22  triangles
ardane_23_after                    219 > 219     0.46 > 0.31   187 > 129    1.00       9 > 0.7           9  fits
ardane_24_epilogue                   92 > 92     0.12 > 0.11     77 > 66    1.00       6 > 0.1           6  fits
courtyard                            42 > 42     0.06 > 0.06     31 > 31    1.54       1 > 0.2           2  fits
cabin                                52 > 52     0.04 > 0.04     34 > 34    1.54       2 > 1.2           3  fits
the_gate                             30 > 30     0.02 > 0.02     22 > 22    1.20       0 > 0.0           0  fits
the_woods                          600 > 600     3.67 > 3.67     50 > 50    2.49       0 > 0.0           0  triangles, surfaces
showcase_glade                   7543 > 7543     3.82 > 3.82   238 > 238    2.78       0 > 0.0           0  triangles, surfaces
showcase_hall                        48 > 48     0.00 > 0.00     20 > 10    1.00      22 > 6.5          22  fits
showcase_materials                   43 > 43     0.20 > 0.20       5 > 5    1.72       3 > 2.0           5  fits
showcase_light                         5 > 5     0.03 > 0.01       8 > 4    1.00       6 > 2.8           6  fits
showcase_sky                         16 > 16     0.08 > 0.08       9 > 9    1.13       0 > 0.0           0  fits
showcase_fog                       222 > 222     1.75 > 1.75     53 > 53    1.78       0 > 0.0           0  triangles
showcase_physics                     21 > 21     0.03 > 0.03       5 > 5    1.62       1 > 0.9           2  fits
showcase_people                      26 > 26     0.02 > 0.02     19 > 19    1.52       1 > 0.8           2  fits
showcase_sound                         5 > 5     0.01 > 0.01       4 > 4    1.62       2 > 1.3           3  fits
showcase_film                        15 > 15     0.03 > 0.03     11 > 11    1.53       3 > 2.5           5  fits
showcase_story                       15 > 15     0.01 > 0.01     10 > 10    1.52       1 > 0.8           2  fits
showcase_settings                      7 > 7     0.04 > 0.04       4 > 4    1.51       1 > 1.0           2  fits
9 of 41 scenes over the Phone budget
```

## Counted the cluster way (2026-11-08, `loop budget` follows `render.clusters`)

`loop budget` now counts what the graphics chip draws since rebuild step 4: each copy in view, then each of
its clusters in view at the detail whose error looks under `render.clusterError` pixels (the cull shader's own
rule; the test checks it picks exactly the engine's `selectClusters`), the sun's pass at the detail you see,
one draw a pass, each pixel lit once (the visibility buffer). Glass is still drawn the old way (the bar's 46
draw calls are its bottles and glasses). The bar: 0.59 M → 0.18 M triangles, 568 → 47 draw calls; what is
left over is its lamps (103 a pixel: the picture pipeline's phase 5). The forests are still over on triangles
(the Woods 4.07 M, the Glade 3.85 M): the phone measured the Woods at 48 ms of graphics-chip time on High.
6 of 41 scenes over the Phone budget (the old way, counted today: 12 of 41); on High, 3 of 41.

```
scene                           things drawn   triangles (M)  draw calls  shaded   lamps (95%)   lamp work  over the budget on
                                today > seen    today > seen today > seen  /pixel   each > need      /pixel
bar                                847 > 847     0.18 > 0.18     47 > 47    1.00    103 > 50.5         103  lamp work
ardane_01_home                       77 > 77     0.01 > 0.01       4 > 4    1.00       3 > 1.1           3  fits
ardane_02_market                   503 > 503     0.06 > 0.06       3 > 3    1.00      21 > 1.8          21  fits
ardane_03_alley                    111 > 111     0.01 > 0.01       2 > 2    1.00       3 > 2.1           3  fits
ardane_04_velasse                  130 > 130     0.01 > 0.01       4 > 4    1.00      10 > 0.7          10  fits
ardane_05_canals                   157 > 157     0.07 > 0.07       4 > 4    1.00       8 > 1.8           8  fits
ardane_06_sluice                   157 > 157     0.01 > 0.01       4 > 4    1.00       4 > 0.1           4  fits
ardane_07_qadesh                   157 > 157     0.01 > 0.01       2 > 2    1.00       1 > 0.1           1  fits
ardane_08_palace                   348 > 348     0.05 > 0.05       4 > 4    1.00       6 > 2.1           6  fits
ardane_09_cisterns                 143 > 143     0.01 > 0.01       3 > 3    1.00      11 > 4.0          11  fits
ardane_10_well                       32 > 32     0.00 > 0.00       2 > 2    1.00       2 > 1.0           2  fits
ardane_11_aelor                    171 > 171     0.93 > 0.93       6 > 6    1.00       1 > 0.0           1  triangles
ardane_12_garden                   570 > 570     0.84 > 0.84       4 > 4    1.00     28 > 15.5          28  triangles, lamp work
ardane_13_roots                    542 > 542     0.12 > 0.12       3 > 3    1.00       9 > 5.1           9  fits
ardane_14_nexus                    176 > 176     0.04 > 0.04       3 > 3    1.00      18 > 7.5          18  fits
ardane_15_undercity                244 > 244     0.04 > 0.04       2 > 2    1.00     21 > 11.8          21  fits
ardane_16_core                     125 > 125     0.01 > 0.01       1 > 1    1.00      12 > 2.9          12  fits
ardane_17_return                     92 > 92     0.02 > 0.02       4 > 4    1.00       7 > 6.0           7  fits
ardane_18_hollow                   775 > 775     0.12 > 0.12       2 > 2    1.00       0 > 0.0           0  fits
ardane_19_scar                       34 > 34     0.00 > 0.00       2 > 2    1.00       2 > 0.2           2  fits
ardane_20_convergence              612 > 612     0.33 > 0.33       6 > 6    1.00      16 > 5.9          16  fits
ardane_21_battle                   331 > 331     0.15 > 0.15       6 > 6    1.00      14 > 7.6          14  fits
ardane_22_mind                     321 > 321     0.05 > 0.05       3 > 3    1.00      22 > 5.6          22  fits
ardane_23_after                    241 > 241     0.04 > 0.04       4 > 4    1.00       9 > 1.1           9  fits
ardane_24_epilogue                   96 > 96     0.02 > 0.02       2 > 2    1.00       6 > 0.5           6  fits
courtyard                            44 > 44     0.01 > 0.01       3 > 3    1.00       1 > 0.1           1  fits
cabin                                54 > 54     0.01 > 0.01       2 > 2    1.00       2 > 1.7           2  fits
the_gate                             32 > 32     0.00 > 0.00       2 > 2    1.00       0 > 0.0           0  fits
the_woods                          719 > 719     4.07 > 4.07       2 > 2    1.00       0 > 0.0           0  triangles
showcase_glade                   8450 > 8450     3.85 > 3.85       2 > 2    1.00       0 > 0.0           0  triangles
showcase_hall                        64 > 64     0.00 > 0.00       1 > 1    1.00      22 > 5.4          22  fits
showcase_materials                   87 > 87     0.09 > 0.09       2 > 2    1.00       3 > 2.0           3  fits
showcase_light                       11 > 11     0.00 > 0.00       1 > 1    1.00       6 > 2.7           6  fits
showcase_sky                         18 > 18     0.01 > 0.01       2 > 2    1.00       0 > 0.0           0  fits
showcase_fog                       265 > 265     1.41 > 1.41       2 > 2    1.00       0 > 0.0           0  triangles
showcase_physics                     24 > 24     0.01 > 0.01       2 > 2    1.00       1 > 0.7           1  fits
showcase_people                      40 > 40     0.00 > 0.00       1 > 1    1.00       1 > 0.6           1  fits
showcase_sound                         9 > 9     0.00 > 0.00       2 > 2    1.00       2 > 1.1           2  fits
showcase_film                        15 > 15     0.00 > 0.00       2 > 2    1.00       3 > 2.1           3  fits
showcase_story                       15 > 15     0.00 > 0.00       1 > 1    1.00       1 > 0.7           1  fits
showcase_settings                      9 > 9     0.01 > 0.01       2 > 2    1.00       1 > 0.7           1  fits
6 of 41 scenes over the Phone budget
```


## What a strip costs (N7, 2026-11-11)

`projects/samples/many_strips.loop`: a room 9 m wide, 6 m high and 12 m long, `count` strips of 1 m in rows
under eight levels of shelves, the camera walking down the middle for one second. A 1 m strip is 4 panel lamps
(`piece` 0.25 m). `--set` reaches only engine settings, so `count` is the file's `let count = 40` line; each
row was measured with that line changed (10, 40, 80, 160) and nothing else, by
`./build/loop budget projects/samples/many_strips.loop --quality phone|high`.
"Lamps sent": lamps the scene gives the frame. "Entries a surface": lamps worked out for each surface (the
light grid's cell list). "Lamp work": shaded surfaces x entries, per pixel; the budget is 24 on Phone, 60 on
High. `loop budget` counts work, not time, so it prints no milliseconds; the column is a share of the budget
(lamp work / budget), the only estimate the tool gives. A phone benchmark (`perf/`) is what sets real
milliseconds.

| quality | strips | lamps sent | entries a surface | lamp work /pixel | share of budget | over the budget on |
|---|---|---|---|---|---|---|
| Phone | 10 | 40 | 16.8 | 17 | 0.70 | fits |
| Phone | 40 | 160 | 40.0 | 40 | 1.67 | lamp work |
| Phone | 80 | 320 | 61.1 | 61 | 2.54 | lamp work |
| Phone | 160 | 640 | 61.1 | 61 | 2.54 | lamp work |
| High | 10 | 40 | 16.8 | 17 | 0.28 | fits |
| High | 40 | 160 | 40.0 | 40 | 0.67 | fits |
| High | 80 | 320 | 61.1 | 61 | 1.02 | lamp work |
| High | 160 | 640 | 61.1 | 61 | 1.02 | lamp work |

Phone leaves its frame budget between 10 and 40 strips (17 of 24 at 10 strips, 40 at 40; a straight line
puts 24 near 19). Entries a surface stop growing at 61 from 80 strips on (the light grid's cap), so
160 strips cost no more per pixel than 80 here, but 640 lamps are sent, and a surface only 61 entries long
drops lamps that matter (222 lamps give 95% of the light at 160 strips): the picture gets dimmer, not slower.
Triangles, draw calls and surfaces stay inside the budget at every count; only lamp work goes over.
