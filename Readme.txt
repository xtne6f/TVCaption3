TVTest TVCaption3 Plugin (暫定)

■概要
TVCaptionMod2の字幕のデコードやレンダリング周りをlibaribcaption(
https://github.com/xqq/libaribcaption )に置き換えてみた字幕プラグインです。いま
のところ実験的な状態なので常用はお勧めしません(例外で落ちても知りません)。
Visual Studio 2017以降ならたぶんビルドできます。
設定は"TVCaption3.ini"をTVCaption3.tvtpと同じ場所に置いて直接編集してください。
プラグインの無効/有効でリロードできます。

EnOsdCompositor
    映像への字幕合成機能を有効にする[=1]かどうか
    # レンダラはVMR9かEVRを利用し、さらに設定キーMethod[=3]としてください。
    # TVTest設定→OSD表示→「映像と合成する」とおなじ方法で字幕を表示できるよう
    # にします。たぶんつぎの場合に効果的です:
    # ・TVTest本体のキャプチャ機能でキャプをとりたい
    # ・XPや非Aero環境での性能向上
    # TVTest ver.0.9.0未満ではAPIフックを利用する比較的リスキーな機能です。Aero
    # な環境では従来の設定キーMethod[=2]を利用することをお勧めします。
FaceName
    使用するフォント名を指定
    # 何も指定しないと適当なフォントが使われます。
Method
    字幕の表示方法
    # レイヤードウィンドウ[=2]、もしくは映像と合成[=3](要EnOsdCompositor[=1])の
    # いずれかを指定してください。
DelayTime / DelayTimeSuper*
    字幕/文字スーパーを受け取ってから表示するまでの遅延時間をミリ秒で指定
    # [=-5000]から[=5000]まで。
StrokeWidth
    字幕文の縁取りの幅の10倍
    # [=0]より大きいとき常に縁取ります。
OrnStrokeWidth
    ORN縁取り指定された字幕文の縁取りの幅の10倍
    # [=0]以上で指定します。
    # 字幕データ内で明示的に縁取りを指示されているときの幅です。
    # StrokeWidthが0より大きいときは無視されます。
IgnoreSmall
    振り仮名らしきものを除外する[=1]かどうか

■ライセンス
MITとします。

■ソース
https://github.com/xtne6f/TVCaption3
