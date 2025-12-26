#include "XmlCls.h"
#include <getopt.h>
#include <iostream>
#include <filesystem>

#ifdef SDL
#include <SDL2/SDL.h>
#endif

int main(int argc, char const *argv[])
{
    XmlDoc dom;
    int opt;
#   ifdef SDL
    g_handle_err_handler = [](const Error* e) {
        if (e && (e->level > NOERR))
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", (e->msg + "\n" + e->data).c_str(), NULL);
    };
#   endif
    
    while((opt = getopt(argc, (char**) argv, "x:")) != -1) 
    { 
        switch(opt) 
        { 
            case 'x': 
                if (!std::filesystem::exists(optarg)) {
                    ErrorPtr err = new Error {lvl::ERR, "File does not exist!", optarg};
                    MSG_ERR(err);
                    return 1;
                }
                dom = XmlDoc(optarg);
                MSG_ERR(dom.err);
                break; 
            case '?': 
                printf("unknown option: %c\n", optopt);
                break; 
        } 
    }

    if (dom.doc) {
        std::string title = dom.XPath<std::string>("string((//EntryType/@name)[1])");
        MSG_ERR(dom.err);
        std::cout << "Project Title: " << title << std::endl;

        std::vector<XmlNode> NL = dom.XPath<std::vector<XmlNode>>("//EntryType/Comment");
        MSG_ERR(dom.err);
        for (auto &node : NL) {
            std::cout << "Comment: " << node.XML() << std::endl;
        }
    }
    return 0;
}
