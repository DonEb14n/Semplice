# Design tokens (M0) - for owner review

Single source of truth: `src/qml/theme/Theme.qml`. Views may use only these values. Change a number there and the whole app follows.

## Colours (theme "True White", SPEC 8.2)
| Token | Value | Use |
|---|---|---|
| `background` | `#9C9C9C` | window background behind panels |
| `surface` | `#FFFFFF` | panels / blocks |
| `text` | `#1E1E1E` | text, icons |
| `textSecondary` | `#5A5A5A` | secondary text |
| `disabled` | `#B3B3B3` | disabled text/icons |
| `danger` | `#C00F0C` | destructive actions |
| `border` | `#D9D9D9` | thin outlines, progress track *(new in M0, derived)* |
| `coverPlaceholder` | `#3D3D3D` | cover placeholder background *(new, from the mock-ups)* |
| `coverPlaceholderIcon` | `#8A8A8A` | note icon on the placeholder *(new)* |

## Spacing (4/8 px grid)
`space1` 4 - `space2` 8 - `space3` 12 - `space4` 16 - `space5` 24 - `space6` 32

## Radii
`radiusPanel` 8 (Figma; SPEC 8.1 said 12, owner chose 8) - `radiusControl` 8 - `radiusPill` 999

## Type scale (px)
`fontTitle` 20 - `fontHeading` 16 - `fontBody` 13 - `fontCaption` 11. Font: Inter (Segoe UI first for Japanese/Chinese), fallback chain Inter, Segoe UI, Yu Gothic UI, Microsoft YaHei UI, Malgun Gothic.

## Icon sizes
`iconSmall` 16 - `iconMedium` 20 - `iconLarge` 24. Icons are SVG (white in the file, recoloured by the theme).

## Animation
`durationFast` 120 ms - `durationNormal` 200 ms

## Window layout (SPEC 8.1)
| Token | Value |
|---|---|
| default / minimum window | 1280 x 760 / 1000 x 620 |
| `gap` (outer margin and between panels) | 8 |
| `hotbarHeight` | 56 |
| library column | 24 % of width, min 260, max 340 |
| right column | 26 % of width, min 300, max 380 |
| middle min width | 420 (see note) |
| player panel height | 63 % of the body height; side panel gets the rest, min 200 |

**Note:** at the minimum window width 1000 the minimum widths (260 + 300 + 420 + gaps 32) add up to 1012, so the middle area shrinks to about 408 px there. Decision recorded in `DECISIONS.md`; raise the minimum window width to 1012 if you prefer.

## Dark theme (owner's palette, "Need to fix pt4", page 2)

`Theme.dark` switches every colour token below (Settings -> Visual -> Theme: True White / Dark / System; applies at once).

| token | True White | Dark |
|---|---|---|
| `background` (behind the panels) | #9C9C9C | #2C2C2C |
| `surface` (panels, blocks) | #FFFFFF | #1E1E1E |
| `text`, icons | #1E1E1E | #FFFFFF |
| `textSecondary` | #5A5A5A | #B3B3B3 |
| `disabled` | #B3B3B3 | #5A5A5A |
| `danger` | #C00F0C | #C00F0C |
| `border` | #D9D9D9 | #3A3A3A |
| `avatarBackground` (round picture without picture) | #2C2C2C | #FFFFFF |
| `coverBackground` (square cover without picture) | #2C2C2C | #414141 |
| `highlight`, `sliderTrack`, `hairline` | #F5F5F5, #F5F5F5, #B3B3B3 | #2C2C2C, #3A3A3A, #5A5A5A |
| `scrim` (behind dialogs), `dropZone` | #99FFFFFF, #B3FFFFFF | #99000000, #B31E1E1E |

Not taken literally from the owner's sheet: the sheet names #5A5A5A "text secondary" and #B3B3B3 "disabled" also for the dark
theme, which would make disabled text brighter than secondary text; the two are swapped in the dark column. One line in
`Theme.qml` per token if the owner wants it the other way.
