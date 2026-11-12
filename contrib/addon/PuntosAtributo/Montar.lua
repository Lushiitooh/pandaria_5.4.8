-- Montar / desmontar con un boton o una tecla.
-- En este servidor la montura se aprende antes del nivel 20 y el juego la muestra
-- en gris, pero llamarla desde aqui funciona igual que desde el menu de monturas.

local _, PA = ...

local NOMBRE_MACRO = "Montar"
local ICONO_MACRO = "Ability_Mount_RidingHorse"

local function SabeVolar()
    return IsSpellKnown(34090) or IsSpellKnown(34091) or IsSpellKnown(90265)
end

-- mountType (6o valor de GetCompanionInfo): 0x1 tierra, 0x2 vuelo, 0x8 bajo el agua
local function ListaMonturas()
    local lista = {}
    for i = 1, GetNumCompanions("MOUNT") do
        local _, nombre, hechizo, icono, invocada, tipo = GetCompanionInfo("MOUNT", i)
        lista[#lista + 1] = { indice = i, nombre = nombre, hechizo = hechizo, icono = icono, tipo = tipo or 1 }
    end
    return lista
end

local function Elegir()
    local lista = ListaMonturas()
    if #lista == 0 then return nil end

    local volar = SabeVolar() and IsFlyableArea()
    local fav = PuntosAtributoDB and PuntosAtributoDB.montura

    -- Favorita, si sirve aqui
    if fav then
        for _, m in ipairs(lista) do
            if m.hechizo == fav and (bit.band(m.tipo, 0x1) > 0 or (volar and bit.band(m.tipo, 0x2) > 0)) then
                return m
            end
        end
    end

    -- Si se puede volar, una voladora al azar; si no, una terrestre al azar
    local candidatas = {}
    if volar then
        for _, m in ipairs(lista) do
            if bit.band(m.tipo, 0x2) > 0 then candidatas[#candidatas + 1] = m end
        end
    end
    if #candidatas == 0 then
        for _, m in ipairs(lista) do
            if bit.band(m.tipo, 0x1) > 0 then candidatas[#candidatas + 1] = m end
        end
    end
    if #candidatas == 0 then candidatas = lista end
    return candidatas[math.random(#candidatas)]
end

function PuntosAtributo_Montar()
    if IsMounted() then
        Dismount()
        return
    end
    if CanExitVehicle and CanExitVehicle() then
        VehicleExit()
        return
    end
    if UnitAffectingCombat("player") then
        UIErrorsFrame:AddMessage("No puedes montar en combate.", 1, 0.1, 0.1)
        return
    end
    local m = Elegir()
    if not m then
        PA.Aviso("Todavía no tienes monturas.")
        return
    end
    CallCompanion("MOUNT", m.indice)
end
PA.Montar = PuntosAtributo_Montar

-- Guarda la montura que llevas puesta como favorita
function PA.GuardarFavorita()
    for i = 1, GetNumCompanions("MOUNT") do
        local _, nombre, hechizo, _, invocada = GetCompanionInfo("MOUNT", i)
        if invocada then
            PuntosAtributoDB.montura = hechizo
            PA.Aviso("Montura favorita: |cff33ff33" .. nombre .. "|r.")
            return
        end
    end
    PuntosAtributoDB.montura = nil
    PA.Aviso("No vas montado: la montura será al azar. (Monta la que quieras y vuelve a pulsar para fijarla.)")
end

-- Crea la macro "Montar" y la pone en la barra de acción
local function CrearMacro()
    if InCombatLockdown() then return nil end
    local indice = GetMacroIndexByName(NOMBRE_MACRO)
    if indice == 0 then
        local _, porPersonaje = GetNumMacros()
        if porPersonaje >= (MAX_CHARACTER_MACROS or 18) then
            PA.Aviso("No queda espacio para otra macro de personaje.")
            return nil
        end
        indice = CreateMacro(NOMBRE_MACRO, ICONO_MACRO, "/run PuntosAtributo_Montar()", 1)
    end
    return indice
end

local function BuscarHueco()
    -- Barra principal, luego barras inferior izquierda/derecha y laterales
    local rangos = { { 1, 12 }, { 61, 72 }, { 49, 60 }, { 25, 36 }, { 37, 48 } }
    for _, r in ipairs(rangos) do
        for slot = r[1], r[2] do
            if not HasAction(slot) then return slot end
        end
    end
    return nil
end

local function YaEnBarra(indice)
    for slot = 1, 120 do
        local tipo, id = GetActionInfo(slot)
        if tipo == "macro" and id == indice then return true end
    end
    return false
end

function PA.PonerMontarEnBarra(silencioso)
    local indice = CrearMacro()
    if not indice or indice == 0 then return end
    if YaEnBarra(indice) then
        if not silencioso then PA.Aviso("El botón |cff33ff33Montar|r ya está en tu barra de acción.") end
        return
    end
    local slot = BuscarHueco()
    if slot then
        PickupMacro(indice)
        PlaceAction(slot)
        ClearCursor()
        PA.Aviso("Puse el botón |cff33ff33Montar|r en tu barra de acción. Úsalo para montar y desmontar.")
    else
        PickupMacro(indice)
        PA.Aviso("Tu barra está llena: arrastra el botón |cff33ff33Montar|r (en el cursor) a donde quieras.")
    end
end

-- La primera vez de cada personaje: macro en la barra
local ev = CreateFrame("Frame")
ev:RegisterEvent("PLAYER_ENTERING_WORLD")
ev:SetScript("OnEvent", function(self)
    self:UnregisterEvent("PLAYER_ENTERING_WORLD")
    PA.Despues(8, function()
        if PuntosAtributoDB and not PuntosAtributoDB.macroMontar and GetNumCompanions("MOUNT") > 0 then
            PuntosAtributoDB.macroMontar = true
            PA.PonerMontarEnBarra(true)
        end
    end)
end)

SLASH_PAMONTAR1 = "/montar"
SlashCmdList["PAMONTAR"] = PuntosAtributo_Montar
