-- Panel central: se abre con el boton del minimapa y reune todo.

local _, PA = ...

local p = PA.CrearVentana("PuntosAtributoPanel", 360, 540, "Servidor de Luis")

local sub = p:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
sub:SetPoint("TOP", 0, -44)
sub:SetText("Todo lo del servidor en un solo lugar")

local BOTONES = {
    { "Puntos de atributo",   "Interface\\Icons\\Spell_Holy_GreaterBlessingofKings", function() PA.AlternarAtributos() end },
    { "Cartas de nivel",      "Interface\\Icons\\INV_Misc_Ticket_Tarot_Stack_01",    function() PA.AbrirCartas() end },
    { "Montar / desmontar",   "Interface\\Icons\\Ability_Mount_RidingHorse",         function() PuntosAtributo_Montar() end },
    { "Montura favorita",     "Interface\\Icons\\Ability_Mount_Charger",             function() PA.GuardarFavorita() end },
    { "Buzón portátil",       "Interface\\Icons\\INV_Letter_15",                     function() PA.Orden("buzon") end },
    { "Subasta portátil",     "Interface\\Icons\\INV_Misc_Coin_02",                  function() PA.Orden("subasta") end },
    { "Guía de niveles",      "Interface\\Icons\\INV_Misc_Map_01",                   function()
        if GuiaNiveles_Alternar then GuiaNiveles_Alternar() else PA.Aviso("El addon GuiaNiveles no está activado.") end
    end },
    { "Botón Montar en barra", "Interface\\Icons\\Ability_Mount_RidingHorse",        function() PA.PonerMontarEnBarra(false) end },
    { "Ordenar bolsas",       "Interface\\Icons\\INV_Misc_Bag_10",                     function() PA.OrdenarAhora() end },
    { "Orden automático sí/no", "Interface\\Icons\\INV_Misc_Bag_08",                   function() PA.AlternarAutoOrden() end },
    { "Salir de combate",     "Interface\\Icons\\Ability_Rogue_Feint",                  function() PA.Orden("salircombate") end },
}

for i, b in ipairs(BOTONES) do
    local col = (i - 1) % 2
    local fila = math.floor((i - 1) / 2)
    local boton = CreateFrame("Button", nil, p, "UIPanelButtonTemplate")
    boton:SetSize(150, 56)
    boton:SetPoint("TOPLEFT", 24 + col * 160, -64 - fila * 64)
    boton:SetText("")
    local ic = boton:CreateTexture(nil, "ARTWORK")
    ic:SetSize(32, 32)
    ic:SetPoint("LEFT", 10, 0)
    ic:SetTexture(b[2])
    local t = boton:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    t:SetPoint("LEFT", ic, "RIGHT", 6, 0)
    t:SetWidth(98)
    t:SetJustifyH("LEFT")
    t:SetText(b[1])
    boton:SetScript("OnClick", b[3])
end

local nota = p:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
nota:SetPoint("BOTTOM", 0, 26)
nota:SetWidth(310)
nota:SetText("Teclas: Menú > Asignación de teclas > Servidor de Luis.\n/montar  /cartas  /puntos  /guia  /sort  /autoordenar  /salircombate")

function PA.AlternarPanel()
    if p:IsShown() then p:Hide() else p:Show() end
end
PuntosAtributo_Panel = PA.AlternarPanel

SLASH_PAPANEL1 = "/servidor"
SlashCmdList["PAPANEL"] = PA.AlternarPanel

SLASH_PASALIRCOMBATE1 = "/salircombate"
SlashCmdList["PASALIRCOMBATE"] = function() PA.Orden("salircombate") end
