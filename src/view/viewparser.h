#pragma once

#include <string>
#include "variant.h"

namespace view {

class Executable;

/* Rough DSL grammar sketch (not accounting for whitespace):
 To clarify:
  x...     means at least one x
  x?       means optional x
  x?...    means some optional x
  ()       denotes a group
 

expr         = lookuproot lookupnext?...
lookuproot   = subkey | selector | eval
lookupnext   = subkey | selector
subkey       = '/' [^\[/]+         <--- this one doesn't ignore whitespace!
ident        = [a-zA-Z0-9_]+
idstr        = ident | literal-str
eval         = '$' (ident | ('{' ext-eval '}'))
ext-eval     = expr ident?...
selector     = '[' selection ']'
selection    = keycmp | keysel | '*'
keycmp       = ident cmpOp eval
keysel       = keysel-op | ws | (keysel-list | eval)
keysel-op    = 'keep' | 'drop'
keysel-list  = keysel...
keysel-entry = idstr ('=' idstr)?
literal      = literal-str | literal-int | literal-flt | literal-bool | literal-null
literal-str  = 'as you'd expect, in \'single quotes\' or "double quotes", with escapes'
literal-int  = [0-9]+
literal-flt  = literal-int '.' literal-int?
literal-bool = 'true' | 'false'
literal-null = 'null'
ws           = <1 or more whitespace chars>
*/

// Return entry point (index) when successfuly parsed, 0 on error
size_t parse(Executable& exe, const char *s, std::string& error);

}
