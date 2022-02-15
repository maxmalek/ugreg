#include "responseformat.h"
#include <string.h>
#include <sstream>
#include "debugfunc.h"

ResponseFormatter::ResponseFormatter()
    : _entries(0)
{
    this->_root.makeArray(*this, 16);
    _hdr.makeMap(*this);
}

ResponseFormatter::~ResponseFormatter()
{
    _hdr.clear(*this);
}

VarRef ResponseFormatter::header()
{
    return VarRef(this, &_hdr);
}

VarRef ResponseFormatter::next()
{
    const size_t need = _entries + 1;
    const size_t have = this->_root.size();
    Var *a = this->_root.array();
    if(have < need)
        a = this->_root.makeArray(*this, have*2);
    _entries = need;
    return VarRef(this, a + need - 1);
}

void ResponseFormatter::addHeader(const char* field, const char* text)
{
    StrRef f = this->putNoRefcount(field, strlen(field));
    Var::Map *m = _hdr.map();
    Var t;
    t.setStr(*this, text);
    m->put(*this, f, std::move(t));
    _hdrOrder.push_back(f);
}

void ResponseFormatter::emitJSON(BufferedWriteStream& out, bool pretty)
{
    _finalize();
    writeJson(out, VarCRef(this, &this->_root), pretty);
}

void ResponseFormatter::emitHTML(std::ostringstream& os)
{
    _finalize();
    os << "<table border=\"1\"><tr>";
    const Var::Map *hm = _hdr.map();
    for(size_t i = 0; i < _hdrOrder.size(); ++i)
        os << "<th>" << hm->get(_hdrOrder[i])->asCString(*this) << "</th>\n";
    os << "</tr>";
    const Var *a = _root.array();
    const size_t N = _root.size();
    for(size_t i = 0; i < N; ++i)
    {
        os << "<tr>";
        const Var::Map *m = _root.at(i)->map();
        for (size_t i = 0; i < _hdrOrder.size(); ++i)
        {
            const StrRef k = _hdrOrder[i];
            const Var *v = m->get(k);
            os << "<td>";
            varToString(os, VarCRef(*this, v));
            os << "</td>\n";
        }
        os << "</tr>";
    }
    os << "</table>\n";
}

void ResponseFormatter::_finalize()
{
    this->_root.makeArray(*this, _entries); // truncate to actual size
}

