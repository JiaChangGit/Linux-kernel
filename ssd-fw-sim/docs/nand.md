# NAND Notes

Each block contains multiple pages.
Pages may only transition:
- FREE -> VALID
- VALID -> INVALID
- INVALID -> FREE (after block erase)

In-place overwrite is forbidden.
