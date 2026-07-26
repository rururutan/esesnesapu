# 似非SENSAPU(Ese-snesapu)

## 説明

`似非SENSAPU`はx64用に作成した[SNESAPU](https://dgrfactory.jp/spcplay/index.html)の振りをするDLLです。

[Game Music Emu](http://slack.net/~ant/libs/audio.html) + SNESAPUのIFグルー層で出来てます。
SNESAPUがx86以外に対応するまでの繋ぎです。

## 利点

* x86用SNESAPUプレイヤーをx64ビルドしてDLL差し替えればそのま使えます
* SNESAPUとGMEの比較に使えるかもしれない

## 制限

* 再現性はSNESAPU比で劣ります
* 大昔に作ったソフトなのでノーサポート

## Configure & Build

```bash
cmake -S src -B build -A x64
cmake --build build --config Release
```

