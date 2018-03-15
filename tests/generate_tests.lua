#!/usr/local/bin/lua

list = {}

function trim(str)
  return (str:gsub("^%s*(.-)%s*$", "%1"))
end

function process_opcode(str)
  local op = str:match('^(.*);')
  if null == op then return end
  op = trim(op)
  local expected = str:match(';(.*)#') 
  if null == expected then return end
  expected = trim(expected)
  local size = str:match('#size:(%d)$') 
  if null == size then return end
  
  list[op] = str
  
  --print(string.format("%s\t\t\t;%s\t\t\t#size:%d", op, expected, size))
  return true
end

function process_test_list(filename)
	fh,err = io.open(filename)
	if err then
		print("Error!")
		return
	end
	while true do
      line = fh:read()
      if line == nil then break end
      if ';' ~= line:sub(1,1) then
        process_opcode(line)
      end
	end
end


process_test_list("all.asm")

for k,v in pairs(list) do
  print(v)
end