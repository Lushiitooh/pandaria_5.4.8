-- Guia de niveles: muestra la ruta de zonas segun tu faccion y nivel, y pone una
-- flecha (TomTom) hacia el campamento de la zona que te toca.
-- Para el detalle mision por mision usa WoWPro (/wp).

local _, G = ...

local ventana, filas, scrollHijo
local pasoActual = nil      -- paso recomendado ahora
local uidFlecha = nil       -- waypoint de TomTom puesto por la guia

local function Aviso(texto)
    DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[Guía]|r " .. texto)
end

-- Lista completa de pasos para este personaje
local function Pasos()
    local lista = {}
    local _, raza = UnitRace("player")
    local inicio = G.INICIO[raza or ""]
    if inicio then lista[#lista + 1] = inicio end
    local faccion = UnitFactionGroup("player")
    for _, p in ipairs(G.RUTA[faccion] or {}) do lista[#lista + 1] = p end
    return lista
end

-- Paso recomendado: el primero cuyo rango incluya el nivel
local function Recomendado(nivel)
    local lista = Pasos()
    for _, p in ipairs(lista) do
        if nivel >= p[1] and nivel < p[2] then return p end
    end
    for _, p in ipairs(lista) do
        if nivel >= p[1] and nivel <= p[2] then return p end
    end
    return lista[#lista]
end

local function QuitarFlecha()
    if uidFlecha and TomTom and TomTom.RemoveWaypoint then
        pcall(TomTom.RemoveWaypoint, TomTom, uidFlecha)
    end
    uidFlecha = nil
end

local function PonerFlecha(p)
    if not p then return end
    if not p[4] then
        Aviso(string.format("|cff33ff33%s|r: %s", p[6], p[8] ~= "" and p[8] or "sigue las misiones de la zona."))
        return
    end
    if TomTom and TomTom.AddMFWaypoint then
        QuitarFlecha()
        local ok, uid = pcall(TomTom.AddMFWaypoint, TomTom, p[3], nil, p[4] / 100, p[5] / 100,
            { title = p[7] .. " (" .. p[6] .. ")", crazy = true, persistent = false, silent = true })
        if ok then
            uidFlecha = uid
            Aviso(string.format("Flecha hacia |cff33ff33%s|r en %s (%.0f, %.0f).", p[7], p[6], p[4], p[5]))
            return
        end
    end
    Aviso(string.format("Ve a |cff33ff33%s|r en %s, coordenadas %.0f, %.0f. (Instala TomTom para ver la flecha.)", p[7], p[6], p[4], p[5]))
end

-- ------------------------------------------------------------
-- Ventana
-- ------------------------------------------------------------
local function Refrescar()
    if not ventana then return end
    local nivel = UnitLevel("player")
    local lista = Pasos()
    pasoActual = Recomendado(nivel)

    ventana.cabecera:SetText(string.format("Nivel %d  -  Te toca: |cff33ff33%s|r", nivel, pasoActual and pasoActual[6] or "?"))
    ventana.detalle:SetText(pasoActual and (pasoActual[7] .. ((pasoActual[8] ~= "") and ("  -  " .. pasoActual[8]) or "")) or "")

    for i, p in ipairs(lista) do
        local fila = filas[i]
        if not fila then
            fila = CreateFrame("Button", nil, scrollHijo)
            fila:SetSize(390, 20)
            fila:SetPoint("TOPLEFT", 0, -(i - 1) * 21)
            fila:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
            fila.fondo = fila:CreateTexture(nil, "BACKGROUND")
            fila.fondo:SetAllPoints()
            fila.niveles = fila:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
            fila.niveles:SetPoint("LEFT", 4, 0)
            fila.niveles:SetWidth(50)
            fila.niveles:SetJustifyH("LEFT")
            fila.zona = fila:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
            fila.zona:SetPoint("LEFT", 56, 0)
            fila.zona:SetWidth(330)
            fila.zona:SetJustifyH("LEFT")
            fila:SetScript("OnClick", function(self) PonerFlecha(self.paso) end)
            fila:SetScript("OnEnter", function(self)
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                GameTooltip:AddLine(self.paso[6])
                GameTooltip:AddLine(self.paso[7], 1, 1, 1)
                if self.paso[8] ~= "" then GameTooltip:AddLine(self.paso[8], 0.8, 0.8, 0.8, true) end
                GameTooltip:AddLine("Clic: poner la flecha aquí", 0.5, 1, 0.5)
                GameTooltip:Show()
            end)
            fila:SetScript("OnLeave", function() GameTooltip:Hide() end)
            filas[i] = fila
        end
        fila.paso = p
        fila.niveles:SetText(p[1] .. "-" .. p[2])
        fila.zona:SetText(p[6] .. "  |cffaaaaaa(" .. p[7] .. ")|r")
        if p == pasoActual then
            fila.fondo:SetTexture(0.1, 0.6, 0.1, 0.35)
        elseif nivel >= p[2] then
            fila.fondo:SetTexture(0, 0, 0, 0)
            fila.zona:SetText("|cff777777" .. p[6] .. "  (" .. p[7] .. ")|r")
        else
            fila.fondo:SetTexture(0, 0, 0, 0)
        end
        fila:Show()
    end
    for i = #lista + 1, #filas do filas[i]:Hide() end
    scrollHijo:SetHeight(#lista * 21 + 4)
end

local function CrearVentana()
    ventana = CreateFrame("Frame", "GuiaNivelesFrame", UIParent)
    ventana:SetSize(460, 500)
    ventana:SetPoint("CENTER", 120, 0)
    ventana:SetFrameStrata("DIALOG")
    ventana:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
        tile = true, tileSize = 32, edgeSize = 32,
        insets = { left = 11, right = 12, top = 12, bottom = 11 },
    })
    ventana:EnableMouse(true)
    ventana:SetMovable(true)
    ventana:SetClampedToScreen(true)
    ventana:RegisterForDrag("LeftButton")
    ventana:SetScript("OnDragStart", ventana.StartMoving)
    ventana:SetScript("OnDragStop", ventana.StopMovingOrSizing)
    ventana:Hide()
    tinsert(UISpecialFrames, "GuiaNivelesFrame")

    local titulo = ventana:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    titulo:SetPoint("TOP", 0, -20)
    titulo:SetText("Guía de niveles")
    local cerrar = CreateFrame("Button", nil, ventana, "UIPanelCloseButton")
    cerrar:SetPoint("TOPRIGHT", -6, -6)

    ventana.cabecera = ventana:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    ventana.cabecera:SetPoint("TOP", 0, -46)
    ventana.detalle = ventana:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    ventana.detalle:SetPoint("TOP", 0, -64)
    ventana.detalle:SetWidth(410)

    local scroll = CreateFrame("ScrollFrame", "GuiaNivelesScroll", ventana, "UIPanelScrollFrameTemplate")
    scroll:SetPoint("TOPLEFT", 22, -88)
    scroll:SetPoint("BOTTOMRIGHT", -40, 84)
    scrollHijo = CreateFrame("Frame", nil, scroll)
    scrollHijo:SetSize(390, 10)
    scroll:SetScrollChild(scrollHijo)
    filas = {}

    local ir = CreateFrame("Button", nil, ventana, "UIPanelButtonTemplate")
    ir:SetSize(200, 26)
    ir:SetPoint("BOTTOMLEFT", 22, 48)
    ir:SetText("Flecha a mi zona")
    ir:SetScript("OnClick", function() PonerFlecha(Recomendado(UnitLevel("player"))) end)

    local quitar = CreateFrame("Button", nil, ventana, "UIPanelButtonTemplate")
    quitar:SetSize(130, 26)
    quitar:SetPoint("LEFT", ir, "RIGHT", 6, 0)
    quitar:SetText("Quitar flecha")
    quitar:SetScript("OnClick", QuitarFlecha)

    local avisar = CreateFrame("CheckButton", nil, ventana, "UICheckButtonTemplate")
    avisar:SetSize(22, 22)
    avisar:SetPoint("BOTTOMLEFT", 22, 22)
    local t = avisar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    t:SetPoint("LEFT", avisar, "RIGHT", 2, 0)
    t:SetText("Avisarme y poner la flecha al cambiar de zona recomendada")
    avisar:SetScript("OnClick", function(self) GuiaNivelesDB.avisar = self:GetChecked() and true or false end)
    ventana:SetScript("OnShow", function()
        avisar:SetChecked(GuiaNivelesDB.avisar ~= false)
        Refrescar()
    end)

    local wp = ventana:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    wp:SetPoint("BOTTOMRIGHT", -24, 28)
    wp:SetText("Misión por misión: /wp (WoWPro)")
end

function GuiaNiveles_Alternar()
    if not ventana then CrearVentana() end
    if ventana:IsShown() then ventana:Hide() else ventana:Show() end
end

-- ------------------------------------------------------------
-- Avisos al subir de nivel
-- ------------------------------------------------------------
local function Revisar(nivel, alEntrar)
    local p = Recomendado(nivel)
    if not p then return end
    local clave = p[6] .. p[1]
    if GuiaNivelesDB.ultimo == clave and not alEntrar then return end
    if GuiaNivelesDB.ultimo ~= clave then
        GuiaNivelesDB.ultimo = clave
        if GuiaNivelesDB.avisar ~= false then
            Aviso(string.format("Con nivel %d te toca |cff33ff33%s|r (niveles %d-%d).", nivel, p[6], p[1], p[2]))
            PonerFlecha(p)
        end
    end
    if ventana and ventana:IsShown() then Refrescar() end
end

local ev = CreateFrame("Frame")
ev:RegisterEvent("ADDON_LOADED")
ev:RegisterEvent("PLAYER_LEVEL_UP")
ev:RegisterEvent("PLAYER_ENTERING_WORLD")
ev:SetScript("OnEvent", function(self, evento, a1)
    if evento == "ADDON_LOADED" and a1 == "GuiaNiveles" then
        GuiaNivelesDB = GuiaNivelesDB or {}
    elseif evento == "PLAYER_LEVEL_UP" then
        Revisar(tonumber(a1) or UnitLevel("player"), false)
    elseif evento == "PLAYER_ENTERING_WORLD" then
        self:UnregisterEvent("PLAYER_ENTERING_WORLD")
        local t = CreateFrame("Frame")
        local espera = 7
        t:SetScript("OnUpdate", function(s, e)
            espera = espera - e
            if espera <= 0 then
                s:SetScript("OnUpdate", nil)
                Revisar(UnitLevel("player"), true)
            end
        end)
    end
end)

SLASH_GUIANIVELES1 = "/guia"
SlashCmdList["GUIANIVELES"] = GuiaNiveles_Alternar
