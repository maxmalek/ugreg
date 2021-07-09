#pragma once

/*

Basically JSON pointers within JSON that describe the resulting json structure

anythign that starts with $ is a variable and will not appear in the output
use variables to query temporary stuff for use elsewhere



Example:

register this as:

/view/person/

and query as:

/view/person/obersteiner

Then * is "obersteiner"

{
    "/ugo/person/<name>": "",
    "/moodle/$course/





*/
