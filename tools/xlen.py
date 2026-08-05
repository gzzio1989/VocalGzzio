# X(Twitter) の「重み付き文字数」を数える。上限 280。
#   重み1: U+0000-U+10FF, U+2000-U+200D, U+2010-U+201F, U+2032-U+2037
#   それ以外（ひらがな・カタカナ・漢字・、。「」など）は重み2
#   URL は実際の文字数にかかわらず 23 として数える
import re, sys

def weight1(o):
    return (o <= 0x10FF) or (0x2000 <= o <= 0x200D) or (0x2010 <= o <= 0x201F) or (0x2032 <= o <= 0x2037)

def xlen(t):
    urls = re.findall(r'https?://\S+', t)
    t = re.sub(r'https?://\S+', '', t)
    return sum(1 if weight1(ord(c)) else 2 for c in t) + 23 * len(urls)

if __name__ == '__main__':
    d = open(sys.argv[1], encoding='utf-8').read()
    for i, b in enumerate(re.findall(r'```\n(.*?)```', d, re.S), 1):
        t = b.rstrip('\n')
        n = xlen(t)
        print('本文%d: 重み %3d / 280  %s  %s' % (i, n, 'OK ' if n <= 280 else 'NG!', t.split('\n')[0][:24]))
