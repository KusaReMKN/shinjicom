# ShinjiConf コマンド例

## これはなに

ShinjiConf を用いて送信できるコマンド例です。

次のように実行してコマンドを送信できます。

```console
$ ./shinjiconf -c /dev/ttyACM1 <./examples/version.bin
to LoRa
0000:	c1 08 01 c8 
0x04 (4)
from LoRa
0000:	05 c1 08 01 20 ed 
0x06 (6)
```

## コマンド例の一覧

### version

バージョン情報を読み出します。

### devaddr

デバイスアドレスを読み出します。

### tc36-1, tc36-2, tc36-3

[TKYTEL COMMENT 36](https://tkytel.github.io/docs/36.txt) の §5 のパラメータを設定します。ただし、Shinjino プログラムとの互換性のため、UART の通信速度を変更しています。tc36-1、tc36-2、及び tc36-3 をこの順に送信します。**tc36-2** によって送信データへのチェックサム付与が有効化されるため、**tc36-3** を送信する際にはオプション **-c** を添える必要があります。

### speed

Air Data Rate を最大にするため、拡散係数を 5 に、帯域幅を 500 kHz に設定します。

### distance

到達距離を最大にするため、拡散係数を 9 に、帯域幅を 125 kHz に設定します。

## 参考資料

- [Technical Datasheet
    920MHz LoRa省電力ワイヤレスモジュール
    E220-900T22S(JP)/E220-900T22L(JP)](https://support.dragon-torch.tech/docs/lora/E220_ver.2.0/)
