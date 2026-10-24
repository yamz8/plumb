---
name: plumb
description: |
  Read quantity takeoffs made in Plumb from construction drawings (PDF): the
  lengths, areas, and counts measured for each item, sheet by sheet. Use when
  asked how much of something a drawing calls for (linear feet of wall,
  square meters of slab, number of doors), or to export the quantities to CSV.
---

# Plumb

Plumb measures quantities on PDF construction drawings. In the window, the
person sets each sheet's scale, adds items (each a length, an area, or a
count, with a color), and clicks on the plan to measure them. The takeoff is
saved apart from the PDF, keyed by the PDF's contents, and the `plumb`
command reads it. No command needs a display, and none changes the PDF.

## Commands

- `plumb sheets <drawing.pdf> --json` lists each sheet's number, label (A-101,
  when the PDF labels its pages), size in inches, scale (null when none is
  set), and how many measurements it has.
- `plumb quantities <drawing.pdf> --json` reports every item: `name`, `kind`
  (`length`, `area`, or `count`), `unit` (`ft`, `sq ft`, `m`, `m2`, or `ea`),
  total `quantity`, and `sheets`, the quantity on each sheet. A length or
  area on a sheet with no scale has a null quantity and is left out of the
  total; `unscaled` counts those measurements.
- `plumb export <drawing.pdf> -o quantities.csv` writes the CSV the window
  exports: one row per item and sheet, then the item's total. `-o -` prints
  it instead.

## Working with the person

- Quote quantities with their units, and name sheets by label.
- When `unscaled` is above zero, say so: those measurements are missing from
  the totals until the person sets the scale of their sheets (press `S` in
  Plumb).
- `plumb open <drawing.pdf>` opens the window on their desktop. It needs
  their display, so do not wait on it.
- Every command takes `--json`, exits 0 on success and 1 on failure, and
  writes errors to stderr.
