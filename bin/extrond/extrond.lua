-- Base library to provide shared functions to the device modules
local M = {}

function M.httpFormatRequest(method, host, resource, extra)
    local t =
    {
        method .. " " .. resource .. " HTTP/1.1",
        "Host: " .. host,
        "Connection: close",
    }
    local i = #t
    if extra then
        for k, v in pairs(extra) do
            i = i + 1
            t[i] = ("%s: %s"):format(k, v)
        end
    end
    t[i+1] = "\r\n" -- terminator
    return table.concat(t, "\r\n")
end

return M
