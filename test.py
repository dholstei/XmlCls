
from XmlCls import XmlCls, lvl


def print_error(error):
    print(f"level: {error.level.name}")
    print(f"msg:   {error.msg}")
    print(f"data:  {error.data}")

if __name__ == "__main__":
    dom = XmlCls("<root><child value='3.14'>text</child></root>")

    pi = dom.XPath("//child/@value", float)
    print(pi)
    txt = dom.XPath("//child", str)
    print(txt)
    chile = dom.XPath("//child")
    val_str = chile.XPath("@value", str)
    print(val_str)
    print(chile.XML())


    bad = XmlCls("<PonziCoin>")
    assert bad.err is not None
    assert bad.err.level == lvl.ERR
    print_error(bad.err)

    good = XmlCls("<PonziCoin/>")
    assert good.err is None