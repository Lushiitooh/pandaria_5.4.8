/*
 * Modo solitario para mazmorras, escenarios y bandas (servidor personal de Luis).
 *
 * Tu personaje no cambia: tus atributos, cartas y equipo cuentan igual que siempre.
 * Lo que cambia es el ajuste de la instancia segun cuantos jugadores hay dentro
 * comparado con los que pide (5, 10 o 25):
 *
 *  - Dano que recibes de los enemigos (tu y tu mascota): baja mucho si estas solo
 *    y vuelve a lo normal a medida que entran mas jugadores.
 *  - Dano que haces a los enemigos: en mazmorras y escenarios queda normal
 *    (los enemigos tienen su vida de siempre). En bandas sube, que equivale a que
 *    los jefes tengan menos vida, para que un jefe dure unos minutos y no pase su
 *    limite de tiempo.
 *
 * Con el grupo completo todo es normal.
 *
 * Configuracion en worldserver.conf (valores para 1 jugador):
 *   MazmorrasSolo.Enable = 1
 *   MazmorrasSolo.Recibido.Mazmorra        = 0.35
 *   MazmorrasSolo.Recibido.MazmorraHeroica = 0.30
 *   MazmorrasSolo.Recibido.Escenario       = 0.50
 *   MazmorrasSolo.Recibido.Banda10         = 0.10
 *   MazmorrasSolo.Recibido.Banda10H        = 0.07
 *   MazmorrasSolo.Recibido.Banda25         = 0.06
 *   MazmorrasSolo.Recibido.Banda25H        = 0.05
 *   MazmorrasSolo.Hecho.Mazmorra           = 1.0
 *   MazmorrasSolo.Hecho.Escenario          = 1.0
 *   MazmorrasSolo.Hecho.Banda10            = 1.7
 *   MazmorrasSolo.Hecho.Banda10H           = 2.0
 *   MazmorrasSolo.Hecho.Banda25            = 3.3
 *   MazmorrasSolo.Hecho.Banda25H           = 4.0
 */

#include "ScriptMgr.h"
#include "Config.h"
#include "Player.h"
#include "Map.h"
#include "Chat.h"
#include "Log.h"

namespace MazmorrasSolo
{
    enum Tipo : uint8 { T_MAZMORRA, T_MAZMORRA_H, T_ESCENARIO, T_BANDA10, T_BANDA10H, T_BANDA25, T_BANDA25H, T_MAX };

    static bool  sActivo = true;
    static float sRecibido[T_MAX];
    static float sHecho[T_MAX];

    static char const* NOMBRE[T_MAX] = { "Mazmorra", "Mazmorra heroica", "Escenario", "Banda de 10", "Banda de 10 heroica", "Banda de 25", "Banda de 25 heroica" };

    static float Limitar(float v, float min, float max) { return v < min ? min : (v > max ? max : v); }

    static void Cargar()
    {
        sActivo = sConfigMgr->GetBoolDefault("MazmorrasSolo.Enable", true);

        sRecibido[T_MAZMORRA]   = sConfigMgr->GetFloatDefault("MazmorrasSolo.Recibido.Mazmorra", 0.35f);
        sRecibido[T_MAZMORRA_H] = sConfigMgr->GetFloatDefault("MazmorrasSolo.Recibido.MazmorraHeroica", 0.30f);
        sRecibido[T_ESCENARIO]  = sConfigMgr->GetFloatDefault("MazmorrasSolo.Recibido.Escenario", 0.50f);
        sRecibido[T_BANDA10]    = sConfigMgr->GetFloatDefault("MazmorrasSolo.Recibido.Banda10", 0.10f);
        sRecibido[T_BANDA10H]   = sConfigMgr->GetFloatDefault("MazmorrasSolo.Recibido.Banda10H", 0.07f);
        sRecibido[T_BANDA25]    = sConfigMgr->GetFloatDefault("MazmorrasSolo.Recibido.Banda25", 0.06f);
        sRecibido[T_BANDA25H]   = sConfigMgr->GetFloatDefault("MazmorrasSolo.Recibido.Banda25H", 0.05f);

        sHecho[T_MAZMORRA]   = sConfigMgr->GetFloatDefault("MazmorrasSolo.Hecho.Mazmorra", 1.0f);
        sHecho[T_MAZMORRA_H] = sHecho[T_MAZMORRA];
        sHecho[T_ESCENARIO]  = sConfigMgr->GetFloatDefault("MazmorrasSolo.Hecho.Escenario", 1.0f);
        sHecho[T_BANDA10]    = sConfigMgr->GetFloatDefault("MazmorrasSolo.Hecho.Banda10", 1.7f);
        sHecho[T_BANDA10H]   = sConfigMgr->GetFloatDefault("MazmorrasSolo.Hecho.Banda10H", 2.0f);
        sHecho[T_BANDA25]    = sConfigMgr->GetFloatDefault("MazmorrasSolo.Hecho.Banda25", 3.3f);
        sHecho[T_BANDA25H]   = sConfigMgr->GetFloatDefault("MazmorrasSolo.Hecho.Banda25H", 4.0f);

        for (uint8 i = 0; i < T_MAX; ++i)
        {
            sRecibido[i] = Limitar(sRecibido[i], 0.01f, 1.0f);
            sHecho[i]    = Limitar(sHecho[i], 1.0f, 20.0f);
        }
    }

    // Tipo de instancia y cuantos jugadores pide. false si no es instancia.
    static bool Clasificar(Map* map, Tipo& tipo, uint32& maximo)
    {
        if (!map || !map->IsDungeon())
            return false;

        InstanceMap* inst = map->ToInstanceMap();
        maximo = inst ? inst->GetMaxPlayers() : 5;
        Difficulty d = map->GetDifficulty();

        if (map->IsScenario() || d == SCENARIO_DIFFICULTY_NORMAL || d == SCENARIO_DIFFICULTY_HEROIC)
        {
            tipo = T_ESCENARIO;
            if (!maximo) maximo = 3;
        }
        else if (map->IsRaid())
        {
            bool heroica = d == RAID_DIFFICULTY_10MAN_HEROIC || d == RAID_DIFFICULTY_25MAN_HEROIC;
            if (d == RAID_DIFFICULTY_1025MAN_FLEX)
                maximo = 10;            // la banda flexible ya se ajusta sola de 10 a 25
            if (!maximo) maximo = 10;
            if (maximo <= 10)
                tipo = heroica ? T_BANDA10H : T_BANDA10;
            else
                tipo = heroica ? T_BANDA25H : T_BANDA25;
        }
        else
        {
            tipo = (d == DUNGEON_DIFFICULTY_HEROIC || d == DUNGEON_DIFFICULTY_CHALLENGE) ? T_MAZMORRA_H : T_MAZMORRA;
            if (!maximo) maximo = 5;
        }
        return true;
    }

    // Valor para 1 jugador que sube en linea recta hasta 1.0 con el grupo completo
    static float Escalar(float solo, uint32 jugadores, uint32 maximo)
    {
        if (maximo <= 1 || jugadores >= maximo)
            return 1.0f;
        if (jugadores < 1)
            jugadores = 1;
        float t = float(jugadores - 1) / float(maximo - 1);
        return solo + (1.0f - solo) * t;
    }

    static bool Factores(Map* map, float& recibido, float& hecho, Tipo* tipoOut = nullptr, uint32* jugOut = nullptr, uint32* maxOut = nullptr)
    {
        Tipo tipo;
        uint32 maximo;
        if (!Clasificar(map, tipo, maximo))
            return false;
        uint32 jugadores = map->GetPlayersCountExceptGMs();
        recibido = Escalar(sRecibido[tipo], jugadores, maximo);
        hecho    = Escalar(sHecho[tipo], jugadores, maximo);
        if (tipoOut) *tipoOut = tipo;
        if (jugOut) *jugOut = jugadores;
        if (maxOut) *maxOut = maximo;
        return recibido < 0.999f || hecho > 1.001f;
    }

    // Jugador al que pertenece la unidad (el mismo, o el dueno de la mascota)
    static Player* Jugador(Unit* u)
    {
        return u ? u->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
    }

    template<class T>
    static void Ajustar(Unit* victima, Unit* atacante, T& dano)
    {
        if (!sActivo || !victima || !atacante || dano <= 0 || victima == atacante)
            return;
        Player* jAtacante = Jugador(atacante);
        Player* jVictima = Jugador(victima);
        if (bool(jAtacante) == bool(jVictima))   // jugador contra jugador o enemigo contra enemigo
            return;

        float recibido, hecho;
        if (!Factores(victima->GetMap(), recibido, hecho))
            return;

        if (jVictima)
            dano = T(float(dano) * recibido);
        else
            dano = T(float(dano) * hecho);
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
        float recibido, hecho;
        Tipo tipo;
        uint32 jugadores, maximo;
        if (!Factores(player->GetMap(), recibido, hecho, &tipo, &jugadores, &maximo))
            return;

        ChatHandler h(player->GetSession());
        h.PSendSysMessage("|cff00ccff[Modo solitario]|r %s con %u de %u jugadores: recibes el |cff00ff00%u%%|r del da\303\261o.",
            NOMBRE[tipo], jugadores, maximo, uint32(recibido * 100.0f + 0.5f));
        if (hecho > 1.001f)
            h.PSendSysMessage("|cff00ccff[Modo solitario]|r Haces |cff00ff00x%.1f|r de da\303\261o a los enemigos.", hecho);
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
        TC_LOG_INFO("server.loading", ">> Modo solitario: %s (mazmorra recibe x%.2f, banda 10 recibe x%.2f hace x%.1f, banda 25 recibe x%.2f hace x%.1f)",
            sActivo ? "ACTIVO" : "desactivado", sRecibido[T_MAZMORRA], sRecibido[T_BANDA10], sHecho[T_BANDA10], sRecibido[T_BANDA25], sHecho[T_BANDA25]);
    }
};

void AddSC_mazmorras_solo()
{
    Cargar();
    new mazmorras_solo_unitscript();
    new mazmorras_solo_playerscript();
    new mazmorras_solo_worldscript();
}
