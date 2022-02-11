#include "responseformat.h"

ResponseFormatter::ResponseFormatter()
    : _entries(0)
{
    this->_root.makeArray(*this, 16);
    _hdr.makeMap(*this);
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

void ResponseFormatter::emitJSON(BufferedWriteStream& out, bool pretty)
{
    _finalize();
    writeJson(out, VarCRef(this, &this->_root), pretty);
}

void ResponseFormatter::emitHTML(std::ostringstream& os)
{
    _finalize();

}

void ResponseFormatter::_finalize()
{
    this->_root.makeArray(*this, _entries);
}

