// Little helper for formatting a response that is either a HTML table or JSON

#pragma once

#include <iosfwd>
#include <vector>
#include "datatree.h"
#include "json_out.h"

class ResponseFormatter : private DataTree
{
public:
    ResponseFormatter();
    ~ResponseFormatter();

    VarRef header();
    VarRef next();

    VarRef array();

    void addHeader(const char *field, const char *text);
    void emitJSON(BufferedWriteStream& out, bool pretty);
    void emitHTML(std::ostringstream& os);

private:
    Var _hdr;
    size_t _entries;
    std::vector<StrRef> _hdrOrder;

    void _finalize();
};