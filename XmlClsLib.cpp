#include "XmlCls.h"

#include <cstring>

extern "C" {

size_t XmlNode_XML(xmlNodePtr node, char* buffer, size_t size)
{
    if (!node) return 0;

    XmlNode xn(node);
    std::string xml = xn.XML();

    // Required size, including terminating NUL.
    size_t required = xml.size() + 1;

    if (!buffer || size == 0)
        return required;

    if (size < required)
        return required;

    std::memcpy(buffer, xml.c_str(), required);
    return required;
}

}