-- Le menu et ses icônes sont créés par assets.xml, dans l'UI de construction.
-- Une entrée utilise un GUID réservé de prévisualisation. Sur le tick logique,
-- ce GUID est transformé en demande adressée au hook CreateStamp de la v85.
local UI = {}
local catalogue = require("native-stamps-data")
local entries = catalogue.stamps
local management = catalogue.management
local managementOpen = false
local createButtons = {}
for _, guid in ipairs(catalogue.create_buttons) do createButtons[guid] = true end
local lastGuid = nil
local captureArmed = false
local captureSawDrag = false
local captureEmptyConfirmed = false
local visibilityReady, visibilityRetry, visibilityError = false, 0, nil

local function log(text) print("[STAMPS-UI] " .. text) end
local live = catalogue.live and require("native-stamps-live").new(catalogue.live, entries, log) or nil
local editor = management and management.editor_revision and require("native-stamps-editor").new(management,log) or nil

local function resetPreview()
    pcall(function() BuildMode:get():SetBuildMode(0) end)
end

local function closeConstructionMenu()
    -- PopUI est l'action Échap : différée, ou appelée deux fois, elle ouvre
    -- GamePause et annule Copy. Fermer uniquement le menu qui nous appartient.
    pcall(function()
        local menu = ui.Scenes.ConstructionMenu.ConstructionMenuData
        local popup = menu.BuildingsPopupData
        local invisible = 0x512AA71F
        if popup.Popup2 ~= invisible then popup:Popup2Close() end
        if popup.Popop1 ~= invisible then popup:Popup1Close() end
        if menu.MenuIsOpen then menu:SetMenuIsOpen(false) end
    end)
end

local function ensureVisible()
    if visibilityReady then return end
    if visibilityRetry > 0 then visibilityRetry = visibilityRetry - 1; return end
    -- Load est appelé avant que le manager de construction soit disponible.
    -- Attendre une partie active avant d'utiliser Unlock, au tick logique.
    local active, hasPipette = pcall(function() return BuildMode:get().HasPipetteBuildings end)
    if not active or type(hasPipette) ~= "boolean" then return end
    local ok, err = pcall(function()
        for _, guid in ipairs(catalogue.unlock_guids) do
            if not Unlock:IsUnlocked(guid) then Unlock:Unlock(guid) end
            if not Unlock:IsUnlocked(guid) then error("GUID encore verrouillé : " .. tostring(guid)) end
        end
    end)
    if ok then
        visibilityReady = true
        log("menu disponible : " .. tostring(#catalogue.unlock_guids) .. " entrées déverrouillées")
    else
        visibilityRetry = 60
        if tostring(err) ~= visibilityError then
            visibilityError = tostring(err)
            log("visibilité du menu non confirmée : " .. visibilityError)
        end
    end
end

function UI:Load(guid)
    local entry = entries[guid]
    if not entry then return false end
    -- Fermer les menus avant de construire la nouvelle pipette : une fermeture
    -- après Fill détruirait la sélection et laisserait un outil incohérent.
    closeConstructionMenu()
    -- Le runtime du jeu n'expose pas io. La DLL intercepte uniquement notre
    -- plage de GUIDs dans cette API entière ; les indices ordinaires restent
    -- des lectures d'icônes. +GUID sélectionne, -GUID consomme l'accusé du fill.
    local ok, accepted = pcall(function() return Stamp:GetStampIconGUID(guid) end)
    if not ok or accepted ~= guid then
        resetPreview()
        log("pont direct vers la DLL indisponible : " .. tostring(accepted))
        return false
    end
    log("clic icône : " .. entry.path)
    local created, errorMessage = pcall(function() Stamp:CreateStamp() end)
    local confirmed, answer = pcall(function() return Stamp:GetStampIconGUID(-guid) end)
    if not created or not confirmed or answer ~= guid then
        resetPreview()
        local reason = not created and tostring(errorMessage) or tostring(answer)
        log("chargement non confirmé par la DLL : " .. reason)
        return false
    end
    -- La DLL a exécuté le nettoyage natif puis Fill depuis une source distincte.
    -- 117653 = MultiCopyDummy, dernière étape du chargeur FUN_142e8ca50.
    local activated, activationError = pcall(function() BuildMode:get():SetBuildMode(117653) end)
    if not activated then
        resetPreview()
        log("outil de pose indisponible : " .. tostring(activationError))
        return false
    end
    -- BuildMode.Object décrit le bâtiment d'aperçu, pas le GUID MultiCopyDummy.
    -- Le comparer à 117653 annulait nous-mêmes une pose valide. La DLL valide
    -- déjà les vecteurs avant son accusé ; lire l'état ici reste un diagnostic.
    local checked, state = pcall(function()
        local mode=BuildMode:get()
        return "mode="..tostring(mode.CurrentBuildMode).." copie="..tostring(mode.HasPipetteBuildings)..
            " éléments="..tostring(mode.PipetteBuildingsCount)
    end)
    if checked then log("état après chargement : "..state) end
    log("stamp prêt à poser : " .. entry.path)
    return true
end

function UI:StartCreate(folderGuid)
    captureArmed = false
    captureSawDrag = false
    captureEmptyConfirmed = false
    if management and not folderGuid then return false end
    -- Le clic dans un menu efface la pipette : ouvrir l'outil AVANT la copie.
    resetPreview()
    closeConstructionMenu()
    if catalogue.clear_copy then
        local cleared, reply=pcall(function() return Stamp:GetStampIconGUID(catalogue.clear_copy) end)
        if not cleared or reply~=catalogue.clear_copy then
            log("création annulée : ancienne copie non vidée")
            return false
        end
        captureEmptyConfirmed=true
    end
    local ok, started = pcall(function() return Target:StartTargetMode(7) end) -- Copy
    if not ok or started == false then
        log("outil de création indisponible : " .. tostring(started))
        return false
    end
    local checked, active = pcall(function() return Stamp:GetStampIconGUID(catalogue.copy_query) end)
    if not checked or active ~= 1 then
        log("outil de copie non confirmé par la DLL")
        return false
    end
    captureArmed = true
    if management then pcall(function() Stamp:GetStampIconGUID(management.capture_on) end) end
    log("création armée : trace une zone sur la carte ; enregistrement au relâchement")
    return true
end

function UI:PollCreate()
    local ok, hasCopy, count, dragging = pcall(function()
        local mode = BuildMode:get()
        return mode.HasPipetteBuildings, mode.PipetteBuildingsCount, Target:get().IsDragActive
    end)
    if not ok or type(dragging) ~= "boolean" then
        captureArmed = false
        log("création annulée : sélection indisponible")
        return
    end
    if dragging then captureSawDrag=true; return end
    -- StartTargetMode ne vide pas nécessairement la copie précédente. Elle ne
    -- constitue jamais une nouvelle sélection : attendre un nouveau tracé.
    if not captureSawDrag and not captureEmptyConfirmed then
        local checked, active = pcall(function() return Stamp:GetStampIconGUID(catalogue.copy_query) end)
        if checked and active~=1 then captureArmed=false; log("création annulée : outil de copie quitté") end
        return
    end
    if not hasCopy or type(count) ~= "number" or count <= 0 then
        local checked, active = pcall(function() return Stamp:GetStampIconGUID(catalogue.copy_query) end)
        if not checked or active ~= 1 then
            captureArmed = false
            log("création annulée : outil de copie quitté")
        end
        return
    end
    -- Aucun menu n'est ouvert ici. Conserver intégralement la copie du joueur.
    captureArmed = false
    captureSawDrag = false
    local command = catalogue.capture_command
    local prepared, answer = pcall(function() return Stamp:GetStampIconGUID(command) end)
    if not prepared or answer ~= command then
        log("enregistrement indisponible : pont DLL absent")
        return
    end
    local saved, err = pcall(function() Stamp:CreateStamp() end)
    local confirmed, result = pcall(function() return Stamp:GetStampIconGUID(-command) end)
    if not saved or not confirmed or result ~= command then
        log("enregistrement non confirmé : " .. tostring(saved and result or err))
        return
    end
    log("stamp enregistré depuis la sélection : " .. tostring(count) .. " élément(s)")
    resetPreview()
    if live then live:Refresh(true) end
end

function UI:Tick()
    ensureVisible()
    if management and visibilityReady then
        local ok,folder,load,opened=pcall(function()
            local destination=Stamp:GetStampIconGUID(management.poll)
            return destination,Stamp:GetStampIconGUID(management.load),Stamp:GetStampIconGUID(management.is_open)==1
        end)
        if ok then
            if live and managementOpen and not opened then live:Refresh(true) end
            managementOpen=opened
            if folder and folder>0 then
                if live then live:Refresh(true) end
                self:StartCreate(folder)
                return
            end
            if load and load>0 then self:Load(load); return end
            if opened then
                if editor then editor:Tick() end
                return
            elseif editor then editor:Reset() end
        end
    end
    if captureArmed then
        self:PollCreate()
        if management and not captureArmed then pcall(function() Stamp:GetStampIconGUID(management.capture_off) end) end
        return
    end
    if live and visibilityReady then live:Tick() end
    if management and live and visibilityReady then
        local hovered=live:HoveredGuid()
        pcall(function() Stamp:GetStampIconGUID(hovered and management.hover_base+hovered-catalogue.live.pool_base or management.hover_clear) end)
    end
    local ok, guid = pcall(function() return BuildMode:get().BuildMode.Object.GUID end)
    local managedAction = management and type(guid)=="number" and
        ((guid>=management.action_base and guid<management.action_base+64) or
         (guid>=management.root_manage and guid<management.root_manage+2))
    if not ok or not (entries[guid] or createButtons[guid] or managedAction) then
        lastGuid = nil
        return
    end
    if guid == lastGuid then return end
    lastGuid = guid
    if management and (createButtons[guid] or managedAction) then
        resetPreview()
        closeConstructionMenu()
        local command = createButtons[guid] and management.choose_create+management.create_regions[guid] or guid
        local accepted,answer=pcall(function() return Stamp:GetStampIconGUID(command) end)
        if not accepted or answer~=1 then log("gestion native indisponible : "..tostring(answer)) end
    elseif createButtons[guid] then self:StartCreate() else self:Load(guid) end
end

function UI:Init()
    local count = 0
    for _ in pairs(entries) do count = count + 1 end
    log("module chargé : " .. tostring(count) .. " icône(s) de stamps")
end

function UI:OnLoad()
    lastGuid = nil
    captureArmed = false
    captureSawDrag = false
    managementOpen=false
    if editor then editor:Reset() end
    if management then pcall(function() Stamp:GetStampIconGUID(management.capture_off) end) end
    visibilityReady, visibilityRetry, visibilityError = false, 0, nil
    if live then live:OnLoad() end
    self:Init()
end

return UI
