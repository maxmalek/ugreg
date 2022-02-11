// Little helper for formatting a response that is either a HTML table or JSON

#pragma once

#include <iosfwd>
#include "datatree.h"
#include "json_out.h"

class ResponseFormatter : public DataTree
{
public:
    ResponseFormatter();

    VarRef header();
    VarRef next();

    void emitJSON(BufferedWriteStream& out, bool pretty);
    void emitHTML(std::ostringstream& os);

private:
    Var _hdr;
    size_t _entries;

    void _finalize();
};