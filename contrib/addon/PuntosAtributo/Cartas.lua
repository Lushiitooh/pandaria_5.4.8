-- Cartas al subir de nivel: ventana con 3 cartas para elegir una.
-- Habla con el servidor mediante ".cartas" (datos / elegir N / rehacer / listaaddon).

local _, PA = ...

local RAREZA = {
    [1] = { nombre = "Común", color = { 0.9, 0.9, 0.9 }, hex = "ffffffff", icono = "Interface\\Icons\\INV_Misc_Ticket_Tarot_Stack_01" },
    [2] = { nombre = "Rara",  color = { 0.0, 0.44, 0.87 }, hex = "ff0070dd", icono = "Interface\\Icons\\INV_Misc_Ticket_Tarot_Furies" },
    [3] = { nombre = "Épica", color = { 0.64, 0.21, 0.93 }, hex = "ffa335ee", icono = "Interface\\Icons\\INV_Misc_Ticket_Tarot_Blessings" },
}

local datos = nil
local esperando = false
local abrirAlSalirDeCombate = false

-- ------------------------------------------------------------
-- Ventana de eleccion
-- ------------------------------------------------------------
local f = PA.CrearVentana("PuntosAtributoCartas", 640, 400, "Elige una carta")

local subtitulo = f:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
subtitulo:SetPoint("TOP", 0, -46)

local cartas = {}
for i = 1, 3 do
    local c = CreateFrame("Frame", nil, f)
    c:SetSize(186, 260)
    c:SetPoint("TOPLEFT", 22 + (i - 1) * 200, -70)
    c:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    c:SetBackdropColor(0.05, 0.05, 0.08, 0.95)

    c.icono = c:CreateTexture(nil, "ARTWORK")
    c.icono:SetSize(64, 64)
    c.icono:SetPoint("TOP", 0, -16)

    c.rareza = c:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    c.rareza:SetPoint("TOP", c.icono, "BOTTOM", 0, -6)

    c.nombre = c:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    c.nombre:SetPoint("TOP", c.rareza, "BOTTOM", 0, -6)
    c.nombre:SetWidth(170)

    c.desc = c:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    c.desc:SetPoint("TOP", c.nombre, "BOTTOM", 0, -12)
    c.desc:SetWidth(160)
    c.desc:SetJustifyH("CENTER")

    c.boton = PA.Boton(c, "Elegir", 120, 26, function()
        if esperando then return end
        esperando = true
        for _, otra in ipairs(cartas) do otra.boton:Disable() end
        PA.Orden("cartas elegir " .. i)
        PA.Despues(4, function() esperando = false end)
    end)
    c.boton:SetPoint("BOTTOM", 0, 14)

    -- Clic en la carta = elegirla
    c:EnableMouse(true)
    c:SetScript("OnMouseUp", function() if c.boton:IsEnabled() then c.boton:Click() end end)
    c:SetScript("OnEnter", function() c:SetBackdropColor(0.15, 0.15, 0.2, 0.95) end)
    c:SetScript("OnLeave", function() c:SetBackdropColor(0.05, 0.05, 0.08, 0.95) end)
    cartas[i] = c
end

local rehacer = PA.Boton(f, "Rehacer cartas", 140, 24, function()
    if esperando then return end
    esperando = true
    PA.Orden("cartas rehacer")
    PA.Despues(4, function() esperando = false end)
end)
rehacer:SetPoint("BOTTOMLEFT", 24, 22)

local lista = PA.Boton(f, "Mis cartas", 120, 24, function() PA.AbrirListaCartas() end)
lista:SetPoint("BOTTOM", -40, 22)

local despues = PA.Boton(f, "Más tarde", 110, 24, function() f:Hide() end)
despues:SetPoint("BOTTOMRIGHT", -24, 22)

local auto = CreateFrame("CheckButton", nil, f, "UICheckButtonTemplate")
auto:SetSize(22, 22)
auto:SetPoint("BOTTOM", 50, 22)
auto.texto = auto:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
auto.texto:SetPoint("LEFT", auto, "RIGHT", 2, 0)
auto.texto:SetText("Abrir solo al subir")
auto:SetScript("OnClick", function(self)
    PuntosAtributoDB.cartasAuto = self:GetChecked() and true or false
end)
f:SetScript("OnShow", function() auto:SetChecked(PuntosAtributoDB.cartasAuto ~= false) end)

local function Refrescar()
    if not datos then return end
    if datos.pendientes == 0 then
        subtitulo:SetText(string.format("No tienes cartas por elegir. Llevas |cff33ff33%d|r cartas.", datos.elegidas))
        for _, c in ipairs(cartas) do c:Hide() end
        rehacer:Disable()
        return
    end
    local r = RAREZA[datos.rareza] or RAREZA[1]
    subtitulo:SetText(string.format("Premio del nivel %d  -  |c%s%s|r  -  Pendientes: |cffffd100%d|r",
        datos.nivel, r.hex, r.nombre, datos.pendientes))
    for i, c in ipairs(cartas) do
        local d = datos.ofertas[i]
        if d then
            local rr = RAREZA[d.rareza] or RAREZA[1]
            c.icono:SetTexture(rr.icono)
            c.rareza:SetText("|c" .. rr.hex .. rr.nombre .. "|r")
            c.nombre:SetText(d.nombre)
            c.nombre:SetTextColor(rr.color[1], rr.color[2], rr.color[3])
            c.desc:SetText(d.desc)
            c:SetBackdropBorderColor(rr.color[1], rr.color[2], rr.color[3], 1)
            c.boton:Enable()
            c:Show()
        else
            c:Hide()
        end
    end
    if datos.rehacer then rehacer:Enable() else rehacer:Disable() end
end

local function Abrir()
    f:Show()
    if datos then Refrescar() end
    PA.Orden("cartas datos")
end

function PA.AbrirCartas()
    if f:IsShown() then f:Hide() else Abrir() end
end
PuntosAtributo_Cartas = PA.AbrirCartas

-- ------------------------------------------------------------
-- Lista de cartas obtenidas
-- ------------------------------------------------------------
local fl = PA.CrearVentana("PuntosAtributoCartasLista", 480, 440, "Mis cartas")
local scroll = CreateFrame("ScrollFrame", "PuntosAtributoCartasScroll", fl, "UIPanelScrollFrameTemplate")
scroll:SetPoint("TOPLEFT", 24, -50)
scroll:SetPoint("BOTTOMRIGHT", -40, 24)
local hijo = CreateFrame("Frame", nil, scroll)
hijo:SetSize(400, 10)
scroll:SetScrollChild(hijo)
local texto = hijo:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
texto:SetPoint("TOPLEFT")
texto:SetWidth(400)
texto:SetJustifyH("LEFT")
texto:SetSpacing(3)

local recibiendo = nil
function PA.AbrirListaCartas()
    texto:SetText("Cargando...")
    fl:Show()
    PA.Orden("cartas listaaddon")
end

PA.AlRecibir("CARTASL#", function(msg)
    local cuerpo = string.sub(msg, 9)
    if cuerpo == "ini" then
        recibiendo = {}
    elseif cuerpo == "fin" then
        local lineas = {}
        if recibiendo then
            table.sort(recibiendo, function(a, b)
                if a.rareza ~= b.rareza then return a.rareza > b.rareza end
                return a.nombre < b.nombre
            end)
            for _, e in ipairs(recibiendo) do
                local r = RAREZA[e.rareza] or RAREZA[1]
                lineas[#lineas + 1] = string.format("|c%s%s|r  x%d/%d\n   |cff33ff33%s|r", r.hex, e.nombre, e.copias, e.max, e.desc)
            end
        end
        if #lineas == 0 then lineas[1] = "Todavía no tienes cartas. Sube de nivel para ganar elecciones." end
        texto:SetText(table.concat(lineas, "\n"))
        hijo:SetHeight(texto:GetStringHeight() + 10)
        recibiendo = nil
    elseif recibiendo then
        for _, item in ipairs(PA.Separar(cuerpo, "^")) do
            local p = PA.Separar(item, "~")
            recibiendo[#recibiendo + 1] = {
                nombre = p[1] or "?", rareza = tonumber(p[2]) or 1,
                copias = tonumber(p[3]) or 0, max = tonumber(p[4]) or 0, desc = p[5] or "",
            }
        end
    end
end)

-- ------------------------------------------------------------
-- Datos del servidor
-- CARTAS#1#pendientes#nivel#rareza#rehacer#elegidas#id~nombre~desc~rareza x3
-- ------------------------------------------------------------
PA.AlRecibir("CARTAS#", function(msg)
    local p = PA.Separar(msg, "#")
    local d = {
        pendientes = tonumber(p[3]) or 0, nivel = tonumber(p[4]) or 0, rareza = tonumber(p[5]) or 1,
        rehacer = p[6] == "1", elegidas = tonumber(p[7]) or 0, ofertas = {},
    }
    for i = 8, 10 do
        if p[i] then
            local c = PA.Separar(p[i], "~")
            d.ofertas[#d.ofertas + 1] = { id = tonumber(c[1]), nombre = c[2] or "?", desc = c[3] or "", rareza = tonumber(c[4]) or 1 }
        end
    end
    local antes = datos and datos.pendientes or 0
    datos = d
    esperando = false
    if PA.MostrarCartasPendientes then PA.MostrarCartasPendientes(d.pendientes) end

    if f:IsShown() then
        if d.pendientes == 0 and antes > 0 then
            f:Hide()
            PA.Aviso("Ya elegiste todas tus cartas. ¡Sigue subiendo de nivel!")
        else
            Refrescar()
        end
    elseif d.pendientes > 0 and PuntosAtributoDB and PuntosAtributoDB.cartasAuto ~= false and d.pendientes > antes then
        if UnitAffectingCombat("player") then
            abrirAlSalirDeCombate = true
        else
            Abrir()
        end
    end
end)

local ev = CreateFrame("Frame")
ev:RegisterEvent("PLAYER_REGEN_ENABLED")
ev:RegisterEvent("PLAYER_ENTERING_WORLD")
ev:SetScript("OnEvent", function(self, evento)
    if evento == "PLAYER_REGEN_ENABLED" then
        if abrirAlSalirDeCombate then
            abrirAlSalirDeCombate = false
            Abrir()
        end
    elseif evento == "PLAYER_ENTERING_WORLD" then
        PA.Despues(6, function() PA.Orden("cartas datos") end)
    end
end)

SLASH_PACARTAS1 = "/cartas"
SlashCmdList["PACARTAS"] = PA.AbrirCartas
