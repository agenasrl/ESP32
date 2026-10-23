---
name: cpl-import
description: Convert an attached pick-and-place / placement file (.txt, .csv, .xlsx or .xls — Altium "Pick Place", Cadence/Allegro place_components, or any table with designator and X/Y columns) into a new CPL_mm.xlsx laid out like the BV2301A0_CPL_mm template (Designator, Mid X, Mid Y, Layer, Rotation; mm; TOP/BOT). Use when the user attaches such a file and asks for a CPL, a CPL_mm.xlsx, or "the usual CPL sheet".
---

# CPL import

Turns a placement export into `CPL_mm.xlsx` with the same layout as the
BV2301A0_CPL_mm template (yellow bold Arial header, same column widths):

| Column     | Filled from                                                        |
|------------|--------------------------------------------------------------------|
| Designator | Designator / refdes / Reference                                    |
| Mid X      | Mid X / Center-X / symbol_x (converted to mm, 4 decimals)          |
| Mid Y      | Mid Y / Center-Y / symbol_y (converted to mm, 4 decimals)          |
| Layer      | `TOP` / `BOT` — from a Layer/Side column (TopLayer, Bottom, T/B…), or from a `mirror` column (`m` = BOT, blank = TOP) |
| Rotation   | Rotation / rot / angle, copied as is                               |

Everything else (description, comment, footprint, symbol_name, quantity…) is ignored.

## Steps

1. Run the converter on the attached file. Write the output in the primary
   working directory unless the user names another place:

   ```bash
   python3 -I .claude/skills/cpl-import/scripts/cpl_import.py "<attached file>" -o CPL_mm.xlsx
   ```

   If `CPL_mm.xlsx` already exists, ask the user before re-running with
   `--force` (or offer a different name such as `<board>_CPL_mm.xlsx`).
   If the user gives column mappings that differ from the defaults above,
   follow theirs (edit the rows in the output with openpyxl, or adapt the
   `FIELDS` table in the script for that run).

2. Read the script's report and check it:
   - part count matches the number of component lines in the source
     (multi-line quoted descriptions in Altium files are not extra parts);
   - "columns used" picked the centre columns (not Pad-X / Ref-X) — if not,
     tell the user and fix it;
   - any WARNING lines (no layer column, no rotation, duplicate designators,
     skipped rows) go into the reply.

3. Send the file to the user and summarise in a few lines: part count,
   TOP/BOT split, which source columns fed each output column, and the
   warnings. Mention that bottom-side rotations are copied unchanged, so they
   should check them in the assembler's preview.

4. If the session works in a git repo with a designated branch, commit and
   push the new file there.
