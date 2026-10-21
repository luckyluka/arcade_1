# Provenance — the schema

Every asset in `assets.json` may carry a `provenance` block: where the art
came from, who made it, and under what terms. The block is **optional**
(an asset without one still compiles) but every asset that ships in this
repository has one — and the owner can require them:

```sh
python3 tools/arc1_assetc.py --assets art/assets.json --out-dir programs \
    --require-provenance
```

which fails with a teaching error naming the assets that have none.

The asset compiler **ignores** the block for its output (the generated
header is byte-identical with or without it) but **validates** its keys:
an unknown key or a wrong type is an error, so a typo cannot hide.

## The schema

```json
{
  "name": "hero",
  "file": "art/hero.png",
  "palette": "hero",
  "provenance": {
    "tool": "procedural",
    "model": "",
    "prompt": "",
    "seed": 20261021,
    "licence": "this repository"
  }
}
```

| key | type | meaning |
|---|---|---|
| `tool` | string, required | how the asset was made: `procedural`, `agent-authored`, `local-ingest` |
| `model` | string | the model, when `tool` is `local-ingest` — else empty |
| `prompt` | string | the prompt, when there was one — else empty |
| `seed` | integer | the seed, when the asset is procedural or ingested — else 0 |
| `licence` | string, required | the terms the asset ships under |

All five keys are always present (an empty string where a key does not
apply). That keeps the generated `art/ART_LICENSES.md` rectangular, and
it makes "the model was not used" a fact on the record rather than an
absence.

## The rules

1. **`tool` is one of** `procedural`, `agent-authored`, `local-ingest`.
   Anything else is an error: the point of the field is that it can be
   counted.
2. **Image-model output is never committed** (the owner's decision,
   2026-10-21). An asset with `tool: "local-ingest"` describes something
   that was produced locally and *not* shipped; the shipped equivalent
   must be code-generated. `art/ART_LICENSES.md` makes that visible.
   The tools are **stdlib only** (no Pillow): the local ingest tool
   reads PNG, which zlib covers, and nothing else.
3. **`licence` is not empty.** A blank licence is an error — an asset
   with no terms is an asset nobody may use.
4. **`seed` is an integer** (it may be 0 = "not seeded"). Generators must
   be deterministic: the same seed must always paint the same pixels.
5. **A generated header never carries the block.** The header is data;
   the provenance lives in the manifest and in `art/ART_LICENSES.md`.

## The generated summary

```sh
python3 tools/arc1_assetc.py --assets programs/petalblade_art/petalblade_assets.json \
    --out-dir programs --licenses art/ART_LICENSES.md
```

writes `art/ART_LICENSES.md`: one row per asset — its name, its tool, its
model/prompt/seed when it has them, and its licence — plus a summary of
how many assets came from each tool. It is regenerated, never hand-edited.
