/*
 * Extras del servidor personal de Luis.
 *
 *  1. Montura temprana: todo personaje sabe Equitacion de aprendiz y la montura
 *     basica de su raza desde el nivel 1.
 *  2. Velocidad en ciudades: dentro de una capital (y los santuarios de Pandaria)
 *     se gana velocidad de movimiento a pie. Se quita al salir.
 *  3. Experiencia extra en grupo: multiplica la experiencia si estas en grupo.
 *
 * Configuracion opcional en worldserver.conf:
 *   Extras.MonturaTemprana.Enable = 1
 *   Extras.VelocidadCiudad.Enable = 1
 *   Extras.VelocidadCiudad.Stacks = 4     (cada stack = +10% de velocidad)
 *   Extras.XPGrupo.Mult           = 2.0   (1.0 = desactivado)
 */

#include "ScriptMgr.h"
#include "Config.h"
#include "Player.h"
#include "Group.h"
#include "DBCStores.h"
#include "SpellAuras.h"
#include "Log.h"

namespace Extras
{
    enum : uint32
    {
        SPELL_EQUITACION_APRENDIZ = 33388,
        SPELL_VELOCIDAD_CIUDAD    = 107199,   // "Invisible Stacking Move Speed Increase" (+10% por stack, invisible)
    };

    struct Config
    {
        bool montura = true;
        bool velocidad = true;
        uint32 stacks = 4;
        float xpGrupo = 2.0f;

        void Cargar()
        {
            montura   = sConfigMgr->GetBoolDefault("Extras.MonturaTemprana.Enable", true);
            velocidad = sConfigMgr->GetBoolDefault("Extras.VelocidadCiudad.Enable", true);
            stacks    = sConfigMgr->GetIntDefault("Extras.VelocidadCiudad.Stacks", 4);
            xpGrupo   = sConfigMgr->GetFloatDefault("Extras.XPGrupo.Mult", 2.0f);
            if (stacks < 1) stacks = 1;
            if (stacks > 20) stacks = 20;
        }
    };

    static Config sCfg;

    static uint32 MonturaDeRaza(uint8 raza)
    {
        switch (raza)
        {
            case RACE_HUMAN:             return 458;     // Brown Horse
            case RACE_ORC:               return 6654;    // Brown Wolf
            case RACE_DWARF:             return 6899;    // Brown Ram
            case RACE_NIGHTELF:          return 10789;   // Spotted Frostsaber
            case RACE_UNDEAD_PLAYER:     return 64977;   // Black Skeletal Horse
            case RACE_TAUREN:            return 18990;   // Brown Kodo
            case RACE_GNOME:             return 10873;   // Red Mechanostrider
            case RACE_TROLL:             return 8395;    // Emerald Raptor
            case RACE_GOBLIN:            return 87090;   // Goblin Trike
            case RACE_BLOODELF:          return 35022;   // Black Hawkstrider
            case RACE_DRAENEI:           return 34406;   // Brown Elekk
            case RACE_WORGEN:            return 103195;  // Mountain Horse
            case RACE_PANDAREN_NEUTRAL:
            case RACE_PANDAREN_ALLIANCE:
            case RACE_PANDAREN_HORDE:    return 120395;  // Green Dragon Turtle
            default:                     return 458;
        }
    }

    static void DarMontura(Player* player)
    {
        if (!sCfg.montura)
            return;
        if (!player->HasSpell(SPELL_EQUITACION_APRENDIZ))
            player->LearnSpell(SPELL_EQUITACION_APRENDIZ, false);
        uint32 montura = MonturaDeRaza(player->GetRace());
        if (montura && !player->HasSpell(montura))
            player->LearnSpell(montura, false);
    }

    static bool EsCiudad(uint32 zona, uint32 area)
    {
        if (AreaTableEntry const* a = sAreaTableStore.LookupEntry(area))
            if (a->flags & AREA_FLAG_CAPITAL)
                return true;
        if (AreaTableEntry const* z = sAreaTableStore.LookupEntry(zona))
            if (z->flags & AREA_FLAG_CAPITAL)
                return true;
        return false;
    }

    static void ActualizarVelocidad(Player* player, uint32 zona, uint32 area)
    {
        bool enCiudad = sCfg.velocidad && EsCiudad(zona, area);
        bool tiene = player->HasAura(SPELL_VELOCIDAD_CIUDAD);

        if (enCiudad && !tiene)
        {
            if (Aura* aura = player->AddAura(SPELL_VELOCIDAD_CIUDAD, player))
                aura->SetStackAmount(uint8(sCfg.stacks));
        }
        else if (!enCiudad && tiene)
            player->RemoveAurasDueToSpell(SPELL_VELOCIDAD_CIUDAD);
    }
}

using namespace Extras;

class extras_playerscript : public PlayerScript
{
public:
    extras_playerscript() : PlayerScript("extras_playerscript") { }

    void OnLogin(Player* player) override
    {
        DarMontura(player);
        ActualizarVelocidad(player, player->GetZoneId(), player->GetAreaId());
    }

    void OnUpdateZone(Player* player, uint32 newZone, uint32 newArea) override
    {
        ActualizarVelocidad(player, newZone, newArea);
    }

    void OnGiveXP(Player* player, uint32& amount, Unit* /*victim*/) override
    {
        if (sCfg.xpGrupo <= 1.0f || !amount)
            return;
        if (Group* grupo = player->GetGroup())
            if (grupo->GetMembersCount() > 1)
                amount = uint32(float(amount) * sCfg.xpGrupo);
    }
};

class extras_worldscript : public WorldScript
{
public:
    extras_worldscript() : WorldScript("extras_worldscript") { }

    void OnConfigLoad(bool /*reload*/) override { sCfg.Cargar(); }

    void OnStartup() override
    {
        sCfg.Cargar();
        TC_LOG_INFO("server.loading", ">> Extras: montura temprana %s, velocidad en ciudad %s (+%u%%), XP en grupo x%.2f",
            sCfg.montura ? "SI" : "no", sCfg.velocidad ? "SI" : "no", sCfg.stacks * 10, sCfg.xpGrupo);
    }
};

void AddSC_extras_servidor()
{
    sCfg.Cargar();
    new extras_playerscript();
    new extras_worldscript();
}
