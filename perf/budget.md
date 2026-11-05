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
