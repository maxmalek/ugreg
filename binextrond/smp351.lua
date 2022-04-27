-- The device configuration is accessible in the global variable CONFIG
-- (It's not used by C++ code)
--[[
print("____ CONFIG ____")
for k, v in pairs(CONFIG) do
    print(("%s = %s"):format(tostring(k), tostring(v)))
end
print("----------------")
]]

local extrond = require "extrond"

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
    return json{hello = "Hello world!"}, "application/json; charset=utf-8"
end

function _login()
    timeout "15s"
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

-- check that a parameter isn't malicious
-- don't want anyone to inject a command by terminating another command early and beginning a new sequence
local function checkparam(x)
    if type(x) == "string" and x:match("[%\r%\n%\x1b%\x00]") then
        error("Parameter contains invalid sequence")
    end
    return x
end

function heartbeat()
    send "Q"
    need(1)
    skipall()
    return "(This is done automatically in background, there is no need to call this manually)"
end

function stop()
    skipall()
    send "\x1bY0RCDR\r"
    expect "RcdrY0\r\n"
    return "Stopped."
end

function start(params)
    -- set params first, THEN start the recording (must be done in this order)
    -- this is horrible actually
    -- because if starting the recoding fails then we've set metadata that will stick around
    -- and possibly affect the next successful start since there's no way to clear them
    -- -> SMP doesn't like empty string as metadata and just returns error, dumb piece of garbage
    local extra = ""
    if params then
        extra = "\n" .. putmeta(params)
    end

    skipall()
    send "\x1bY1RCDR\r"
    expect "RcdrY1\r\n"
    return "Started." .. extra, "text/plain; charset=utf-8"
end

function pause()
    skipall()
    send "\x1bY2RCDR\r"
    expect "RcdrY2\r\n"
    return "Paused."
end

-- FIXME: this is buggy. Seems like the SMP has a truncation bug somewhere in the dir listing
-- since it reports always around 500 MB free.
-- Doesn't seem like there's any other way to do this via telnet...
--[[
local function _diskfree()
    -- it's faster to not enumerate root and just go to some unused subdir "x"
    -- kinda dumb to do it like this but didn't find a better way to do this
    send "\x1b/recordings/x/CJ\r\x1bLF\r"
    expect "Dir recordings/x/\r\n"
    local b
    while true do
        local line = readline()
        if line == "" then
            break
        end
        if not r then
            b = line:match "^(%d+) Bytes Left$"
            b = b and tonumber(b)
        end
    end
    return b
end
]]

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

-- unchecked, will fail with too long val or val=""
local function setmetaID(id, val)
    local s = ("\x1bM%d*%sRCDR\r"):format(id, val)
    local e = ("RcdrM%d*%s\r\n"):format(id, val) -- The manual lies about this, lol
    skipall()
    send(s)
    expect(e)
end

local function setmeta(k, val)
    local id = _metainv[k] -- look up ID given the metadata field name
    if not id then
        return nil, k
    end
    val = tostring(val):match"^%s*(.-)%s*$" -- strip leading/trailing whitespace
    local warn
    if #val > 127 then -- 127 chars is all the SIS protocol allows
        val = val:sub(1, 127)
        warn = "field truncated"
    end
    checkparam(val)
    setmetaID(id, val)
    return val, warn
end

function echoparams(params)
    local t = { "params = " .. tostring(params) .. "\n-------------" }
    if params then
        local i = #t
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

function alarms()
    send "39I"
    return readline()
end

function files()
    skipall()
    send "\x1b/CJ\r\x1bLF\r"
    expect "Dir /\r\n" -- Manual says "Dirl", but it's just "Dir"
    local t = {}
    local i = 0
    local total = 0
    while true do
        local line = readline()
        if line == "" then
            break
        end
        local sz = line:match " (%d+)$"
        total = total + ((sz and tonumber(sz)) or 0)
        i = i + 1
        t[i] = line
    end
    total = total // (1024*1024)
    table.insert(t, ("Total size: %d MB"):format(total))
    skipall()
    return table.concat(t, "\n")
end

--[[
function diskfree()
    local b = assert(_diskfree(), "Device did not report free space")
    local mb = b // (1024*1024)
    return ("%d B\n%d MB\n"):format(b, mb)
end
]]

local function _authHdr()
    -- basic auth is a bad idea when sending over the open internet, but (1) we're in a LAN,
    -- and (2) the telnet/SIS connection is completely unencrypted and has no SSL, so sending this
    -- in plaintext doesn't even make it worse
    local user = CONFIG.user or "admin"
    local pw = assert(CONFIG.password)
    return "Basic " .. base64enc(user .. ":" .. pw)
end

local function readall(s)
    local ret
    local d, err
    while true do
        d, err = s:read()
        if not d then
            break
        elseif d == "" then
            coroutine.yield()
        else
            ret = (ret or "") .. d
        end
    end
    return ret, err
end

-- unfortunately there are no SIS functions for livestreaming, so we need a bit of a kludge:
-- The SMP device has an undocumented, internal REST API that is also callable from outside
-- as long as we supply the proper admin login (which we know so this is easy)
-- The web-interface itself uses this (trace outgoing REST calls in the browser if in doubt)
local function livestartstop(streamer, on)
    -- obtained from sniffing network traffic via firefox site debugger
    local payload = ('[{"uri":"/streamer/rtmp/%d/pub_control","value":%d}]'):format(streamer, on)
    local h =
    {
        Authorization = _authHdr(),
        -- Browser sends application/x-www-form-urlencoded; charset=UTF-8 but this is fine too
        ["Content-Type"] = "application/json; charset=UTF-8",
        ["Content-Length"] = tostring(#payload),
    }
    local req = extrond.httpFormatRequest("PUT", CONFIG.host, "/api/swis/resources", h)
    local conn, status, statustext, contentlen, headers = httprequest(req .. payload, CONFIG.host, 80, false, 5000) -- gets invalid response header and reports failure
    if conn then
        local data = readall(conn)
        conn:close()
        if loadjson(data) then -- ... but if it parses as some json, we're good
            return true -- ok
        end
    end
    error("conn:readResponse() failed:\n" .. status .. "\nData:\n" .. tostring(remain))
end

function livestart()
    livestartstop(1, 1)
    livestartstop(2, 1)
    return "livestream started"
end

function livestop()
    livestartstop(1, 0)
    livestartstop(2, 0)
    return "livestream stopped"
end

local intToBool = setmetatable(
    { [0] = false, [1] = true },
    { __index = error }
)

function _isstreaming()
    local h =
    {
        Authorization = _authHdr(),
    }
    local req = extrond.httpFormatRequest("GET", CONFIG.host, "/api/swis/resources?uri=/streamer/rtmp/1/pub_control&uri=/streamer/rtmp/2/pub_control", h)
    
    local conn, status, statustext, contentlen, header = httprequest(req, CONFIG.host, 80, false, 5000)
    -- likely was an invalid HTTP response header, try to parse the content anyway
    if conn then
        local data = readall(conn)
        conn:close()
        local j
        if data then -- payload should be json, if it parses ok we're good
            j = loadjson(data)
            print(j)
        end
        if j then
            local on = false
            local t = {}
            for _, e in pairs(j) do
                local id = assert(e.meta.uri:match"^/streamer/rtmp/(%d+)/pub_control")
                local v = assert(e.result)
                id = math.tointeger(id)
                v = intToBool[v]
                t[id] = v
                on = on or v
            end
            return on, t -- simple + detailed per-stream
        end
    end
    return nil, "conn:readResponse() failed:\n" .. statustext .. "\nData:\n" .. tostring(data)
end

function livestatus()
    local on, x = _isstreaming()
    if on == nil and x then
        return x, 502, "text/plain; charset=utf-8"
    end
    return tostring((on and 1) or 0), "text/plain; charset=utf-8"
end

local _status = { "stopped", "recording", "paused" }
function status()
    timeout "1500ms"
    skipall()
    -- recording status
    send "\x1bYRCDR\r35I" -- send this early so that the reply has time to arrive
    local st = readline() -- "<ESC>YRCDR\r"
    local dur = readline() -- "35I"
    st = _status[math.tointeger(st) + 1]
    skipall()
    -- disk status (broken)
    local b --= _diskfree()
    local suffix = ""
    if b then
        local mb = b // (1024*1024)
        suffix = (" [%d MB free]"):format(mb)
    end
    -- livestream status
    local live, liveerr = _isstreaming()
    if live == nil then
        live = "error"
    else
        live = (live and "<b>ON</b>") or "off"
    end
    --
    return ("%s (%s)%s, stream: %s"):format(st, dur, suffix, live)
end

-- TODO: detect ingesting status
