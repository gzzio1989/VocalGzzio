#pragma once
// =============================================================================
//  VocalGzzio v2.10.0 —「点検」まわりの土台
//
//  ここには「音を作らない、けれど製品として要る」3つを置きます。
//
//   ● 使われ方カウンタ (#75)
//       どの機能を何秒使ったかを手元に貯めるだけ。**ネットワークは一切使いません。**
//       送信もしません。あなたが報告セットを送ってくれたときだけ、作者に届きます。
//       数え方は「音声スレッドで触らない」= 画面側の 0.5 秒タイマーから
//       「いま効いているか」を見て足すだけ。音の処理は 1 サンプルも変わりません。
//
//   ● 名前ごとの設定 (#76)
//       ホストが教えてくれるトラック名（VST3 の ChannelContext）をキーにして、
//       設定を覚えておく箱。トラック名が無いホスト・単体起動版では、
//       ユーザーが打った名前を使います。だから正確には「曲ごと」ではなく
//       「**名前ごと**」です。画面の文言もそう書きます。
//
//   ● 報告セット (#74)
//       版・環境・いまの設定・使われ方を 1 ファイルにまとめる。
//       「再現しません」の往復を減らすための道具です。
//
//  どれも音声スレッドから呼びません。ファイル入出力は必ず画面側から。
// =============================================================================

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "TestPaths.h"

namespace gz::checkup
{

// ---- 使われ方カウンタ ------------------------------------------------------
// 「効いている機能」を 0.5 秒ごとに数えて、秒として貯める。
// 貯める先は 1 ファイル。中身は人が読める XML なので、送る前に自分で中身を確認できる。
class Usage
{
public:
    // 数える対象。amtId が 0 でなければ「効いている」とみなす。
    // onId があるものは、スイッチが入っていることも条件にする。
    struct Item { const char* key; const char* onId; const char* amtId; };

    static juce::File file()
    {
        return gz::dataDirectory().getChildFile ("usage.xml");
    }

    void load()
    {
        seconds.clear();
        if (auto xml = juce::XmlDocument::parse (file()))
        {
            firstSeen = xml->getStringAttribute ("first", firstSeen);
            launches  = xml->getIntAttribute ("launches", 0);
            for (auto* e : xml->getChildWithTagNameIterator ("U"))
                seconds.set (e->getStringAttribute ("k"),
                             juce::String (e->getDoubleAttribute ("s")));
        }
        ++launches;
        if (firstSeen.isEmpty())
            firstSeen = juce::Time::getCurrentTime().toISO8601 (true);
    }

    void save() const
    {
        juce::XmlElement xml ("VOCALGZZIO_USAGE");
        xml.setAttribute ("first", firstSeen);
        xml.setAttribute ("launches", launches);
        for (const auto& k : seconds.getAllKeys())
        {
            auto* e = xml.createNewChildElement ("U");
            e->setAttribute ("k", k);
            e->setAttribute ("s", seconds[k].getDoubleValue());
        }
        auto f = file();
        f.getParentDirectory().createDirectory();
        xml.writeTo (f, {});
    }

    void add (const juce::String& key, double sec)
    {
        seconds.set (key, juce::String (seconds[key].getDoubleValue() + sec));
    }

    double get (const juce::String& key) const { return seconds[key].getDoubleValue(); }
    const juce::StringPairArray& all() const { return seconds; }
    int  getLaunches() const { return launches; }
    juce::String getFirstSeen() const { return firstSeen; }

    void clear() { seconds.clear(); }

private:
    juce::StringPairArray seconds;
    juce::String firstSeen;
    int launches = 0;
};

// ---- 名前ごとの設定 --------------------------------------------------------
// 「その名前で覚える／その名前を呼び出す」だけの、とても素朴な箱。
// 中身は設定 XML をそのまま入れ子にして持つ。
class Recall
{
public:
    static juce::File file()
    {
        return gz::dataDirectory().getChildFile ("recall.xml");
    }

    // name の設定を覚える（同じ名前があれば上書き）
    static bool store (const juce::String& name, const juce::XmlElement& state)
    {
        if (name.isEmpty()) return false;
        auto root = juce::XmlDocument::parse (file());
        if (root == nullptr) root = std::make_unique<juce::XmlElement> ("VOCALGZZIO_RECALL");

        // 既存の同名を消してから足す（重複させない）
        for (int i = root->getNumChildElements(); --i >= 0;)
            if (auto* c = root->getChildElement (i))
                if (c->getStringAttribute ("name") == name)
                    root->removeChildElement (c, true);

        auto* e = root->createNewChildElement ("ENTRY");
        e->setAttribute ("name", name);
        e->setAttribute ("saved", juce::Time::getCurrentTime().toISO8601 (true));
        e->addChildElement (new juce::XmlElement (state));

        auto f = file();
        f.getParentDirectory().createDirectory();
        return root->writeTo (f, {});
    }

    // name の設定を取り出す（無ければ nullptr）
    static std::unique_ptr<juce::XmlElement> fetch (const juce::String& name)
    {
        if (name.isEmpty()) return nullptr;
        auto root = juce::XmlDocument::parse (file());
        if (root == nullptr) return nullptr;
        for (auto* c : root->getChildWithTagNameIterator ("ENTRY"))
            if (c->getStringAttribute ("name") == name)
                if (auto* inner = c->getFirstChildElement())
                    return std::make_unique<juce::XmlElement> (*inner);
        return nullptr;
    }

    static juce::StringArray names()
    {
        juce::StringArray out;
        if (auto root = juce::XmlDocument::parse (file()))
            for (auto* c : root->getChildWithTagNameIterator ("ENTRY"))
                out.add (c->getStringAttribute ("name"));
        return out;
    }
};

} // namespace gz::checkup
