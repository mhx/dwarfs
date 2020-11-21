function filter(f)
  -- if f.name == 'Jamroot' or (f.name == 'test' and f.type == 'dir') then
  --   return false
  -- end
  return true
end

function autovivify(C, args, num)
  for i = 1, num do
    local v = args[i]
    if C[v] == nil then C[v] = {} end
    C = C[v]
  end
  return C
end

function incr(C, ...)
  local args = { n = select("#", ...), ... }
  C = autovivify(C, args, args.n - 2)
  local field = args[args.n - 1]
  C[field] = (C[field] or 0) + args[args.n]
end

function push(C, ...)
  local args = { n = select("#", ...), ... }
  C = autovivify(C, args, args.n - 1)
  table.insert(C, args[args.n])
end

function sortbysize(tbl)
  return function (a, b)
           return tbl[b]["size"]/tbl[b]["num"] < tbl[a]["size"]/tbl[a]["num"]
         end
end

function order(filelist)
  local C = {}
  for _, f in pairs(filelist) do
    local _, _, base, ext = string.find(f.name, "(.*)(%.%w+)$")
    if ext == nil or string.find(ext, "[a-z]") == nil then
      base, ext = f.name, ""
    end
    incr(C, ext, "size", f.size)
    incr(C, ext, "num", 1)
    incr(C, ext, "name", base, "size", f.size)
    incr(C, ext, "name", base, "num", 1)
    push(C, ext, "name", base, "files", f)
  end
  local ordered = {}
  local exts = {}
  for k, _ in pairs(C) do table.insert(exts, k) end
  table.sort(exts, sortbysize(C))
  for _, ext in ipairs(exts) do
    local N = C[ext]["name"]
    local bases = {}
    for k, _ in pairs(N) do table.insert(bases, k) end
    table.sort(bases, sortbysize(N))
    for _, base in ipairs(bases) do
      local files = N[base]["files"]
      table.sort(files, function (a, b)
                          return b.size < a.size
                        end)
      for _, file in ipairs(files) do
        table.insert(ordered, file)
      end
    end
  end
  return ordered
end
