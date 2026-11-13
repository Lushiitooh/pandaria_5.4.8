-- Orden automatico del inventario (usa BankStack).
-- Ordena las bolsas solo, unos segundos despues de recibir objetos
-- (botin, compras, correo, comercio), cuando no estas en combate.
-- Al abrir el banco tambien lo ordena.

local _, PA = ...

local pendiente = false
local esperando = false

local function Activo()
    return PuntosAtributoDB and PuntosAtributoDB.autoOrden ~= false
end

local function PuedeOrdenar()
    if not SlashCmdList["SORT"] then return false end          -- BankStack no esta
    if InCombatLockdown() or UnitAffectingCombat("player") then return false end
    if UnitIsDeadOrGhost("player") then return false end
    if CursorHasItem() then return false end
    if (LootFrame and LootFrame:IsShown()) or (MerchantFrame and MerchantFrame:IsShown())
        or (TradeFrame and TradeFrame:IsShown()) or (AuctionFrame and AuctionFrame:IsShown())
        or (SendMailFrame and SendMailFrame:IsShown()) then
        return false
    end
    return true
end

local function Intentar()
    esperando = false
    if not pendiente or not Activo() then return end
    if not PuedeOrdenar() then
        -- reintentar mas tarde
        esperando = true
        PA.Despues(3, Intentar)
        return
    end
    pendiente = false
    SlashCmdList["SORT"]("")
end

local function Programar(segundos)
    pendiente = true
    if not esperando then
        esperando = true
        PA.Despues(segundos or 2, Intentar)
    end
end

function PA.OrdenarAhora()
    if not SlashCmdList["SORT"] then
        PA.Aviso("Falta el addon BankStack.")
        return
    end
    SlashCmdList["SORT"]("")
end

function PA.AlternarAutoOrden()
    PuntosAtributoDB.autoOrden = not Activo()
    PA.Aviso("Orden automático del inventario: " .. (Activo() and "|cff33ff33ACTIVADO|r" or "|cffff3333DESACTIVADO|r"))
end

local ev = CreateFrame("Frame")
ev:RegisterEvent("CHAT_MSG_LOOT")
ev:RegisterEvent("LOOT_CLOSED")
ev:RegisterEvent("MERCHANT_CLOSED")
ev:RegisterEvent("MAIL_CLOSED")
ev:RegisterEvent("TRADE_CLOSED")
ev:RegisterEvent("AUCTION_HOUSE_CLOSED")
ev:RegisterEvent("PLAYER_REGEN_ENABLED")
ev:RegisterEvent("BANKFRAME_OPENED")
ev:SetScript("OnEvent", function(self, evento, msg)
    if not Activo() then return end
    if evento == "CHAT_MSG_LOOT" then
        -- solo lo que recibe el propio jugador
        if type(msg) == "string" and (msg:find("^Recibes") or msg:find("^You receive") or msg:find("^Recibiste")) then
            Programar(3)
        end
    elseif evento == "PLAYER_REGEN_ENABLED" then
        if pendiente then Programar(2) end
    elseif evento == "BANKFRAME_OPENED" then
        if BankStack and BankStack.CommandDecorator and BankStack.SortBags then
            PA.Despues(1, function() BankStack.CommandDecorator(BankStack.SortBags, "bank")() end)
        end
    else
        Programar(2)
    end
end)

SLASH_PAAUTOORDEN1 = "/autoordenar"
SlashCmdList["PAAUTOORDEN"] = PA.AlternarAutoOrden
