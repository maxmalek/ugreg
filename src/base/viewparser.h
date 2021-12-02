#pragma once

#include <string>
#include "variant.h"
#include "viewexec.h"

namespace view {

class Executable;

/* Rough DSL grammar sketch (not accounting for whitespace):
 To clarify:
  x...     means at least one x
  x?       means optional x
  x?...    means some optional x
  ()       denotes a group

--- parser starts with unquoted-text ---

unquoted-text = (literal-text? evalroot)... literal-text?
evalroot      = varref | ('$' fncall) | ('${' expr '}')      <-- all start with $
--- main language ---
expr          = eval modlist
eval          = dot | literal | varref | fncall | query      <--- anything that yields one or more values
dot           = '.'
--- variables and identifiers ---
varref        = '$' (idstr | '*')
idstr         = ident | literal-str
ident         = [a-zA-Z0-9_]+
--- function call ---
fncall        = ident '(' exprlist ')'
exprlist      = expr (',' expr)?...
--- modifiers ---
modlist       = mod?...
mod           = selector | query | transform | simplekey
transform     = '|' (idstr | fncall)
lookup        = '/' idstr     <--- works outside of query
--- query / tree lookup --- // FIXME: re-add those once the rest is stabilized
#query         = '{' querybody '}'
#querybody     = lookuproot lookupnext?...
#lookuproot    = subkey | selector | expr
#lookupnext    = subkey | selector
#subkey        = '/' [^\[/}]+         <--- this one doesn't ignore whitespace!
--- selection and filtering ---
selector      = '[' selection ']'
selection     = keycmp | keysel | '*'
keycmp        = ident cmpOp expr
keysel        = keysel-op | ws | (keysel-list | expr)
keysel-op     = 'keep' | 'drop' | 'key'
keysel-list   = keysel...
keysel-entry  = idstr ('=' idstr)?
--- terminals (values, literals, etc) ---
range         = range-entry (',' range-entry)...
range-entry   = (literal-int ':' literal-int) | literal-int
literal       = literal-str | literal-int | literal-flt | literal-bool | literal-null
literal-text  = anything until a closing quote or '{'
literal-str   = ('"' literal-text '"') | (''' literal-text ''')
literal-int   = [0-9]+
literal-flt   = literal-int '.' literal-int?
literal-bool  = 'true' | 'false'
literal-null  = 'null'
ws            = <1 or more whitespace chars>
*/

// Return entry point (index) when successfuly parsed, 0 on error
size_t parse(Executable& exe, const char *s, std::string& error);

const char *getKeySelOpName(KeySelOp op);

}
