-- Test script to send a protobuf payload to the log_server
-- Usage: xmake lua tools/log_server/test.lua

import("core.base.socket")

function main()
    local payload = {
        8, 128, 139, 244, 134, 6, 16, 1, 26, 11, 115, 114, 99, 47, 109, 97, 105, 110, 46, 114, 115,
        32, 42, 42, 6, 115, 116, 100, 111, 117, 116, 50, 24, 115, 116, 97, 114, 116, 105, 110, 103,
        32, 116, 111, 97, 115, 116, 32, 101, 110, 103, 105, 110, 101, 46, 46, 46
    }

    local sock = socket.tcp()
    sock:connect("127.0.0.1", 8080)
    sock:send(string.char(table.unpack(payload)))
    sock:close()

    print("Payload sent successfully")
end
