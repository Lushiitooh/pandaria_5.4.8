-- Nucleo compartido del addon del servidor de Luis.
-- Funciones comunes para todos los modulos (atributos, cartas, montura, panel).

local _, PA = ...
PuntosAtributo = PA   -- acceso global (atajos de teclado, macros)

-- Envia una orden al servidor (".orden argumentos")
function PA.Orden(texto)
    SendChatMessage("." .. texto, "SAY")
end

-- Temporizadores multiples: PA.Despues(segundos, funcion)
local pendientes = {}
local reloj = CreateFrame("Frame")
reloj:Hide()
reloj:SetScript("OnUpdate", function(self, transcurrido)
    local quedan = false
    for i = #pendientes, 1, -1 do
        local t = pendientes[i]
        t.resta = t.resta - transcurrido
        if t.resta <= 0 then
            table.remove(pendientes, i)
            local ok, err = pcall(t.funcion)
            if not ok then geterrorhandler()(err) end
        else
            quedan = true
        end
    end
    if not quedan and #pendientes == 0 then self:Hide() end
end)

function PA.Despues(segundos, funcion)
    pendientes[#pendientes + 1] = { resta = segundos, funcion = funcion }
    reloj:Show()
end

-- Separa un texto por un caracter
function PA.Separar(texto, sep)
    local partes = {}
    for v in string.gmatch(texto, "([^" .. sep .. "]+)") do partes[#partes + 1] = v end
    return partes
end

-- Mensaje del addon en el chat
function PA.Aviso(texto)
    DEFAULT_CHAT_FRAME:AddMessage("|cff00ccff[Servidor de Luis]|r " .. texto)
end

-- Estilo comun de ventana
function PA.CrearVentana(nombre, ancho, alto, tituloTexto)
    local f = CreateFrame("Frame", nombre, UIParent)
    f:SetSize(ancho, alto)
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
    tinsert(UISpecialFrames, nombre)

    f.titulo = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    f.titulo:SetPoint("TOP", 0, -20)
    f.titulo:SetText(tituloTexto or "")

    local cerrar = CreateFrame("Button", nil, f, "UIPanelCloseButton")
    cerrar:SetPoint("TOPRIGHT", -6, -6)
    return f
end

function PA.Boton(padre, texto, ancho, alto, alPulsar)
    local b = CreateFrame("Button", nil, padre, "UIPanelButtonTemplate")
    b:SetSize(ancho, alto or 24)
    b:SetText(texto)
    b:SetScript("OnClick", alPulsar)
    return b
end

-- Lineas de datos del servidor que no deben verse en el chat
local PREFIJOS = { "ATRIB#", "CARTAS#", "CARTASL#" }
ChatFrame_AddMessageEventFilter("CHAT_MSG_SYSTEM", function(self, evento, msg, ...)
    if type(msg) == "string" then
        for _, p in ipairs(PREFIJOS) do
            if string.sub(msg, 1, #p) == p then return true end
        end
    end
    return false
end)

-- Distribuidor de mensajes del servidor: PA.AlRecibir("PREFIJO#", funcion(msg))
local oyentes = {}
function PA.AlRecibir(prefijo, funcion)
    oyentes[#oyentes + 1] = { prefijo = prefijo, funcion = funcion }
end

local eventos = CreateFrame("Frame")
eventos:RegisterEvent("CHAT_MSG_SYSTEM")
eventos:SetScript("OnEvent", function(self, evento, msg)
    if type(msg) ~= "string" then return end
    for _, o in ipairs(oyentes) do
        if string.sub(msg, 1, #o.prefijo) == o.prefijo then
            o.funcion(msg)
            return
        end
    end
end)

-- Textos de los atajos de teclado (Menu > Asignacion de teclas)
BINDING_HEADER_PUNTOSATRIBUTO = "Servidor de Luis"
BINDING_NAME_PUNTOSATRIBUTO_MONTAR = "Montar / desmontar"
BINDING_NAME_PUNTOSATRIBUTO_PANEL = "Abrir el panel del servidor"
BINDING_NAME_PUNTOSATRIBUTO_CARTAS = "Abrir las cartas"
BINDING_NAME_PUNTOSATRIBUTO_GUIA = "Abrir la guia de niveles"
