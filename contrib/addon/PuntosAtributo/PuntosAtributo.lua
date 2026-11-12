-- Puntos de Atributo: boton en el minimapa + ventana para repartir puntos.
-- Habla con el servidor mediante el comando ".atributos" (datos / asignar / reiniciar).

local _, PA = ...

local NOMBRES = { "Fuerza", "Agilidad", "Aguante", "Intelecto", "Espiritu", "Armadura", "Celeridad", "Critico", "Maestria" }
local ICONOS = {
    "Interface\\Icons\\Spell_Nature_Strength",
    "Interface\\Icons\\Ability_Hunter_SilentHunter",
    "Interface\\Icons\\Spell_Holy_WordFortitude",
    "Interface\\Icons\\Spell_Holy_MagicalSentry",
    "Interface\\Icons\\Spell_Holy_DivineSpirit",
    "Interface\\Icons\\INV_Chest_Plate16",
    "Interface\\Icons\\Ability_Rogue_Sprint",
    "Interface\\Icons\\Ability_CriticalStrike",
    "Interface\\Icons\\Spell_Holy_ChampionsBond",
}
local NUM = #NOMBRES

local datos = nil
local esperandoRespuesta = false

-- ------------------------------------------------------------
-- Utilidades
-- ------------------------------------------------------------
local function Enviar(orden)
    local cmd = ".atributos"
    if orden and orden ~= "" then cmd = cmd .. " " .. orden end
    SendChatMessage(cmd, "SAY")
end

local Despues = PA.Despues

local function Parsear(msg)
    local p = {}
    for v in string.gmatch(msg, "[^#]+") do p[#p + 1] = v end
    if p[1] ~= "ATRIB" then return nil end
    local d = { nivel = tonumber(p[3]) or 0, libres = tonumber(p[4]) or 0, total = tonumber(p[5]) or 0, stats = {} }
    for i = 1, NUM do
        local b = 5 + (i - 1) * 3
        d.stats[i] = {
            gastado  = tonumber(p[b + 1]) or 0,
            aplicado = tonumber(p[b + 2]) or 0,
            valor    = tonumber(p[b + 3]) or 0,
        }
    end
    return d
end

-- ------------------------------------------------------------
-- Ventana principal
-- ------------------------------------------------------------
local f = CreateFrame("Frame", "PuntosAtributoFrame", UIParent)
f:SetSize(470, 400)
f:SetPoint("CENTER")
f:SetFrameStrata("DIALOG")
f:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 },
})
f:EnableMouse(true)
f:SetMovable(true)
f:SetClampedToScreen(true)
f:RegisterForDrag("LeftButton")
f:SetScript("OnDragStart", f.StartMoving)
f:SetScript("OnDragStop", f.StopMovingOrSizing)
f:Hide()
tinsert(UISpecialFrames, "PuntosAtributoFrame")

local titulo = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
titulo:SetPoint("TOP", 0, -20)
titulo:SetText("Puntos de Atributo")

local cerrar = CreateFrame("Button", nil, f, "UIPanelCloseButton")
cerrar:SetPoint("TOPRIGHT", -6, -6)

local info = f:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
info:SetPoint("TOP", 0, -46)
info:SetText("Cargando datos del servidor...")

local filas = {}
local CANTIDADES = { { "+1", 1, 34 }, { "+5", 5, 34 }, { "+10", 10, 40 }, { "Todo", 9999, 48 } }

for i = 1, NUM do
    local y = -72 - (i - 1) * 30
    local fila = {}

    local icono = f:CreateTexture(nil, "ARTWORK")
    icono:SetSize(24, 24)
    icono:SetPoint("TOPLEFT", 24, y)
    icono:SetTexture(ICONOS[i])

    fila.nombre = f:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    fila.nombre:SetPoint("LEFT", icono, "RIGHT", 8, 0)
    fila.nombre:SetText(NOMBRES[i])

    fila.valor = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    fila.valor:SetPoint("LEFT", icono, "RIGHT", 90, 0)
    fila.valor:SetText("-")

    fila.botones = {}
    local x = -24
    for j = #CANTIDADES, 1, -1 do
        local c = CANTIDADES[j]
        local b = CreateFrame("Button", nil, f, "UIPanelButtonTemplate")
        b:SetSize(c[3], 22)
        b:SetPoint("TOPRIGHT", x, y + 1)
        b:SetText(c[1])
        b.cantidad = c[2]
        b:SetScript("OnClick", function(self)
            if esperandoRespuesta then return end
            esperandoRespuesta = true
            Enviar("asignar " .. (i - 1) .. " " .. self.cantidad)
            Despues(3, function() esperandoRespuesta = false end)
        end)
        fila.botones[j] = b
        x = x - c[3] - 3
    end

    filas[i] = fila
end

StaticPopupDialogs["PUNTOSATRIB_REINICIAR"] = {
    text = "Se devolveran todos tus puntos de atributo para repartirlos de nuevo.\n\nContinuar?",
    button1 = ACCEPT,
    button2 = CANCEL,
    OnAccept = function() Enviar("reiniciar") end,
    timeout = 0,
    whileDead = true,
    hideOnEscape = true,
}

local reiniciar = CreateFrame("Button", nil, f, "UIPanelButtonTemplate")
reiniciar:SetSize(150, 24)
reiniciar:SetPoint("BOTTOMLEFT", 24, 22)
reiniciar:SetText("Reiniciar puntos")
reiniciar:SetScript("OnClick", function() StaticPopup_Show("PUNTOSATRIB_REINICIAR") end)

local maestro = CreateFrame("Button", nil, f, "UIPanelButtonTemplate")
maestro:SetSize(150, 24)
maestro:SetPoint("BOTTOMRIGHT", -24, 22)
maestro:SetText("Convocar Maestro")
maestro:SetScript("OnClick", function() Enviar("") end)

local actualizar = CreateFrame("Button", nil, f, "UIPanelButtonTemplate")
actualizar:SetSize(90, 24)
actualizar:SetPoint("BOTTOM", 0, 22)
actualizar:SetText("Actualizar")
actualizar:SetScript("OnClick", function() Enviar("datos") end)

-- ------------------------------------------------------------
-- Boton del minimapa
-- ------------------------------------------------------------
local mb = CreateFrame("Button", "PuntosAtributoMinimapButton", Minimap)
mb:SetSize(31, 31)
mb:SetFrameStrata("MEDIUM")
mb:SetFrameLevel(8)
mb:SetHighlightTexture("Interface\\Minimap\\UI-Minimap-ZoomButton-Highlight")
mb:RegisterForClicks("LeftButtonUp", "RightButtonUp")
mb:RegisterForDrag("LeftButton")

local mbFondo = mb:CreateTexture(nil, "BACKGROUND")
mbFondo:SetSize(20, 20)
mbFondo:SetTexture("Interface\\Minimap\\UI-Minimap-Background")
mbFondo:SetPoint("TOPLEFT", 7, -5)

local mbIcono = mb:CreateTexture(nil, "ARTWORK")
mbIcono:SetSize(18, 18)
mbIcono:SetTexture("Interface\\Icons\\Spell_Holy_GreaterBlessingofKings")
mbIcono:SetPoint("TOPLEFT", 7, -6)

local mbBorde = mb:CreateTexture(nil, "OVERLAY")
mbBorde:SetSize(53, 53)
mbBorde:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")
mbBorde:SetPoint("TOPLEFT")

local mbNumero = mb:CreateFontString(nil, "OVERLAY", "NumberFontNormal")
mbNumero:SetPoint("BOTTOMRIGHT", mb, "BOTTOMRIGHT", 0, 2)
mbNumero:SetTextColor(0.2, 1, 0.2)

-- Cartas por elegir (numero dorado arriba)
local mbCartas = mb:CreateFontString(nil, "OVERLAY", "NumberFontNormal")
mbCartas:SetPoint("TOPRIGHT", mb, "TOPRIGHT", 2, -1)
mbCartas:SetTextColor(1, 0.82, 0)
function PA.MostrarCartasPendientes(n)
    mbCartas:SetText((n and n > 0) and tostring(n) or "")
    PA.cartasPendientes = n or 0
end

local function PosicionarBoton()
    local angulo = math.rad((PuntosAtributoDB and PuntosAtributoDB.angulo) or 200)
    mb:ClearAllPoints()
    mb:SetPoint("CENTER", Minimap, "CENTER", math.cos(angulo) * 80, math.sin(angulo) * 80)
end

mb:SetScript("OnDragStart", function(self)
    self:SetScript("OnUpdate", function()
        local mx, my = Minimap:GetCenter()
        local px, py = GetCursorPosition()
        local escala = Minimap:GetEffectiveScale()
        px, py = px / escala, py / escala
        PuntosAtributoDB.angulo = math.deg(math.atan2(py - my, px - mx))
        PosicionarBoton()
    end)
end)
mb:SetScript("OnDragStop", function(self) self:SetScript("OnUpdate", nil) end)

local function Alternar()
    if f:IsShown() then
        f:Hide()
    else
        f:Show()
        Enviar("datos")
    end
end

mb:SetScript("OnClick", function(self, boton)
    if boton == "RightButton" then
        Alternar()             -- clic derecho: ventana de atributos
    elseif PA.AlternarPanel then
        PA.AlternarPanel()     -- clic: panel del servidor
    else
        Alternar()
    end
end)
PA.AlternarAtributos = Alternar

mb:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_LEFT")
    GameTooltip:AddLine("Servidor de Luis")
    if datos then
        GameTooltip:AddLine("Puntos de atributo libres: |cff33ff33" .. datos.libres .. "|r de " .. datos.total, 1, 1, 1)
    end
    if PA.cartasPendientes and PA.cartasPendientes > 0 then
        GameTooltip:AddLine("Cartas por elegir: |cffffd100" .. PA.cartasPendientes .. "|r", 1, 1, 1)
    end
    GameTooltip:AddLine("Clic: panel (atributos, cartas, montura, buzon, subasta, guia)", 0.8, 0.8, 0.8)
    GameTooltip:AddLine("Clic derecho: puntos de atributo", 0.8, 0.8, 0.8)
    GameTooltip:AddLine("Arrastrar: mover el boton", 0.8, 0.8, 0.8)
    GameTooltip:Show()
end)
mb:SetScript("OnLeave", function() GameTooltip:Hide() end)

-- ------------------------------------------------------------
-- Refrescar la ventana con los datos del servidor
-- ------------------------------------------------------------
local function Refrescar()
    if not datos then return end
    info:SetText(string.format("Nivel %d   -   Puntos libres: |cff33ff33%d|r de %d", datos.nivel, datos.libres, datos.total))
    mbNumero:SetText(datos.libres > 0 and tostring(datos.libres) or "")
    for i = 1, NUM do
        local s = datos.stats[i]
        filas[i].valor:SetText(string.format("%d pts  =  |cff33ff33+%d|r   (cada punto +%d)", s.gastado, s.aplicado, s.valor))
        for _, b in pairs(filas[i].botones) do
            local necesita = (b.cantidad == 9999) and 1 or b.cantidad
            if datos.libres >= necesita then b:Enable() else b:Disable() end
        end
    end
end

-- ------------------------------------------------------------
-- Eventos
-- ------------------------------------------------------------
local eventos = CreateFrame("Frame")
eventos:RegisterEvent("ADDON_LOADED")
eventos:RegisterEvent("PLAYER_ENTERING_WORLD")
eventos:RegisterEvent("CHAT_MSG_SYSTEM")
eventos:SetScript("OnEvent", function(self, evento, a1)
    if evento == "ADDON_LOADED" and a1 == "PuntosAtributo" then
        PuntosAtributoDB = PuntosAtributoDB or {}
        PosicionarBoton()
    elseif evento == "PLAYER_ENTERING_WORLD" then
        Despues(4, function() Enviar("datos") end)
    elseif evento == "CHAT_MSG_SYSTEM" then
        if type(a1) == "string" and string.sub(a1, 1, 6) == "ATRIB#" then
            local d = Parsear(a1)
            if d then
                datos = d
                esperandoRespuesta = false
                Refrescar()
            end
        end
    end
end)

SLASH_PUNTOSATRIBUTO1 = "/puntos"
SlashCmdList["PUNTOSATRIBUTO"] = Alternar
