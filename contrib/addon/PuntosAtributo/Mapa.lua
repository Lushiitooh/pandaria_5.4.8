-- Mapa en mazmorras: algunos addons de guia (Dugi, WoWPro...) cambian el mapa
-- abierto al punto de su guia, que esta fuera de la mazmorra, y se ve el mapa
-- del mundo. Al abrir el mapa dentro de una mazmorra lo devolvemos a la mazmorra.
-- Despues se puede cambiar de piso o de zona con normalidad.

local _, PA = ...

local function VolverAMazmorra()
    if WorldMapFrame:IsShown() and IsInInstance() then
        SetMapToCurrentZone()
    end
end

WorldMapFrame:HookScript("OnShow", function()
    if not IsInInstance() then return end
    VolverAMazmorra()
    PA.Despues(0.1, VolverAMazmorra)
    PA.Despues(0.5, VolverAMazmorra)
end)
