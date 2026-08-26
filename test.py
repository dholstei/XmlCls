
from XmlCls import XmlCls

if __name__ == "__main__":
    dom = XmlCls("<root><child value='3.14'>text</child></root>")

    pi = dom.XPath("//child/@value", float)
    print(pi)
    txt = dom.XPath("//child", str)
    print(txt)
