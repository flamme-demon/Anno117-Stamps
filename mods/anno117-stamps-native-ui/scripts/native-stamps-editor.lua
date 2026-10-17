-- CPopup copies BodyText into CGenericPopupData.DescriptionText on opening.
-- A later asset/TextId change alone does not repaint that retained UI string.
local Editor = {}
Editor.__index = Editor

local function get(node,key)
    if node==nil then return nil end
    local ok,value=pcall(function() return node[key] end)
    if ok then return value end
end

local function description(scene)
    local data=get(scene,"GenericPopupSceneData") or
        get(get(scene,"SceneData"),"GenericPopupSceneData")
    return get(data,"DescriptionText")
end

local function visibleText()
    local scenes=get(_G.ui,"Scenes")
    local node=description(get(scenes,"GenericPopup"))
    if node then return node end
    -- ui.Scenes is the game's registered scene table. Resolve by its actual
    -- exported GenericPopupSceneData property, without guessing scene aliases.
    if type(scenes)=="table" then
        local count=0
        for _,scene in pairs(scenes) do
            count=count+1; if count>512 then break end
            node=description(scene)
            if node then return node end
        end
    end
end

function Editor.new(config,log)
    return setmetatable({config=config,log=log},Editor)
end

function Editor:Reset()
    self.revision,self.text,self.presented,self.lastError=nil,nil,nil,nil
end

function Editor:Tick()
    local ok,errorMessage=pcall(function()
        local c=self.config
        local revision=Stamp:GetStampIconGUID(c.editor_revision)
        if revision==0 then self:Reset(); return end
        local dialog=Stamp:GetStampIconGUID(c.editor_dialog)
        if type(dialog)~="number" or dialog<=0 then error("dialogue d'édition absent") end
        if revision~=self.revision then
            local length=Stamp:GetStampIconGUID(c.editor_length)
            if type(length)~="number" or length<0 or length>4095 or length%1~=0 then error("taille du texte invalide") end
            local parts={}
            for offset=0,length-1,3 do
                local value=Stamp:GetStampIconGUID(c.editor_bytes+offset/3)
                if type(value)~="number" or value<0 or value>0xffffff or value%1~=0 then error("texte d'édition invalide") end
                for _=1,math.min(3,length-offset) do
                    parts[#parts+1]=string.char(value%256); value=math.floor(value/256)
                end
            end
            if Stamp:GetStampIconGUID(c.editor_revision)~=revision then return end
            self.revision,self.text=revision,table.concat(parts)
        end
        local node=visibleText()
        if not node then error("texte du dialogue natif indisponible") end
        -- Change the rendered CTextData, not only its initialization asset.
        if get(node,"Text")~=self.text then
            local setter=get(node,"SetText")
            if type(setter)=="function" then setter(node,self.text) else node.Text=self.text end
        end
        if get(node,"Text")~=self.text then error("mise à jour du texte refusée") end
        if self.presented~=dialog then
            self.presented=dialog
            self.log("texte du dialogue de saisie actualisé : "..tostring(dialog))
        end
    end)
    if not ok then
        local message=tostring(errorMessage)
        if self.lastError~=message then self.lastError=message; self.log("affichage de la saisie différé : "..message) end
    else self.lastError=nil end
end

return Editor
