#!/usr/local/bin/lua

passed = 0
failed = 0
total_count = 0
TEMP_LISTING = "temp.lst"
OUTPUT_BIN = "out.bin"

function trim(str)
  return (str:gsub("^%s*(.-)%s*$", "%1"))
end

function assembly_test(expected_hex, expected_size)
  local exec = io.popen('..\\bin\\zasm.exe -s temp.lst -t ' .. OUTPUT_BIN)
  local result = exec:read("*a")
  exec:close()
  
  res_file,err = io.open(OUTPUT_BIN, "rb")
  if err then
		print("Error!")
		return
	end
  local compiled_bytes = res_file:read("*all")
  local content = {compiled_bytes:byte(1,-1)}
  local compiled_size = res_file:seek("end")
  
  local compiled_hex = ""
  for i = 1, #content do
	compiled_hex = compiled_hex .. string.format("%X", content[i])
  end
  
  print(string.format("Result: %X = %X, size %u = %u", tonumber(expected_hex), tonumber(compiled_hex, 16), expected_size, compiled_size))

  res_file:close()
  os.remove(OUTPUT_BIN)
  
  return compiled_size == expected_size and tonumber(expected_hex) == tonumber(compiled_hex, 16)
end

function process_opcode(str)
  total_count = total_count + 1
  local op = str:match('^(.*);')
  if null == op then return end
  op = trim(op)
  local expected = str:match(';(.*)#') 
  if null == expected then return end
  expected = trim(expected)
  local size = str:match('#size:(%d)$') 
  if null == size then return end
  print(string.format("[Test %u]### Test opcode: %s, Expected: %s, size: %d", total_count, op, expected, size))
  
  
  out = io.open (TEMP_LISTING, "w")
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
	fail_log = io.open ("failed.txt", "a")	 
	fail_log:write(str.."\n")
	io.close(fail_log)
  end
  os.remove(TEMP_LISTING)
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
process_test_list("all.asm")
print(string.format("Total:\n%u PASSED\n%u FAILED", passed, failed))