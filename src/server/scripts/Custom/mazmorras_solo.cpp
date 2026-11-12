/*
 * Mazmorras en solitario (servidor personal de Luis).
 *
 * Con "DungeonFinder.Solo.Enabled = 1" el buscador de mazmorras arma un grupo de
 * una sola persona. Este script ajusta la mazmorra cuando estas solo dentro:
 * tu (y tu mascota) haces mas dano y recibes menos, que equivale a que los
 * monstruos y jefes tengan menos vida y peguen menos.
 * Al salir con el buscador vuelves al lugar donde te anotaste (lo hace el juego).
 *
 * Configuracion opcional en worldserver.conf:
 *   MazmorrasSolo.Enable        = 1
 *   MazmorrasSolo.DanoHecho     = 2.5   (multiplica tu dano)
 *   MazmorrasSolo.DanoRecibido  = 0.4   (multiplica el dano que recibes)
 *   MazmorrasSolo.Bandas        = 0     (1 = tambien en bandas)
 */

#include "ScriptMgr.h"
#include "Config.h"
#include "Player.h"
#include "Map.h"
#include "Chat.h"
#include "Log.h"

namespace MazmorrasSolo
{
    static bool sActivo = true;
    static float sDanoHecho = 2.5f;
    static float sDanoRecibido = 0.4f;
    static bool sBandas = false;

    static void Cargar()
    {
        sActivo       = sConfigMgr->GetBoolDefault("MazmorrasSolo.Enable", true);
        sDanoHecho    = sConfigMgr->GetFloatDefault("MazmorrasSolo.DanoHecho", 2.5f);
        sDanoRecibido = sConfigMgr->GetFloatDefault("MazmorrasSolo.DanoRecibido", 0.4f);
        sBandas       = sConfigMgr->GetBoolDefault("MazmorrasSolo.Bandas", false);
        if (sDanoHecho < 1.0f) sDanoHecho = 1.0f;
        if (sDanoRecibido > 1.0f) sDanoRecibido = 1.0f;
        if (sDanoRecibido < 0.05f) sDanoRecibido = 0.05f;
    }

    // Mapa donde aplica el ajuste: mazmorra (o banda si esta activado) con un solo jugador
    static bool MapaSolo(Map* map)
    {
        if (!map || !map->IsDungeon())
            return false;
        if (map->IsRaid() && !sBandas)
            return false;
        return map->GetPlayersCountExceptGMs() == 1;
    }

    // Jugador al que pertenece la unidad (el mismo, o el dueno de la mascota)
    static Player* Jugador(Unit* u)
    {
        return u ? u->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
    }

    template<class T>
    static void Ajustar(Unit* victima, Unit* atacante, T& dano)
    {
        if (!sActivo || !victima || !atacante || dano <= 0 || !MapaSolo(victima->GetMap()))
            return;
        Player* jAtacante = Jugador(atacante);
        Player* jVictima = Jugador(victima);
        if (jAtacante && !jVictima)
            dano = T(float(dano) * sDanoHecho);
        else if (jVictima && !jAtacante)
            dano = T(float(dano) * sDanoRecibido);
    }
}

using namespace MazmorrasSolo;

class mazmorras_solo_unitscript : public UnitScript
{
public:
    mazmorras_solo_unitscript() : UnitScript("mazmorras_solo_unitscript") { }

    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override { Ajustar(target, attacker, damage); }
    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage) override { Ajustar(target, attacker, damage); }
    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, int32& damage) override { Ajustar(target, attacker, damage); }
};

class mazmorras_solo_playerscript : public PlayerScript
{
public:
    mazmorras_solo_playerscript() : PlayerScript("mazmorras_solo_playerscript") { }

    void OnMapChanged(Player* player) override
    {
        if (!sActivo)
            return;
        Map* map = player->GetMap();
        if (!map || !map->IsDungeon() || (map->IsRaid() && !sBandas))
            return;
        if (map->GetPlayersCountExceptGMs() <= 1)
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cff00ccff[Mazmorra en solitario]|r Est\303\241s solo: haces |cff00ff00x%.1f|r de da\303\261o y recibes solo el |cff00ff00%u%%|r del da\303\261o.",
                sDanoHecho, uint32(sDanoRecibido * 100.0f + 0.5f));
    }
};

class mazmorras_solo_worldscript : public WorldScript
{
public:
    mazmorras_solo_worldscript() : WorldScript("mazmorras_solo_worldscript") { }
    void OnConfigLoad(bool /*reload*/) override { Cargar(); }
    void OnStartup() override
    {
        Cargar();
        TC_LOG_INFO("server.loading", ">> Mazmorras en solitario: %s (dano x%.1f, recibido x%.2f, bandas %s)",
            sActivo ? "ACTIVO" : "desactivado", sDanoHecho, sDanoRecibido, sBandas ? "si" : "no");
    }
};

void AddSC_mazmorras_solo()
{
    Cargar();
    new mazmorras_solo_unitscript();
    new mazmorras_solo_playerscript();
    new mazmorras_solo_worldscript();
}
