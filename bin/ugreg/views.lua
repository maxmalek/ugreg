-- THIS IS A DRAFT

--[=[
functions:
K(s, ...): construct comparator (Cmp)that goes T/s/.../ where T is the starting tree and has more metamethods
keys(V) V:keys()
values(V) V:values()


Cmp:
.eq .gt .lt .gte .lte .ne
op| - union
op& - intersection

[] aka __index can get string or Cmp:
- string: lookup that subkey
- Cmp: 

]=]

function testview(data, params)
    local ids = data.rooms[K"name".eq(params.name)].id
    local rooms = data.users[K"room".eq(ids)]
    return rooms
end
