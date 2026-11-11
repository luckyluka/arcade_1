PY ?= python3
TOOL = $(PY) tools/arcforge_ref.py
EX = examples/hero.iron_knight.json

.PHONY: check lint test golden render list
check:
	$(PY) tests/run_tests.py
lint:
	$(TOOL) lint
test:
	$(TOOL) test $(EX)
golden:
	$(TOOL) golden $(EX) --update
render:
	$(TOOL) render $(EX) --out out --scale 10
list:
	$(TOOL) list
