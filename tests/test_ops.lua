#!/usr/local/bin/lua

passed = 0
failed = 0

function trim(str)
  return (str:gsub("^%s*(.-)%s*$", "%1"))
end

function assembly_test(bin, size)
  local exec = io.popen('..\\bin\\zasm.exe -s temp.lst -t out')
  local result = exec:read("*a")
  exec:close()
  
  res_file,err = io.open("out", "rb")
  if err then
		print("Error!")
		return
	end
  local hex = res_file:read(10)
  local content = {hex:byte(1,-1)}
  local res_size = res_file:seek("end")
  print(string.format("%X - %X%X", bin, tonumber(content[1]), tonumber(content[2])))
 
  res_file:close()
  --os.remove("out")
  return res_size == size
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
  print(string.format("### Test opcode: %s, Expected: %s, size: %d", op, expected, size))
  
  
  out = io.open ("temp.lst", "w")
  out:write(".org 0\nstart:\n")
  out:write(str..'\n')
  out:write(".end\n")
  io.close(out)
  
  if assembly_test(expected, tonumber(size)) then
    passed = passed + 1
    print("[PASSED]")
  else
    failed = failed + 1
    print("[FAILED]")
  end
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


print("Assembler test")
process_test_list("test.asm")
print(string.format("Total:\n%u PASSED\n%u FAILED", passed, failed))