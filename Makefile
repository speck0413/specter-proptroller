# Spectre Proptroller: board build helpers (see README.md)
.PHONY: bootstrap typecheck place build fab order clean
bootstrap: ; tools/bootstrap.sh
typecheck: ; aieda/node_modules/.bin/tsc -p tsconfig.json
place: ; bin/aieda build hardware/board.ts --no-route
build: ; JAVA_TOOL_OPTIONS=$${JAVA_TOOL_OPTIONS:--Xmx6g} bin/aieda build hardware/board.ts && python3 tools/silk_refs.py && python3 tools/portable_paths.py
# fab: aieda fab writes the gerbers / BOM / CPL even when its release gate blocks (exit 1, not orderable). Keep the files and the path
# clean-up, then say plainly which it was; exits 0 either way so `make fab order` still packages the files for a JLC preview.
fab: ; bin/aieda fab hardware/board.ts; s=$$?; python3 tools/portable_paths.py; \
	if [ $$s -eq 0 ]; then echo "== fab: release gate PASSED (aieda fab exit 0): orderable"; \
	else echo "== fab: files written, but the release gate is NOT PASSED (aieda fab exit $$s): NOT ORDERABLE. See the gate lines above and bin/aieda verify hardware/board.ts"; fi
order: ; python3 tools/jlc_packages.py hardware hardware/fab/order
clean: ; rm -rf hardware/kicad hardware/fab   # note: kicad/ and fab/ are tracked; git restores them
