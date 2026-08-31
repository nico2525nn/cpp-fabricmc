# Advancements — source note

Vanilla `minecraft:story` advancement JSONs are clean-room authored based on PrismarineJS/minecraft-data 1.21.4 and vanilla `client.jar#data/minecraft/advancement` as reference (Mojang EULA, redistributable data). Files under `story/` mirror vanilla structure: `display`, `parent`, `criteria` (triggers `minecraft:tick`, `minecraft:inventory_changed`, `minecraft:player_killed_entity`), `requirements`. Only ~20 representative entries are included (full vanilla is ~30); missing entries fallback to cppfm: tree. See `plan/plan35.md` §1.
