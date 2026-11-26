# Step 00 - Repository skeleton

## What we are solving
Before any code exists we fix where code goes. Every later step adds files to one of a few
folders, so a reader always knows where to look and the Makefile can list sources by folder.

## Why
- Vendored code (`third_party/`) is kept apart from our code so it can be updated in one go
  and so warnings and reviews can treat it differently.
- `bsp/` (this board) is apart from `drivers/` (chips and peripherals) and `app/` (behaviour),
  so the same driver can move to another board by replacing `bsp/` only.
- `middleware/` holds the glue between a third-party stack and our drivers (lwIP port, FatFs port).
- Build output goes to `build/`, which is ignored by git.

## What changed
- `README.md` - project summary, stack, layout, pin map.
- `.gitignore` - build output, host test binaries, editor files.
- `third_party/README.md` - the components we will vendor, with version and licence.
- `app/`, `bsp/`, `drivers/`, `middleware/`, `docs/` - empty folders (kept with `.gitkeep`).

## How to verify
No code yet. Check the tree:

```
git ls-files
```

Expected: the folders listed in `README.md` under "Repository layout" exist.
