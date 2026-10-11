Minecraft 1.21.4 + Fabric Reference Workspace

目的

Minecraft Java Edition 1.21.4 のサーバー実装を別言語へ移植するときに、

- Vanilla Minecraft の実装
- server.jar 固有の内容
- Fabric Loader
- Fabric API
- Yarn / Intermediary mappings
- Mixin の仕組み

をまとめて検索・参照できるローカル環境を作る。

最終的にはこんな構成を目指す。

mc-reference/
├── minecraft/       # Vanilla 1.21.4 Decompiled Source
├── server/          # server.jar / server-only資料
├── fabric-loader/
├── fabric-api/
├── yarn/
├── intermediary/
└── notes/

---

1. Minecraft 1.21.4本体を取得

MinecraftSourcesGenerator（MSG）を使う。

git clone https://github.com/Bram1903/MinecraftSourcesGenerator.git
cd MinecraftSourcesGenerator

./gradlew generateVersion -Pmc=1.21.4 -Peula=true

生成先：

MinecraftSourcesGenerator/
└── sources/
    └── 1.21.4/
        ├── net/
        │   └── minecraft/
        └── com/
            └── mojang/

MSGは、

official client.jar
        +
official server.jar
        ↓
Mojmapで名前復元
        ↓
client/server merge
        ↓
Parchment情報追加（利用可能な部分）
        ↓
Vineflower
        ↓
Java source

という処理を行う。1.21.4はMojang公式mapping対象で、Parchmentが存在する場合はparameter nameやJavadocも追加される。

IDEで読みたい場合：

./gradlew ide -Pmc=1.21.4 -Peula=true

"workspace/" をIDEで開く。

※ MSG自体の現在のGradle toolchainはJava 25を要求する。生成された1.21.4 MinecraftそのもののJava要件とは別の話。

---

2. server.jarも別に保存する

MSGのmerged sourceだけに依存しない。

MSGはclientとserverの両方をまとめてMinecraft全体を読みやすくするためのものなので、

minecraft/
    → 全体を理解・検索する資料

server/
    → 実際のDedicated Serverの確認用

という扱いにする。

server.jarにはJavaコード以外にも重要なデータがあるので、元のjar自体も保存する。

例：

server/
├── server-1.21.4.jar
├── extracted/
└── decompiled/      # 必要ならserver-onlyで別途decompile

別言語へ移植するときにMSGとserver-onlyの結果が食い違ったら、server.jar側を優先して確認する。

---

3. Fabric側を取得

FabricはMinecraftそのものではないので、デコンパイルする必要はない。

OSSのソースをそのまま置く。

cd mc-reference

git clone https://github.com/FabricMC/fabric-loader.git
git clone https://github.com/FabricMC/fabric.git fabric-api
git clone https://github.com/FabricMC/yarn.git
git clone https://github.com/FabricMC/intermediary.git

それぞれ1.21.4時代のtag/commitへcheckoutする。

役割は、

fabric-loader/
    Fabricの起動・class loading・mod loading
    Knot
    MappingResolver
    Mixin bootstrap

fabric-api/
    Fabric API自体
    Events
    networking
    lifecycle hooks
    registry extensions等

yarn/
    Minecraftの読みやすい名前・mapping情報

intermediary/
    Fabricがバージョン間で使う安定した名前空間

Fabric LoomもMinecraftのclient/server jarを取得して開発環境を構築し、Fabric Loader・Mixin・mappingを扱う仕組みなので、Fabric側の挙動確認用として別途Loomプロジェクトを1つ持っていてもよい。

---

4. 最終的なディレクトリ

おすすめ：

mc-reference/
│
├── minecraft/
│   └── 1.21.4/
│       └── net/minecraft/...
│
├── server/
│   ├── server-1.21.4.jar
│   ├── extracted/
│   └── decompiled/
│
├── fabric/
│   ├── loader/
│   ├── api/
│   ├── yarn/
│   └── intermediary/
│
├── external/
│   ├── mixin/
│   └── minecraft-data/     # 必要ならPrismarine等
│
└── notes/
    ├── architecture.md
    ├── mappings.md
    └── port-status.md

このディレクトリ自体をAIエージェントから検索可能にする。

Minecraftのdecompiled sourceは再配布用repoへコミットせず、ローカル参照資料として扱う。MSG自身も生成物をgit-ignoreし、ローカルでの研究・開発用途として扱う設計になっている。

---

5. 別言語への移植ではこう使う

基本ルール：

① MSGで対象クラスを探す
        ↓
② 関連クラス・interfaceを追う
        ↓
③ server-only実装も確認
        ↓
④ Fabric Loader/APIがその部分へ何をしているか検索
        ↓
⑤ 別言語側へ実装
        ↓
⑥ 実際の1.21.4 clientで動作確認

例：

ServerPlayer
ServerLevel
MinecraftServer
ServerGamePacketListenerImpl
ServerChunkCache
ChunkMap
Entity
Level

を移植するとする。

AIには、

Minecraft 1.21.4 Mojmap sourceを一次資料にする。

Fabricや別実装をMinecraft本体の正しい挙動だと仮定しない。

実装する機能について：
1. minecraft/ からVanilla実装を調査
2. server/ と比較
3. fabric/ から関連hook/mixinを調査
4. 依存関係を整理
5. 別言語側へ実装
6. 不明な部分は推測せずTODOとして残す

という方針を与える。

---

情報源の優先順位

最優先
│
├─ Official server.jar
├─ MSG Mojmap Minecraft source
│
├─ Fabric Loader source
├─ Fabric API source
│
├─ Yarn / Intermediary
│
├─ Paper等
├─ Pumpkin / Basalt / 他の再実装
│
└─ Wiki等
   参考程度

Paperや他言語Minecraft serverは非常に役立つが、Vanillaと異なる設計や簡略化が入る可能性がある。

Minecraftの挙動を決める一次資料にはしない。

---

方針を一言で言うと

«Minecraft 1.21.4をMSGで丸ごと読める状態にし、server.jarを答え合わせ用として残し、Fabric Loader/API/mappingsを横に置いて、全部をAIから横断検索できる巨大なreference workspaceにする。»

Fabricを無理やり「デコンパイル済みMinecraft」に合体させる必要はない。

Vanilla・Fabric・Mappingsを分離したまま全部見える状態にする方が、移植にもデバッグにも使いやすい。
