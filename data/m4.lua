#!/usr/local/bin/lua

dirList = 
{
	{name="#define", descr="define", args=2}
}

function split(inputstr, sep)
    if sep == nil then
            sep = "%s"
    end
    local t={} ; i=1
    for str in string.gmatch(inputstr, "([^"..sep.."]+)") do
            t[i] = str
            i = i + 1
    end
    return t
end

function findDirect (token)
	for k, v in pairs(dirList) do 
		if v.name == token then return v end
	end
end

function processDirectives (string)
	for k, v in pairs(string) do 
		d = findDirect(v)
        if d ~= nil then
        	print("Directive found, it is "..d.descr.." "..string[2].." as "..string[3])
        end
	end
end

function load_source(filename)
	fh,err = io.open(filename)
	if err then
		print("Error!")
		return
	end
	while true do
	        line = fh:read()
	        if line == nil then break end
	        if ';' ~= line:sub(1,1) then
	        tok = split(line)
	        processDirectives(tok)
	        
		end
	end
end



load_source("test.asm")
