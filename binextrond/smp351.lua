local function PP(s)
    print("### " .. tostring(s) .. " ###")
end

local _htmlenc = { ["<"] = "&lt;", [">"] = "&gt;" }
local function htmlesc(s)
    return s:gsub(".", _htmlenc)
end

local _errors =
{
    E10 = "Unrecognized command",
    E12 = "Invalid port number",
    E13 = "Invalid parameter (number is out of range)",
    E14 = "Not valid for this configuration",
    E17 = "Invalid command for signal type",
    E18 = "System timed out",
    E22 = "Busy",
    E24 = "Privilege violation",
    E25 = "Device is not present",
    E26 = "Maximum connections exceeded",
    E28 = "Bad file name or file not found",
}

-- these fields (lowecased!) are understood by the SMP
-- and will be forwarded in the opencast media package
-- once a recording is complete and sent to ingest
local _metadata =
{
    [0 ] = "Contributor",
    [1 ] = "Coverage",
    [2 ] = "Creator",     -- Vortragender
    [3 ] = "Date",
    [4 ] = "Description",
    [5 ] = "Format",
    [6 ] = "Identifier",
    [7 ] = "Language",
    [8 ] = "Publisher",
    [9 ] = "Relation",    -- Serie
    [10] = "Rights",
    [11] = "Source",
    [12] = "Subject",
    [13] = "Title",       -- Titel
    [14] = "Type",
    [15] = "SystemName",
    [16] = "Course",
}
local _metainv = {}
for k, v in pairs(_metadata) do
    --print(k, v)
    _metainv[v:lower()] = k
end

local function readNonEmptyLine(...)
    local line
    while (not line) or line == "" do
        line = readline(...)
    end
    return line
end

-- return text, contentType, errorCode
-- or in case of errors:
-- return 404 -- no text, just error
-- return "Thingy not found!", 404 -- text + error code
-- return text, errorCode, contentType -- also fine (first number is error, rest is text + contentType)
function hello_text()
    return "Hello world!", "text/plain; charset=utf-8"
end

function hello_json()
    return '{"hello": "Hello world!"}', "application/json; charset=utf-8"
end

function _login()
    timeout "5s"
    local greet = readNonEmptyLine()
    if not greet:match "^%(c%) Copyright [%d%-]+, Extron Electronics, SMP 35[12]" then
        error("Unexpected device identification upon login")
    end
    local date = readNonEmptyLine() -- date line, can skip that
    need(1) -- at least 1 more byte
    local empty = readline(true) -- one empty line
    need(9)
    local pw = readn(9) -- this line is unterminated
    if not pw:match "Password:" then
        error("Expected password prompt, got: " .. line)
    end
    send(CONFIG.password .. "\n")
    need(5)
    local login = readNonEmptyLine()
    if not login:match "Login Administrator" then
        error("Not logged in as admin, got: " .. line)
    end
end

function heartbeat()
    send "Q"
    need(1)
    skipall()
end

function stop()
    skipall()
    send "\x1bY0RCDR\r"
    expect "RcdrY0\r\n"
end

function start()
    skipall()
    send "\x1bY1RCDR\r"
    expect "RcdrY1\r\n"
end

function pause()
    skipall()
    send "\x1bY2RCDR\r"
    expect "RcdrY2\r\n"
end

local _status = { "stopped", "recording", "paused" }
function status()
    timeout "1s"
    skipall()
    send "\x1bYRCDR\r35I"
    local st = readline() -- "<ESC>YRCDR\r"
    local dur = readline() -- "35I"
    st = _status[math.tointeger(st) + 1]
    return st .. " (" .. dur .. ")"
end

function info()
    skipall()
    send "I33I"
    local rec = readline() -- "I"
    local res = readline() -- "33I"
    return res .. "\n" .. rec .. "\n"
end

function detail(params)
    --[[local st = status()
    send "I33I"
    local rec = readline() -- "I"
    local res = readline() -- "33I"
    
    if params and params.json then
        return ('{"status":"%s", "rec":"%s", "res":"%s"}')
                :format(st, rec, res),
                "application/json"
    end]]
    
    local t = {}
    for k, v in pairs(_G) do
        -- global Lua functions are callable endpoints
        if type(k) == "string" and k:sub(1,1) ~= "_" and type(v) == "function" and debug.getinfo(v, "S").what == "Lua" then
            table.insert(t, k)
        end
    end
    --return table.concat(t, "\n"), "text/plain; charset=utf-8"
    
    table.sort(t)
    for i, e in ipairs(t) do
        t[i] = ('<a href="%s">/%s</a><br />'):format(e,e)
    end
    
    local function h1(s) return "<h1>" .. s .. "</h1>" end
    
    return --h1("Status") .. htmlesc(st) .. "<br />" .. htmlesc(rec) .. "<br />" .. res .. "</pre>" ..
           h1("Endpoints") .. table.concat(t, "\n")
    , "text/html"
end

function startform()
    local html = [=[
<html><body>
<form action="startform_go" method="post">
<label for="title">Title:</label>
<input type="text" id="title" name="title"><br />
<label for="title">Creator:</label>
<input type="text" id="creator" name="creator"><br />
<label for="relation">Series UUID:</label>
<input type="text" id="relation" name="relation"><br />
<label for="rights">License:</label>
<input type="text" id="rights" name="rights" value="ALLRIGHTS"><br />
<input type="submit" value="Submit">
</form>
</body></html>
]=]
    return html, "text/html; charset=utf-8"
end

function startform_go(params)
    assert(params, "need parameters")
    local text, content = putmeta(params)
    start()
    return text, content
end

local function setmeta(k, val)
    local id = _metainv[k] -- look up ID given the metadata field name
    if not id then
        return nil, k
    end
    val = tostring(val):match"^%s*(.-)%s*$" -- strip leading/trailing whitespace
    local warn
    if #val > 127 then
        val = val:sub(1, 127)
        warn = "field truncated"
     end
    local s = ("\x1bM%d*%sRCDR\r"):format(id, val)
    local e = ("RcdrM%d*%s\r\n"):format(id, val) -- The manual lies about this, lol
    skipall()
    send(s)
    expect(e)
    return val, warn
end

function echoparams(params)
    local t = {}
    if params then
        local i = 0
        for k, v in pairs(params) do
            i = i + 1
            local extra = ""
            if _metainv[k] then
                extra = (" (metadata field ID %d)"):format(_metainv[k])
            end
            t[i] = tostring(k) .. " = " .. tostring(v) .. extra
        end
    end
    return table.concat(t, "\n"), "text/plain; charset=utf-8"
end

function metadata()
    local t = {}
    local i = 0
    for name, id in pairs(_metainv) do
        i = i + 1
        t[i] = name
    end
    return table.concat(t, "\n"), "text/plain; charset=utf-8"
end

function readmeta()
    local n = 0
    local t, a = {}, {}
    for name, id in pairs(_metainv) do
        n = n + 1
        local s = ("\x1bM%dRCDR\r"):format(id)
        t[n] = s
        a[n] = name
    end
    send(table.concat(t))
    t = {}
    for i = 1, n do
        t[i] = ("%s = %s"):format(a[i], readline())
    end
    table.sort(t)
    
    return table.concat(t, "\n"), "text/plain; charset=utf-8"
end

function putmeta(params)
    assert(params, "need parameters")
    local t = {}
    local i = 0
    for k, v in pairs(params) do
        if v and #v > 0 then -- Don't try to set empty fields; some cause an error when empty
            print("Set meta:", k, "["..v.."]")
            local val, warn = setmeta(k, v)
            if not val then
                warn = "unrecognized metadata field"
            end
            i = i + 1
            t[i] = ("%s = %s%s"):format(k, val, warn and (" [" .. warn .. "]") or "")
        end
    end
    
    return "Assigned metadata:\n" .. table.concat(t, "\n"), "text/plain; charset=utf-8"
end

function reboot(params)
PP(params and params.password)
PP(CONFIG.password)
    if params then
        if params.password == CONFIG.password then
            skipall()
            send "\x1b1BOOT\r"
            expect "Boot1\r\n"
            return "Reboot initiated"
        else
            -- FIXME: want to just not reply to the client to prevent brute-forcing
            return "Wrong password", 401
        end
    end
    return "Pass the device password if you really mean it\n('password' parameter)", 403
end
