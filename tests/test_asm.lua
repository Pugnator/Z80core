#!/usr/local/bin/lua

passed = 0
failed = 0
skipped = 0
total_count = 0
TEMP_LISTING = "temp.lst"
OUTPUT_BIN = "out.bin"
FAILED_LIST = "failed.txt"
previous = ""
release = false

function trim(str)
  return (str:gsub("^%s*(.-)%s*$", "%1"))
end

function assembly_test(expected_hex, expected_size, duplicated)
  local exec = io.popen('..\\bin\\zasm.exe -s temp.lst -o ' .. OUTPUT_BIN)
  local result = exec:read("*a")
  exec:close()
  
  res_file,err = io.open(OUTPUT_BIN, "rb")
  if err then
		print("Error creating temp source file!" .. result)
		os.exit()
	end
  local compiled_bytes = res_file:read("*all")
  local content = {compiled_bytes:byte(1,-1)}
  local compiled_size = res_file:seek("end")
  
  local compiled_hex = ""
  for i = 1, #content do
    compiled_hex = compiled_hex .. string.format("%.2X", content[i])
  end
  
  if 0 == compiled_size then
    print(result)
  end
  
  retval = compiled_size == expected_size and tonumber(expected_hex) == tonumber(compiled_hex, 16)
  
  if not retval and not duplicated then
    print(string.format("Result: %X = %X, size %u = %u", tonumber(expected_hex), tonumber(compiled_hex, 16), expected_size, compiled_size))
  end

  res_file:close()  
return retval
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
  
  out = io.open (TEMP_LISTING, "w")
  out:write(".org 0\nstart:\n")
  out:write(str..'\n')
  out:write(".end\n")
  io.close(out)
  if not release then print(string.format("[Test %u]### Opcode: %s, Expected: %s, size: %d", total_count, op, expected, size)) end
  
  if assembly_test(expected, tonumber(size), previous == op) then
    passed = passed + 1
    if not release then print("[PASSED]") end
  else
    if previous == op then
      skipped = skipped + 1
      if not release then print("[SKIPPED]") end
    else
      failed = failed + 1
      print(string.format("[Test %u]### Opcode: %s, Expected: %s, size: %d", total_count, op, expected, size))
      print("[FAILED]")
      fail_log = io.open (FAILED_LIST, "a")	 
      fail_log:write(str.."\n")
      io.close(fail_log)
    end
  end
  previous = op  
end

function process_test_list(filename)
	fh,err = io.open(filename)
	if err then
		print("Error opening source file!")
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


os.remove(FAILED_LIST)
print("Assembler test")
process_test_list("asm_test.asm")
print(string.format("Total:\n%u PASSED\n%u FAILED\n%u SKIPPED", passed, failed, skipped))
os.remove(OUTPUT_BIN)
os.remove(TEMP_LISTING)