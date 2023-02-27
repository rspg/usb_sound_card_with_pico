
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

USB制御にはPicoSDKに含まれるTinyUSBを使用しています。  
rp2040の実装ではメモリが解放されない問題があるため、事前に領域を確保して解放を回避する[パッチ](patch/tinyusb-rp2040-allow-memory-preallocation.patch)を用意しています。  
ビルド前にTinyUSBにこのパッチをあてておく必要があります。  

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

## 免責事項
実用における十分なテストは行っていません。
ある程度の電気への知識とリスクを理解の上ご利用をお願いします。  
このプロジェクトに基づく損害や傷害が生じても当方は責任を負いかねます。