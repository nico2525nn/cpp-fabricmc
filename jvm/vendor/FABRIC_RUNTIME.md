# Fabric runtime provisioning contract

対象は Minecraft Fabric 1.21.4 / protocol 769、Fabric Loader 0.16.9 です。
公式 Loader の profile と Maven artifact は `fabric-runtime.lock.json` に固定し、
実行時にはネットワークへ接続しません。

## 手順

ネットワークを使う操作は、作業者が明示的に実行する provision の一回だけです。

```sh
python3 tools/fetch_fabric_runtime.py \
  --provision --cache-dir build/fabric-runtime
python3 tools/verify_fabric_runtime.py \
  --offline --cache-dir build/fabric-runtime
```

`fetch_fabric_runtime.py` は `curl --fail --location` に timeout、HTTPS、公式
host 制限を付け、profile と各 jar を一時ファイルへ取得します。取得後に lock の
サイズと SHA-256 が一致した場合だけ `os.replace` します。不一致は削除して失敗し、
既存の不一致キャッシュを自動修復しません。修復を意図する場合だけ
`--provision --force` を使います。

`--offline`（および mode 無指定）は `curl` を呼ばず、キャッシュに対して次を検証
します。

- loader profile の固定 SHA-256、JSON の profile id と `mainClass`
- lock に列挙された全 jar の固定サイズと SHA-256
- 各 jar の必須 class/service
- loader jar 内の embedded MixinExtras のサイズと SHA-256
- intermediary jar の `mappings/mappings.tiny`

不足時は実行に進まず、必要な provision コマンドを示して終了します。SHA-256、
サイズ、version のいずれかが違う場合も hard error です。

## 起動境界

`verify_fabric_runtime.py --probe` は上記 offline 検証を通過したローカル jar だけで
Knot/GameProvider を起動します。公式の `MinecraftGameProvider` は Mojang の実体
`server.jar` を要求するため、C++ shadow class directory にはそのまま適用できません。
そこで probe では Loader の `GameProvider` service interface に固定した
`CppfmGameProvider` を明示選択し、shadow class directory を class path として公開
します。shadow 側に同名の `net/fabricmc/**` や `org/spongepowered/**` stub があると
公式 jar と Java の型 identity が分裂するため、probe はそれらを除外した一時 staging
path を使い、`net/minecraft/**`、`com/mojang/**`、`cppfm/**` を保持します。成立しなかった場合は、どの stage、command、stdout、stderr、必要 API が
不足したかを `probe-evidence.json` に残します。

これは「公式 Loader/Knot の API 境界を検証した」ことを意味し、任意の Fabric mod、
Mojang server jar、公式 client/GUI、vanilla RNG parity、24 時間運用
を意味しません。runtime の URL は provision/verify ツールの外へ渡しません。
