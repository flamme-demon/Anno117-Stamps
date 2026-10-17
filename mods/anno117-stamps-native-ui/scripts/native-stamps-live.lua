-- Live inventory and native Phoenix controls. No io, web UI or stamp rewriting.
local Live = {}
Live.__index = Live

local function get(object, key)
    if object == nil then return nil end
    local ok, value = pcall(function() return object[key] end)
    if ok then return value end
    return nil
end

local function put(object, key, value)
    if object == nil then return false end
    local ok = pcall(function()
        local setter = get(object, "Set" .. key)
        if type(setter)=="function" then setter(object, value)
        elseif object[key] ~= value then object[key] = value end
    end)
    return ok and get(object, key)==value
end

-- PhoenixArray can be a native indexed userdata, not a Lua table. In particular
-- a popup's newest entries and the tooltip sections must also be traversed.
local function children(node, callback)
    -- PhoenixArray bindings register GetSize/GetElement and use zero-based
    -- indices (FUN_1415cebd0, FUN_14160a0c0, FUN_1415e1e70). The generated
    -- any[][] annotation does not make these native arrays Lua tables.
    local size, element=get(node,"GetSize"),get(node,"GetElement")
    if type(size)=="function" and type(element)=="function" then
        local ok,count=pcall(size,node)
        if ok and type(count)=="number" and count>=0 and count<=8192 and count%1==0 then
            for index=0,count-1 do
                local valid,value=pcall(element,node,index)
                if valid then callback(value) end
            end
            return
        end
    end
    if type(node)=="table" then
        for _,child in pairs(node) do callback(child) end
    elseif type(node)=="userdata" then
        -- Luau's native arrays may expose generalized iteration (__iter)
        -- without Lua's pairs or length metamethods.
        local metaOK, meta=pcall(getmetatable,node)
        local iter=metaOK and get(meta,"__iter")
        if type(iter)=="function" then
            local ok, nextValue, state, key=pcall(iter,node)
            if ok and type(nextValue)=="function" then
                for _=1,8192 do
                    local k,value=nextValue(state,key)
                    if k==nil then break end
                    callback(value or k); key=k
                end
                return
            end
        end
        local ok, length=pcall(function() return #node end)
        local first=get(node,0)~=nil and 0 or 1
        if ok and type(length)=="number" and length>=0 and length<=8192 then
            for index=first,first+length-1 do callback(get(node,index)) end
        else
            for index=first,first+255 do
                local value=get(node,index)
                if value==nil then break end
                local valid=get(value,"isValid")
                if type(valid)=="function" then
                    local checked, active=pcall(valid,value)
                    if checked and not active then break end
                end
                callback(value)
            end
        end
    end
end

function Live.new(config, entries, log)
    return setmetatable({config=config, entries=entries, log=log, rows={}, tick=0,
                         revision=nil, sweep=true, lastError=nil, reportedView=false}, Live)
end

function Live:Query(command)
    local value = Stamp:GetStampIconGUID(command)
    if type(value) ~= "number" or value < 0 or value % 1 ~= 0 then error("réponse catalogue invalide") end
    return value
end

function Live:ReadString(row, lengthCommand, bytesCommand, maximum)
    local length = self:Query(lengthCommand + row)
    if length > maximum then error("texte de catalogue trop long") end
    local parts, remaining = {}, length
    for chunk=0, math.ceil(length/3)-1 do
        local value = self:Query(bytesCommand + row*256 + chunk)
        if value > 0xffffff then error("bloc de texte invalide") end
        for _=1, math.min(3, remaining) do
            parts[#parts+1] = string.char(value % 256)
            value = math.floor(value / 256)
        end
        remaining = remaining - 3
    end
    return table.concat(parts)
end

function Live:IsManaged(guid)
    local c = self.config
    return type(guid)=="number" and guid>=c.pool_base and guid<c.pool_base+2*c.folders*c.stride
end

function Live:Refresh(force)
    local ok, changed = pcall(function()
        local c = self.config
        if self:Query(c.protocol) ~= 6 then error("DLL avec catalogue vivant v6 indisponible") end
        local revision = self:Query(force and c.refresh or c.revision)
        if revision==0 then error("catalogue pas encore disponible") end
        if self:Query(c.presentation)~=1 then error("sources natives de nom et d’icône pas encore disponibles") end
        if revision==self.revision and not self.sweep then return false end
        local count = self:Query(c.count)
        if count > 2*c.folders*c.stride then error("trop d'entrées dans le catalogue") end
        local rows, entries = {}, {}
        for index=0, count-1 do
            local guid = self:Query(c.row_guid + index)
            if not self:IsManaged(guid) or rows[guid] then error("GUID de catalogue invalide") end
            local row = {guid=guid, icon_guid=self:Query(c.row_icon + index),
                         name=self:ReadString(index, c.name_length, c.name_bytes, 255),
                         icon=self:ReadString(index, c.icon_length, c.icon_bytes, 511)}
            row.folder = (guid-c.pool_base)%c.stride==0
            row.buildings = self:Query(c.building_count + index)
            row.road_tiles = self:Query(c.road_tiles + index)
            row.polygons = c.polygon_count and self:Query(c.polygon_count+index) or 0
            row.components = {}
            local parts = self:Query(c.component_count + index)
            if parts>4097 then error("composition du stamp invalide") end
            for part=0, parts-1 do
                local offset=index*4097+part
                row.components[#row.components+1] = {
                    guid=self:Query(c.component_guid+offset),
                    amount=self:Query(c.component_amount+offset)}
            end
            rows[guid] = row
            if not row.folder then
                entries[guid] = {path=row.name, name=row.name, elements=row.buildings+row.road_tiles+row.polygons}
            end
        end
        -- Query metadata does not rescan. Verify the snapshot before publishing it.
        if self:Query(c.revision) ~= revision then error("catalogue en cours d'actualisation") end
        if self.sweep then
            -- Unlock state can be serialized by a save. Hide unoccupied slots again.
            for guid=c.pool_base, c.pool_base+2*c.folders*c.stride-1 do
                if not rows[guid] and Unlock:IsUnlocked(guid) then Unlock:Relock(guid) end
            end
        else
            for guid in pairs(self.rows) do
                if not rows[guid] and Unlock:IsUnlocked(guid) then Unlock:Relock(guid) end
            end
        end
        -- Unlock children first, then their folder: never expose an empty folder.
        for _, folder in ipairs({false, true}) do
            for guid, row in pairs(rows) do
                if row.folder==folder and not Unlock:IsUnlocked(guid) then Unlock:Unlock(guid) end
            end
        end
        for guid in pairs(self.entries) do self.entries[guid] = nil end
        for guid, entry in pairs(entries) do self.entries[guid] = entry end
        self.rows, self.revision, self.sweep = rows, revision, false
        self.reportedView=false; self.reportedTooltip=false
        local stamps = 0
        for _ in pairs(entries) do stamps=stamps+1 end
        self.log("catalogue actualisé sans redémarrage : " .. tostring(stamps) .. " stamp(s)")
        local errors = self:Query(c.errors)
        if errors>0 then self.log(tostring(errors) .. " fichier(s) refusé(s) ou emplacements épuisés ; détails dans le journal DLL") end
        return true
    end)
    if not ok then
        local message = tostring(changed)
        if self.lastError~=message then self.lastError=message; self.log("actualisation différée : " .. message) end
        return false
    end
    self.lastError=nil
    return changed
end

function Live:Costs(row)
    local api = get(get(_G.rdgs, "VirtualSpaceBuildCost"), "GetFromGuid")
    if type(api)~="function" then return nil end
    local products = {}
    for _, component in ipairs(row.components) do
        local ok, price = pcall(api, component.guid)
        local costs = ok and get(price, "Costs")
        if type(costs)~="table" then return nil end
        for _, cost in pairs(costs) do
            local guid, amount = get(cost, "ProductGUID"), get(cost, "Amount")
            if type(guid)~="number" or type(amount)~="number" or amount<0 then return nil end
            if amount>0 then
                local product = products[guid] or {guid=guid, amount=0, icon=get(cost,"Icon"), name=get(cost,"Name"),
                                                   available=get(cost,"AvailableAmount")}
                product.amount=product.amount+amount*component.amount
                if type(product.name)~="string" or type(product.icon)~="string" then return nil end
                products[guid]=product
            end
        end
    end
    local ordered={}
    for _, product in pairs(products) do ordered[#ordered+1]=product end
    table.sort(ordered,function(a,b)
        if a.guid==1010017 then return b.guid~=1010017 end
        if b.guid==1010017 then return false end
        return a.guid<b.guid
    end)
    return ordered
end

function Live:PaintTooltip(row)
    local tip = get(get(get(_G.ui,"Scenes"),"Infotip"),"InfoTipData")
    if not get(tip,"IsVisible") then return end
    local function walk(node, callback, depth)
        if node==nil or depth>8 then return end
        if type(node)~="table" and type(node)~="userdata" then return end
        callback(node)
        children(node,function(child) walk(child,callback,depth+1) end)
    end
    local heading=false
    walk(get(tip,"Header"),function(node)
        if not heading and type(get(node,"HeadlineTextString"))=="string" then
            heading=put(node,"HeadlineTextString",row.name)
            if get(node,"IconID")~=nil and row.icon~="" then put(node,"IconID",row.icon) end
            if get(node,"SublineTextString")~=nil then
                put(node,"SublineTextString",row.folder and "Dossier de stamps" or "Stamp")
            end
        end
    end,0)
    if row.folder then return end
    local prices=self:Costs(row)
    local summary=tostring(row.buildings).." bâtiment(s) · "..tostring(row.road_tiles).." case(s) de route"
    if row.polygons and row.polygons>0 then summary=summary.." · "..row.polygons.." zone(s)" end
    local grids, paragraph = {}, nil
    walk(get(tip,"Body"),function(node)
        if not paragraph and type(get(node,"TextString"))=="string" then paragraph=node end
        local cells=get(node,"DataType")
        if type(cells)=="table" or type(cells)=="userdata" then
            local function collect(value,depth)
                if value==nil or depth>5 then return end
                if type(get(value,"Icon"))=="string" and type(get(value,"Text"))=="string" then
                    grids[#grids+1]=value
                else children(value,function(child) collect(child,depth+1) end) end
            end
            collect(cells,0)
        end
    end,0)
    if prices then
        if #grids>=#prices and #grids>0 then
            for index,cell in ipairs(grids) do
                local price=prices[index]
                put(cell,"Icon",price and price.icon or "")
                put(cell,"Text",price and tostring(price.amount) or "")
                put(cell,"IsWarning",price~=nil and type(price.available)=="number" and price.available<price.amount)
            end
        else
            local lines={summary,"Coût de construction (base) :"}
            for _,price in ipairs(prices) do lines[#lines+1]=tostring(price.amount).." "..price.name end
            if #prices==0 then lines[#lines+1]="Gratuit" end
            summary=table.concat(lines,"\n")
            for _,cell in ipairs(grids) do put(cell,"Icon",""); put(cell,"Text","") end
            if not paragraph and grids[1] then
                local values={}
                for _,price in ipairs(prices) do values[#values+1]=tostring(price.amount).." "..price.name end
                put(grids[1],"Text",#prices>0 and table.concat(values," / ") or "Gratuit")
            end
        end
    else
        -- Never present the template residence's cost as the cost of the group.
        summary=summary.."\nCoût du groupe indisponible"
        for _,cell in ipairs(grids) do put(cell,"Icon",""); put(cell,"Text","") end
        if not paragraph and grids[1] then put(grids[1],"Text","Coût du groupe indisponible") end
    end
    if paragraph then put(paragraph,"TextString",summary) end
    if heading and paragraph and not self.reportedTooltip then
        self.reportedTooltip=true
        self.log("infobulle de stamp mise à jour : "..row.name)
    end
end

function Live:Paint()
    local scene = get(get(_G.ui, "Scenes"), "ConstructionMenu")
    local data = get(scene, "ConstructionMenuData")
    if not data then return end
    local seen, budget, focused = {}, 8192, nil
    local function findGuid(context, info, pagination)
        local building=get(pagination,"BuildingGuid")
        if self:IsManaged(building) then return building end
        for _, key in ipairs({"Guid", "GuidSecondary"}) do
            local guid = get(context, key)
            if self:IsManaged(guid) then return guid end
        end
        local ref=get(info,"RefGUID")
        if self:IsManaged(ref) then return ref end
    end
    local function visit(node, depth)
        if node==nil or depth>8 or budget<=0 or seen[node] then return end
        local kind = type(node)
        if kind~="table" and kind~="userdata" then return end
        seen[node]=true; budget=budget-1
        -- Group popups and production-chain popups expose different button types.
        local base = get(node,"BtnBaseData") or get(node,"BtnData")
        if base then
            local states = get(base, "States")
            local guid = findGuid(get(states,"Context"),get(base,"InfoTip"),get(node,"PaginationData"))
            if guid then
                local row = self.rows[guid]
                if row then
                    -- The DLL updates CStandard.IconFilename and the native
                    -- TextId source before unlock. Writing these frame models
                    -- here competes with the game's next bind and causes flicker.
                    if get(states,"IsFocused") or get(get(node,"PaginationData"),"IsFocused") then focused=row end
                end
            end
        end
        local interaction = get(node, "CategoryInteraction")
        local categoryGuid = findGuid(get(interaction, "Context"))
        if categoryGuid and self.rows[categoryGuid] then
            local row = self.rows[categoryGuid]
            if get(interaction,"IsFocused") and not focused then focused=row end
        end
        -- Native array exports are tables of rows containing weak game objects.
        children(node,function(value) visit(value,depth+1) end)
        for _, key in ipairs({"BuildingsBtns","PinnedObject","AdditionalEntry","BuildingsEntry"}) do visit(get(node,key),depth+1) end
    end
    for _, key in ipairs({"PinSlots_Left", "PinSlots_Center", "PinSlots_Right"}) do visit(get(data, key), 0) end
    visit(get(get(data, "ConstructionData"), "CategoryBuildingsData"), 0)
    local popups = get(data, "BuildingsPopupData")
    visit(get(popups, "Popup1BuildingsGroupData"), 0)
    visit(get(popups, "Popup2BuildingsGroupData"), 0)
    visit(get(popups,"Popup1ProductionChainData"),0)
    visit(get(popups,"Popup2ProductionChainData"),0)
    -- Mouse hover does not imply keyboard IsFocused. The native tooltip manager
    -- supplies the reference of the widget actually hovered by the player.
    local manager=get(_G.InfoTip,"get")
    if type(manager)=="function" then
        local ok, tip=pcall(manager,_G.InfoTip)
        if ok then focused=self.rows[get(tip,"RefGuid")] end
    end
    if focused then self:PaintTooltip(focused) end
end

function Live:Tick()
    self.tick=self.tick+1
    if self.revision==nil or self.tick%30==0 then self:Refresh(false) end
    do
        local ok, err = pcall(function() self:Paint() end)
        if not ok and self.paintError~=tostring(err) then
            self.paintError=tostring(err); self.log("mise à jour visuelle différée : " .. tostring(err))
        end
    end
end

function Live:OnLoad()
    self.revision=nil; self.sweep=true; self.tick=0; self.reportedView=false; self.reportedTooltip=false
    self.rows={}
    for guid in pairs(self.entries) do self.entries[guid]=nil end
end

function Live:HoveredGuid()
    local tip=get(get(get(_G.ui,"Scenes"),"Infotip"),"InfoTipData")
    if not get(tip,"IsVisible") then return nil end
    local ok,manager=pcall(function() return InfoTip:get() end)
    if not ok then return nil end
    local source=get(manager,"CheckInfoTipSource")
    if type(source)=="function" then
        local checked,active=pcall(source,manager,"ConstructionMenu")
        if not checked or not active then return nil end
    end
    local guid=get(manager,"RefGuid")
    return self.rows[guid] and guid or nil
end

return Live
