
:point_right: [English](./README.md)

# USB Sound Card with Raspberry Pi Pico

## このプロジェクトについて

![Complete image](image/IMG_1616.jpg)

Raspberry PI PicoでUSBオーディオインタフェースを作成する試みです。  
ハードウェアの回路図とPicoのファームウェアソースコードを含んでいます。  


## 機能

- USB Audio Class 2.0 (USB 2.0 Full speed)
- 2ch Line in
- 2ch Line out
- 2ch PCM SPDIF in 
- 2ch PCM SPDIF out
- 2ch Headphone out
- 最大量子化ビット数 24bit
- 最大サンプリング周波数 96KHz

USB帯域の不足のためUSB24bit/96KHz使用時に入力と出力を同時に利用することはできません。  
ライン入力とSPDIF入力は内部でミックスされて利用されます。これらのボリュームは汎用USBドライバ経由でコントロールできます。（現状はWindowsのWinUSBのみ対応）  

## ハードウェア

回路図の制作にはKiCadを使用しています.  
内容を確認する際は[KiCad6以上](https://www.kicad.org/)をインストールしてください.  

電気回路に詳しくないため設計はほとんどICのリファレンス通りとなっています。  
テストや機能追加をしやすくしたかったため機能ごとにモジュール化するようにしました。  
部品は用意に入手できるものと思います。(基本的には秋月電子通商でそろうはず)  
DACに使用しているPCM5102aはやや高価ですが、PCM510xシリーズであればおそらくそのまま置き換え可能だと思います。  
電源に使用したインダクタは大きいものしか手に入らなかったのですが、許容電流を下げて小型のものに置き換えられると思います。

## ファームウェア 

C/C++で記述しています。  
SPDIFの制御はIC等を使用せずPIOを利用してソフトウェアで行っています。  
前述のとおり、ハードウェアはモジュール化して設計するようにしていますが、ソフトウェアもそれに倣いテストや機能追加が行いやすいよう各機能を切り離せるように意識しています。  

ビルドを行うには、下記サイトを参照してPicoSDKとCMakeを導入してください。
Please refer to follows for details.
- [Raspberry Pi Documentation The C/C++ SDK](https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html)  
- [CMake](https://cmake.org/)

Cmake実行のためフォルダを作成します
> mkdir build  
> cd build  

標準的な構成は
> cmake ..  

トレースログを有効する場合
> cmake .. -DLOG=1   

LOGはTinyUSBのデバッグレベルに渡されます。  
この内容は[こちら](https://docs.tinyusb.org/en/latest/reference/getting_started.html#log)で確認できます。

プロファイルを有効にする場合 
> cmake .. -DPROFILE=1

USB制御にはPicoSDKに含まれるTinyUSBを使用しています。  
rp2040の実装ではエンドポイントのメモリが解放されない問題があるため、事前に領域を確保して解放を回避する[パッチ](patch/tinyusb-rp2040-allow-memory-preallocation.patch)を用意しました。  
TinyUSB内のアロケーションで停止する場合はこのパッチを当てることで解消できるかもしれません。

## デバイスのデバッグ

デバイスは自身のシリアルポートにログを出力しますので、シリアルモニター等を使って見ることができます。
またシリアルモニターで下記コマンドを入力することができます。

> stats_on

統計情報を500msごとに出力を行います(LOG>0であること)
 
> stats_off

統計情報の出力を停止します

> perf

計測したパフォーマンス情報を出力します(PROFILE!=0であること)

## 固有のデバイスリクエスト (Windowsのみ)
ファームにはホストから内部挙動を制御するための固有のリクエストを実装しています。  
Windowsに接続した場合、ファームはMicrosoft OS Descriptor 2.0の定義によりOSにWinUSBドライバーをインストールすることをWindowsに要求し、ユーザーモードでデバイスへのリクエストの送信ができるようにします。  

[control_uwp](control_uwp)はWindowsRuntimeを通じてWinUSB APIを利用し、デバイス内の入力ターミナルのボリュームを制御するソフトウェアです。  
ビルドには[Visual Studio 2022とC#のUWPアプリケーション開発オプション](https://visualstudio.microsoft.com/)をインストールする必要があります。

## 参考資料
- [Raspberry Pi Pico Datasheet](https://datasheets.raspberrypi.com/pico/pico-datasheet.pdf?_gl=1*lbhpfj*_ga*MjA5MDIxNzYzOC4xNjg0MzYzNDM5*_ga_22FD70LWDS*MTY5NjU4ODY5OS4zNS4xLjE2OTY1ODg5NTIuMC4wLjA.)  
- [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf?_gl=1*1rlrh3d*_ga*MjA5MDIxNzYzOC4xNjg0MzYzNDM5*_ga_22FD70LWDS*MTY5NjU4ODY5OS4zNS4xLjE2OTY1ODg4OTYuMC4wLjA.)    
- [SPECIFICATION OF THE DIGITAL AUDIO INTERFACE (The AES/EBU interface)](https://tech.ebu.ch/docs/tech/tech3250.pdf)
- [Texas Instruments PCM510xA 2.1 VRMS, 112/106/100 dB Audio Stereo DAC with PLL and 32-bit, 384 kHz PCM Interface](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf?ts=1696564046679&ref_url=https%253A%252F%252Fwww.ti.com%252Fproduct%252FPCM5102A)
- [Texas Instruments PCM1808 Single-Ended, Analog-Input 24-Bit, 96-kHz Stereo ADC](https://www.ti.com/jp/lit/ds/symlink/pcm1808.pdf?ts=1696503248668&ref_url=https%253A%252F%252Fwww.ti.com%252Fproduct%252Fja-jp%252FPCM1808)

## 免責事項
実用における十分なテストは行っていません。
ある程度の電気への知識とリスクを理解の上ご利用をお願いします。  
このプロジェクトに基づく損害や傷害が生じても当方は責任を負いかねます。

