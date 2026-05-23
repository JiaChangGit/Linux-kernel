# FTL Notes

The FTL uses page-level mapping and out-of-place updates.

Write flow:
1. Lookup old PPA.
2. Allocate new NAND page.
3. Program new page.
4. Invalidate old page.
5. Update mapping table.

This ordering is intentional to preserve metadata consistency.
