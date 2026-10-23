#!/usr/bin/env bash
# Render a small poster (thumbnail) of each guide's posterStep from its live page: ../html/posters/<id>-<slug>.webp
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p ../html/posters
T="$(mktemp -d)"; trap 'rm -r "$T"' EXIT
node -e "import('./src/guides/all.js').then(m=>m.GUIDES.forEach(g=>console.log(g.id+'-'+g.slug, (g.posterStep??g.steps.length-2)+1)))" |
while read -r page step; do
  build/shot.sh "$page.html#step=$step&poster" "$T/$page.png" 1280 720 >/dev/null
  python3 -c "from PIL import Image; Image.open('$T/$page.png').convert('RGB').resize((640,360), Image.LANCZOS).save('../html/posters/$page.webp', quality=78)"
  echo "poster $page (step $step)"
done
