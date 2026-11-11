/*
 * Sistema de Atributos (Maestro de Atributos) - servidor personal de Luis.
 *
 * - Cada nivel entrega puntos de atributo: 2 por nivel hasta el 19, 3 hasta el 39,
 *   4 hasta el 59, 5 hasta el 79 y 6 desde el 80, mas 3 extra cada 10 niveles.
 * - Los puntos se reparten con el Maestro de Atributos (comando .atributos lo convoca)
 *   en Fuerza, Agilidad, Aguante, Intelecto, Espiritu, Armadura, Celeridad,
 *   Critico y Maestria. Se pueden reiniciar gratis cuando se quiera.
 * - Cada punto vale mas mientras mas nivel tiene el personaje (configurable).
 * - Al asignar, el servidor mide el valor real antes y despues y lo informa
 *   en el chat ([Verificado]).
 *
 * Configuracion opcional en worldserver.conf:
 *   Atributos.Enable            = 1
 *   Atributos.ValorPorPunto     = 1.0   (multiplicador general de stats primarios)
 *   Atributos.Armadura.Mult     = 10.0  (armadura por punto = valor * esto)
 *   Atributos.Indice.Mult       = 4.0   (indice de celeridad/critico/maestria por punto)
 *   Atributos.PuntosExtraCada10 = 3
 */

#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "Creature.h"
#include "TemporarySummon.h"
#include "Chat.h"
#include "Log.h"
#include <array>
#include <unordered_map>
#include <string>
#include <cstdio>
#include <cstdarg>

namespace Atributos
{
    enum Constantes : uint32
    {
        NPC_MAESTRO          = 500100,
        DURACION_CONVOCADO   = 3 * MINUTE * IN_MILLISECONDS,

        // Gossip: sender
        SENDER_MENU          = 1,
        SENDER_STAT          = 100,   // 100 + indice de stat
        SENDER_REINICIO      = 50,

        // Gossip: acciones del menu principal
        ACCION_INFO          = 1,
        ACCION_RESUMEN       = 2,
        ACCION_REINICIAR     = 3,
        ACCION_CERRAR        = 4,
        ACCION_ABRIR_STAT    = 10,    // 10 + indice

        // Acciones del submenu de stat
        ACCION_SUMAR_1       = 1,
        ACCION_SUMAR_5       = 5,
        ACCION_SUMAR_10      = 10,
        ACCION_SUMAR_TODO    = 999,
        ACCION_VOLVER        = 1000,
    };

    enum Stat : uint8
    {
        FUERZA = 0, AGILIDAD, AGUANTE, INTELECTO, ESPIRITU,
        ARMADURA, CELERIDAD, CRITICO, MAESTRIA,
        TOTAL_STATS
    };

    static char const* const NOMBRES[TOTAL_STATS] =
    {
        "Fuerza", "Agilidad", "Aguante", "Intelecto", "Espiritu",
        "Armadura", "Celeridad", "Critico", "Maestria"
    };

    static char const* const COLUMNAS[TOTAL_STATS] =
    {
        "fuerza", "agilidad", "aguante", "intelecto", "espiritu",
        "armadura", "celeridad", "critico", "maestria"
    };

    typedef std::array<uint32, TOTAL_STATS> Gastos;
    typedef std::array<int32, TOTAL_STATS> Aplicado;

    struct Datos
    {
        Gastos gastado{};
        Aplicado aplicado{};
        bool cargado = false;
    };

    // ------------------------------------------------------------------
    // Configuracion
    // ------------------------------------------------------------------
    struct Config
    {
        bool activo = true;
        float valorPorPunto = 1.0f;
        float multArmadura = 10.0f;
        float multIndice = 4.0f;
        uint32 extraCada10 = 3;

        void Cargar()
        {
            activo        = sConfigMgr->GetBoolDefault("Atributos.Enable", true);
            valorPorPunto = sConfigMgr->GetFloatDefault("Atributos.ValorPorPunto", 1.0f);
            multArmadura  = sConfigMgr->GetFloatDefault("Atributos.Armadura.Mult", 10.0f);
            multIndice    = sConfigMgr->GetFloatDefault("Atributos.Indice.Mult", 4.0f);
            extraCada10   = sConfigMgr->GetIntDefault("Atributos.PuntosExtraCada10", 3);
        }
    };

    static Config sCfg;
    static std::unordered_map<uint32, Datos> sJugadores;

    // ------------------------------------------------------------------
    // Reglas
    // ------------------------------------------------------------------
    static uint32 PuntosPorNivel(uint32 nivel)
    {
        if (nivel <= 19) return 2;
        if (nivel <= 39) return 3;
        if (nivel <= 59) return 4;
        if (nivel <= 79) return 5;
        return 6;
    }

    static uint32 PuntosTotales(uint32 nivel)
    {
        uint32 total = 0;
        for (uint32 l = 2; l <= nivel; ++l)
            total += PuntosPorNivel(l);
        total += (nivel / 10) * sCfg.extraCada10;
        return total;
    }

    // Cuanto vale 1 punto de un stat primario a este nivel (1 a 9 en nivel 90).
    static float ValorBase(uint32 nivel)
    {
        uint32 v = (nivel + 9) / 10;
        if (v < 1) v = 1;
        return float(v) * sCfg.valorPorPunto;
    }

    static int32 ValorPorPunto(Stat s, uint32 nivel)
    {
        float base = ValorBase(nivel);
        switch (s)
        {
            case ARMADURA:  return int32(base * sCfg.multArmadura + 0.5f);
            case CELERIDAD:
            case CRITICO:
            case MAESTRIA:  return int32(base * sCfg.multIndice + 0.5f);
            default:        return int32(base + 0.5f);
        }
    }

    static uint32 PuntosGastados(Datos const& d)
    {
        uint32 total = 0;
        for (uint32 v : d.gastado)
            total += v;
        return total;
    }

    static uint32 PuntosLibres(Player* player, Datos const& d)
    {
        uint32 total = PuntosTotales(player->GetLevel());
        uint32 usados = PuntosGastados(d);
        return total > usados ? total - usados : 0;
    }

    // ------------------------------------------------------------------
    // Aplicacion de bonos sobre el personaje
    // ------------------------------------------------------------------
    static void AplicarUno(Player* player, Stat s, int32 cantidad, bool aplicar)
    {
        if (cantidad == 0)
            return;

        switch (s)
        {
            case FUERZA:    player->HandleStatModifier(UNIT_MOD_STAT_STRENGTH,  TOTAL_VALUE, float(cantidad), aplicar); break;
            case AGILIDAD:  player->HandleStatModifier(UNIT_MOD_STAT_AGILITY,   TOTAL_VALUE, float(cantidad), aplicar); break;
            case AGUANTE:   player->HandleStatModifier(UNIT_MOD_STAT_STAMINA,   TOTAL_VALUE, float(cantidad), aplicar); break;
            case INTELECTO: player->HandleStatModifier(UNIT_MOD_STAT_INTELLECT, TOTAL_VALUE, float(cantidad), aplicar); break;
            case ESPIRITU:  player->HandleStatModifier(UNIT_MOD_STAT_SPIRIT,    TOTAL_VALUE, float(cantidad), aplicar); break;
            case ARMADURA:  player->HandleStatModifier(UNIT_MOD_ARMOR,          TOTAL_VALUE, float(cantidad), aplicar); break;
            case CELERIDAD:
                player->ApplyRatingMod(CR_HASTE_MELEE,  cantidad, aplicar);
                player->ApplyRatingMod(CR_HASTE_RANGED, cantidad, aplicar);
                player->ApplyRatingMod(CR_HASTE_SPELL,  cantidad, aplicar);
                break;
            case CRITICO:
                player->ApplyRatingMod(CR_CRIT_MELEE,  cantidad, aplicar);
                player->ApplyRatingMod(CR_CRIT_RANGED, cantidad, aplicar);
                player->ApplyRatingMod(CR_CRIT_SPELL,  cantidad, aplicar);
                break;
            case MAESTRIA:
                player->ApplyRatingMod(CR_MASTERY, cantidad, aplicar);
                break;
            default:
                break;
        }
    }

    static void QuitarTodo(Player* player, Datos& d)
    {
        for (uint8 i = 0; i < TOTAL_STATS; ++i)
        {
            AplicarUno(player, Stat(i), d.aplicado[i], false);
            d.aplicado[i] = 0;
        }
    }

    static void AplicarTodo(Player* player, Datos& d)
    {
        QuitarTodo(player, d);
        if (!sCfg.activo)
            return;

        uint32 nivel = player->GetLevel();
        for (uint8 i = 0; i < TOTAL_STATS; ++i)
        {
            int32 cantidad = int32(d.gastado[i]) * ValorPorPunto(Stat(i), nivel);
            AplicarUno(player, Stat(i), cantidad, true);
            d.aplicado[i] = cantidad;
        }
    }

    // Valor que el jugador ve en su hoja de personaje (para la verificacion)
    static float ValorVisible(Player* player, Stat s)
    {
        switch (s)
        {
            case FUERZA:    return player->GetStat(STAT_STRENGTH);
            case AGILIDAD:  return player->GetStat(STAT_AGILITY);
            case AGUANTE:   return player->GetStat(STAT_STAMINA);
            case INTELECTO: return player->GetStat(STAT_INTELLECT);
            case ESPIRITU:  return player->GetStat(STAT_SPIRIT);
            case ARMADURA:  return float(player->GetArmor());
            case CELERIDAD: return float(player->GetUInt32Value(PLAYER_FIELD_COMBAT_RATINGS + CR_HASTE_MELEE));
            case CRITICO:   return float(player->GetUInt32Value(PLAYER_FIELD_COMBAT_RATINGS + CR_CRIT_MELEE));
            case MAESTRIA:  return float(player->GetUInt32Value(PLAYER_FIELD_COMBAT_RATINGS + CR_MASTERY));
            default:        return 0.0f;
        }
    }

    // ------------------------------------------------------------------
    // Base de datos (tabla characters.character_atributos)
    // ------------------------------------------------------------------
    static void CrearTabla()
    {
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `character_atributos` ("
            "`guid` INT UNSIGNED NOT NULL,"
            "`fuerza` INT UNSIGNED NOT NULL DEFAULT 0,"
            "`agilidad` INT UNSIGNED NOT NULL DEFAULT 0,"
            "`aguante` INT UNSIGNED NOT NULL DEFAULT 0,"
            "`intelecto` INT UNSIGNED NOT NULL DEFAULT 0,"
            "`espiritu` INT UNSIGNED NOT NULL DEFAULT 0,"
            "`armadura` INT UNSIGNED NOT NULL DEFAULT 0,"
            "`celeridad` INT UNSIGNED NOT NULL DEFAULT 0,"
            "`critico` INT UNSIGNED NOT NULL DEFAULT 0,"
            "`maestria` INT UNSIGNED NOT NULL DEFAULT 0,"
            "PRIMARY KEY (`guid`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='Sistema de atributos del Maestro de Atributos'");
    }

    static void Cargar(Player* player, Datos& d)
    {
        d.gastado.fill(0);
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT fuerza, agilidad, aguante, intelecto, espiritu, armadura, celeridad, critico, maestria "
            "FROM character_atributos WHERE guid = %u", player->GetGUIDLow());
        if (result)
        {
            Field* f = result->Fetch();
            for (uint8 i = 0; i < TOTAL_STATS; ++i)
                d.gastado[i] = f[i].GetUInt32();
        }
        d.cargado = true;
    }

    static void Guardar(Player* player, Datos const& d)
    {
        CharacterDatabase.PExecute(
            "REPLACE INTO character_atributos (guid, fuerza, agilidad, aguante, intelecto, espiritu, armadura, celeridad, critico, maestria) "
            "VALUES (%u, %u, %u, %u, %u, %u, %u, %u, %u, %u)",
            player->GetGUIDLow(), d.gastado[0], d.gastado[1], d.gastado[2], d.gastado[3],
            d.gastado[4], d.gastado[5], d.gastado[6], d.gastado[7], d.gastado[8]);
    }

    static Datos& Obtener(Player* player)
    {
        Datos& d = sJugadores[player->GetGUIDLow()];
        if (!d.cargado)
        {
            Cargar(player, d);
            AplicarTodo(player, d);
        }
        return d;
    }

    // ------------------------------------------------------------------
    // Mensajes
    // ------------------------------------------------------------------
    static void Mensaje(Player* player, std::string const& texto)
    {
        ChatHandler(player->GetSession()).SendSysMessage(texto.c_str());
    }

    static std::string Formato(char const* fmt, ...)
    {
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        return buf;
    }

    static void MostrarResumen(Player* player, Datos const& d)
    {
        uint32 nivel = player->GetLevel();
        Mensaje(player, Formato("|cff00ccff[Atributos]|r Nivel %u: %u puntos en total, %u libres.",
            nivel, PuntosTotales(nivel), PuntosLibres(player, d)));
        for (uint8 i = 0; i < TOTAL_STATS; ++i)
        {
            if (!d.gastado[i])
                continue;
            Mensaje(player, Formato("  %s: %u puntos = |cff00ff00+%d|r", NOMBRES[i], d.gastado[i], d.aplicado[i]));
        }
    }

    // ------------------------------------------------------------------
    // Gossip
    // ------------------------------------------------------------------
    static void MenuPrincipal(Player* player, Creature* creature)
    {
        Datos& d = Obtener(player);
        uint32 nivel = player->GetLevel();
        ClearGossipMenuFor(player);

        AddGossipItemFor(player, GOSSIP_ICON_DOT,
            Formato("Puntos libres: %u   (nivel %u)", PuntosLibres(player, d), nivel),
            SENDER_MENU, ACCION_INFO);

        for (uint8 i = 0; i < TOTAL_STATS; ++i)
        {
            AddGossipItemFor(player, GOSSIP_ICON_TRAINER,
                Formato("%s: %u puntos (+%d)   [cada punto: +%d]", NOMBRES[i], d.gastado[i], d.aplicado[i], ValorPorPunto(Stat(i), nivel)),
                SENDER_MENU, ACCION_ABRIR_STAT + i);
        }

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Ver resumen en el chat", SENDER_MENU, ACCION_RESUMEN);
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Reiniciar todos mis puntos (gratis)", SENDER_REINICIO, ACCION_REINICIAR,
            "Se devolveran todos tus puntos para repartirlos de nuevo. Continuar?", 0, false);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Cerrar", SENDER_MENU, ACCION_CERRAR);

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    static void MenuStat(Player* player, Creature* creature, Stat s)
    {
        Datos& d = Obtener(player);
        uint32 libres = PuntosLibres(player, d);
        int32 valor = ValorPorPunto(s, player->GetLevel());
        ClearGossipMenuFor(player);

        AddGossipItemFor(player, GOSSIP_ICON_DOT,
            Formato("%s: %u puntos (+%d). Libres: %u", NOMBRES[s], d.gastado[s], d.aplicado[s], libres),
            SENDER_STAT + s, ACCION_VOLVER);

        if (libres >= 1)  AddGossipItemFor(player, GOSSIP_ICON_TRAINER, Formato("Asignar 1 punto (+%d)", valor), SENDER_STAT + s, ACCION_SUMAR_1);
        if (libres >= 5)  AddGossipItemFor(player, GOSSIP_ICON_TRAINER, Formato("Asignar 5 puntos (+%d)", valor * 5), SENDER_STAT + s, ACCION_SUMAR_5);
        if (libres >= 10) AddGossipItemFor(player, GOSSIP_ICON_TRAINER, Formato("Asignar 10 puntos (+%d)", valor * 10), SENDER_STAT + s, ACCION_SUMAR_10);
        if (libres >= 1)  AddGossipItemFor(player, GOSSIP_ICON_TRAINER, Formato("Asignar todos los libres: %u (+%d)", libres, valor * int32(libres)), SENDER_STAT + s, ACCION_SUMAR_TODO);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<< Volver", SENDER_STAT + s, ACCION_VOLVER);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    static void Asignar(Player* player, Stat s, uint32 cantidad)
    {
        Datos& d = Obtener(player);
        uint32 libres = PuntosLibres(player, d);
        if (cantidad > libres)
            cantidad = libres;
        if (!cantidad)
        {
            Mensaje(player, "|cffff0000[Atributos]|r No tienes puntos libres.");
            return;
        }

        float antes = ValorVisible(player, s);
        int32 esperado = int32(cantidad) * ValorPorPunto(s, player->GetLevel());

        d.gastado[s] += cantidad;
        AplicarTodo(player, d);
        Guardar(player, d);

        float despues = ValorVisible(player, s);
        int32 real = int32(despues - antes + (despues >= antes ? 0.5f : -0.5f));

        if (real >= esperado)
            Mensaje(player, Formato("|cff00ff00[Verificado]|r %s: %.0f -> %.0f (+%d). Asignaste %u punto(s). Te quedan %u.",
                NOMBRES[s], antes, despues, real, cantidad, PuntosLibres(player, d)));
        else
            Mensaje(player, Formato("|cffff9900[Atributos]|r %s: %.0f -> %.0f (+%d, esperado +%d). Asignaste %u punto(s). "
                "Si la diferencia es menor, puede deberse a efectos temporales activos.",
                NOMBRES[s], antes, despues, real, esperado, cantidad));
    }

    static void Reiniciar(Player* player)
    {
        Datos& d = Obtener(player);
        uint32 devueltos = PuntosGastados(d);
        QuitarTodo(player, d);
        d.gastado.fill(0);
        Guardar(player, d);
        Mensaje(player, Formato("|cff00ccff[Atributos]|r Puntos reiniciados. Se devolvieron %u puntos. Libres: %u.",
            devueltos, PuntosLibres(player, d)));
    }
}

using namespace Atributos;

// ----------------------------------------------------------------------
// NPC: Maestro de Atributos
// ----------------------------------------------------------------------
class npc_maestro_atributos : public CreatureScript
{
public:
    npc_maestro_atributos() : CreatureScript("npc_maestro_atributos") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!sCfg.activo)
        {
            Mensaje(player, "|cffff0000[Atributos]|r El sistema de atributos esta desactivado.");
            return true;
        }
        MenuPrincipal(player, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        ClearGossipMenuFor(player);

        if (sender == SENDER_MENU)
        {
            if (action >= ACCION_ABRIR_STAT && action < ACCION_ABRIR_STAT + TOTAL_STATS)
            {
                MenuStat(player, creature, Stat(action - ACCION_ABRIR_STAT));
                return true;
            }
            switch (action)
            {
                case ACCION_RESUMEN:
                    MostrarResumen(player, Obtener(player));
                    break;
                case ACCION_CERRAR:
                    CloseGossipMenuFor(player);
                    return true;
                default:
                    break;
            }
            MenuPrincipal(player, creature);
            return true;
        }

        if (sender == SENDER_REINICIO)
        {
            Reiniciar(player);
            MenuPrincipal(player, creature);
            return true;
        }

        if (sender >= SENDER_STAT && sender < SENDER_STAT + TOTAL_STATS)
        {
            Stat s = Stat(sender - SENDER_STAT);
            switch (action)
            {
                case ACCION_SUMAR_1:    Asignar(player, s, 1); break;
                case ACCION_SUMAR_5:    Asignar(player, s, 5); break;
                case ACCION_SUMAR_10:   Asignar(player, s, 10); break;
                case ACCION_SUMAR_TODO: Asignar(player, s, PuntosLibres(player, Obtener(player))); break;
                default:
                    MenuPrincipal(player, creature);
                    return true;
            }
            MenuStat(player, creature, s);
            return true;
        }

        MenuPrincipal(player, creature);
        return true;
    }
};

// ----------------------------------------------------------------------
// Eventos del jugador
// ----------------------------------------------------------------------
class atributos_playerscript : public PlayerScript
{
public:
    atributos_playerscript() : PlayerScript("atributos_playerscript") { }

    void OnLogin(Player* player) override
    {
        if (!sCfg.activo)
            return;

        Datos& d = sJugadores[player->GetGUIDLow()];
        d = Datos();
        Cargar(player, d);
        AplicarTodo(player, d);

        uint32 libres = PuntosLibres(player, d);
        if (libres)
            Mensaje(player, Formato("|cff00ccff[Atributos]|r Tienes |cff00ff00%u|r puntos de atributo libres. Escribe |cffffff00.atributos|r para convocar al Maestro.", libres));
    }

    void OnLogout(Player* player) override
    {
        sJugadores.erase(player->GetGUIDLow());
    }

    void OnLevelChanged(Player* player, uint8 oldLevel) override
    {
        if (!sCfg.activo)
            return;

        Datos& d = Obtener(player);
        AplicarTodo(player, d);   // el valor de cada punto cambia con el nivel

        uint32 nuevo = player->GetLevel();
        if (nuevo > oldLevel)
        {
            uint32 ganados = PuntosTotales(nuevo) - PuntosTotales(oldLevel);
            Mensaje(player, Formato("|cff00ccff[Atributos]|r Subiste a nivel %u: +%u puntos. Libres: |cff00ff00%u|r. Escribe |cffffff00.atributos|r.",
                nuevo, ganados, PuntosLibres(player, d)));
        }
        else if (PuntosGastados(d) > PuntosTotales(nuevo))
        {
            // Si baja de nivel y tiene mas puntos gastados de los que le corresponden, se reinician.
            Reiniciar(player);
        }
    }
};

// ----------------------------------------------------------------------
// Inicio del servidor: crea la tabla y lee la configuracion
// ----------------------------------------------------------------------
class atributos_worldscript : public WorldScript
{
public:
    atributos_worldscript() : WorldScript("atributos_worldscript") { }

    void OnConfigLoad(bool /*reload*/) override
    {
        sCfg.Cargar();
    }

    void OnStartup() override
    {
        sCfg.Cargar();
        CrearTabla();
        TC_LOG_INFO("server.loading", ">> Sistema de Atributos: %s (valor por punto x%.2f, armadura x%.1f, indices x%.1f)",
            sCfg.activo ? "ACTIVO" : "desactivado", sCfg.valorPorPunto, sCfg.multArmadura, sCfg.multIndice);
    }
};

// ----------------------------------------------------------------------
// Comando .atributos (cualquier jugador): convoca al Maestro por 3 minutos
// ----------------------------------------------------------------------
class atributos_commandscript : public CommandScript
{
public:
    atributos_commandscript() : CommandScript("atributos_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> comandos =
        {
            { "atributos", SEC_PLAYER, false, &HandleAtributos, "Convoca al Maestro de Atributos." },
        };
        return comandos;
    }

    static bool HandleAtributos(ChatHandler* handler, char const* args)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
            return false;

        if (!sCfg.activo)
        {
            handler->SendSysMessage("El sistema de atributos esta desactivado.");
            return true;
        }

        std::string arg = args ? args : "";
        if (arg == "resumen")
        {
            MostrarResumen(player, Obtener(player));
            return true;
        }

        if (player->IsInCombat())
        {
            handler->SendSysMessage("|cffff0000[Atributos]|r No puedes convocar al Maestro en combate.");
            return true;
        }

        float x, y, z;
        player->GetClosePoint(x, y, z, player->GetObjectSize(), 2.0f);
        if (TempSummon* maestro = player->SummonCreature(NPC_MAESTRO, x, y, z, player->GetOrientation() + 3.14159f,
            TEMPSUMMON_TIMED_DESPAWN, DURACION_CONVOCADO, true))
        {
            maestro->SetFacingToObject(player);
            handler->SendSysMessage("|cff00ccff[Atributos]|r El Maestro de Atributos ha llegado (3 minutos). Hablale para repartir tus puntos.");
        }
        else
            handler->SendSysMessage("|cffff0000[Atributos]|r No se pudo convocar al Maestro. Se aplico el SQL del Maestro en la base de datos?");
        return true;
    }
};

void AddSC_sistema_atributos()
{
    sCfg.Cargar();
    new npc_maestro_atributos();
    new atributos_playerscript();
    new atributos_worldscript();
    new atributos_commandscript();
}
