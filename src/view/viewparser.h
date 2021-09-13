#pragma once

#include <string>
#include "variant.h"

namespace view {

class Executable;

/* Rough DSL grammar sketch (not accounting for whitespace):

expr         = lookup | eval
lookup       = lookuproot lookupnext?...
lookuproot   = subkey | selector | eval
lookupnext   = subkey | selector
subkey       = '/' [^\[/]+         <--- this one doesn't ignore whitespace!
ident        = [a-zA-Z0-9_]+
idsub        = ident | subkey
eval         = '$' (ident | ('{' ext-eval '}')
ext-eval     = idsub ident?...
selector     = '[' selection ']'
selection    = (ident cmpOp eval) | (unOp ident)
literal      = literal-str | literal-int | literal-flt | literal-bool | literal-null
literal-str  = 'as you'd expect, in \'single quotes\' or "double quotes", with escapes'
literal-int  = [0-9]+
literal-flt  = literal-int '.' literal-int?
literal-bool = 'true' | 'false'
literal-null = 'null'
*/

// Return entry point (index) when successfuly parsed, 0 on error
size_t parse(Executable& exe, const char *s, std::string& error);

}
