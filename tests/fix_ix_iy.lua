fixed = 0

function trim(str)
  return (str:gsub("^%s*(.-)%s*$", "%1"))
end

function fix_pointer(str)
  local op = str:match('^(.*);')
  if null == op then return false end
  op = trim(op)
  local expected = str:match(';(.*)#') 
  if null == expected then return false end
  expected = trim(expected)
  local size = str:match('#size:(%d)$') 
  if null == size then return false end
  fixed = fixed + 1
  op = string.gsub(op, "IX [+] 0", "IX + 0x7F")
  expected = string.gsub(expected, "aa", "7f")
  print(string.format("%s       		;%s       		#size:%d", op, expected, size))
  return true
end

function process_file(filename)
	fh,err = io.open(filename)
	if err then
		print("Error!")
		return
	end
	while true do
		line = fh:read()
    if line == nil then break end
    if line:match('^(.*)[IX [+] 0](.*)') then
      fix_pointer(line)      
    else
      print(line)
    end      
	end
end

process_file("all.asm")