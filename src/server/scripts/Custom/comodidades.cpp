/*
 * Comodidades portatiles (servidor personal de Luis).
 *
 *   .buzon     aparece un buzon a tu lado durante 2 minutos
 *   .subasta   aparece un subastador a tu lado durante 2 minutos
 *   .salircombate  te saca del combate si quedaste trabado (10 s sin pelear)
 *
 * Los usa el panel del addon PuntosAtributo (botones "Buzon" y "Subasta").
 *
 * Configuracion opcional en worldserver.conf:
 *   Comodidades.Enable   = 1
 *   Comodidades.Duracion = 120   (segundos que dura)
 *   Comodidades.Espera   = 30    (segundos entre usos)
 */

#include "ScriptMgr.h"
#include "Config.h"
#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
#include "TemporarySummon.h"
#include "Chat.h"
#include "InstanceScript.h"
#include "Pet.h"
#include <mutex>
#include <unordered_map>
#include <ctime>

namespace Comodidades
{
    enum : uint32
    {
        GO_BUZON             = 32349,   // Mailbox
        NPC_SUBASTA_ALIANZA  = 8719,    // Subastador Fitch (Ventormenta)
        NPC_SUBASTA_HORDA    = 8673,    // Subastador Thathung (Orgrimmar)
        NPC_SUBASTA_NEUTRAL  = 8661,    // Subastador Beardo (Gadgetzan)
    };

    static bool sActivo = true;
    static uint32 sDuracion = 120;
    static uint32 sEspera = 30;

    static std::mutex sMutex;
    static std::unordered_map<uint64, time_t> sUltimoUso;   // (guid << 1 | tipo) -> momento

    static void Cargar()
    {
        sActivo   = sConfigMgr->GetBoolDefault("Comodidades.Enable", true);
        sDuracion = sConfigMgr->GetIntDefault("Comodidades.Duracion", 120);
        sEspera   = sConfigMgr->GetIntDefault("Comodidades.Espera", 30);
        if (sDuracion < 10) sDuracion = 10;
    }

    // Devuelve los segundos que faltan (0 = se puede usar) y marca el uso si se puede
    static uint32 Esperar(Player* player, uint32 tipo)
    {
        std::lock_guard<std::mutex> lock(sMutex);
        uint64 clave = (uint64(player->GetGUIDLow()) << 1) | tipo;
        time_t ahora = time(nullptr);
        time_t& ultimo = sUltimoUso[clave];
        if (ultimo && ahora < ultimo + time_t(sEspera))
            return uint32(ultimo + time_t(sEspera) - ahora);
        ultimo = ahora;
        return 0;
    }

    static bool Comprobar(ChatHandler* handler, Player* player)
    {
        if (!sActivo)
        {
            handler->SendSysMessage("Las comodidades port\303\241tiles est\303\241n desactivadas.");
            return false;
        }
        if (player->IsInCombat())
        {
            handler->SendSysMessage("|cffff0000No puedes hacer eso en combate.|r");
            return false;
        }
        if (player->GetMap()->IsBattlegroundOrArena())
        {
            handler->SendSysMessage("|cffff0000No disponible en campos de batalla ni arenas.|r");
            return false;
        }
        if (player->IsMounted() || player->IsInFlight() || player->GetTransport())
        {
            handler->SendSysMessage("|cffff0000B\303\241jate de la montura o del transporte primero.|r");
            return false;
        }
        return true;
    }
}

using namespace Comodidades;

class comodidades_commandscript : public CommandScript
{
public:
    comodidades_commandscript() : CommandScript("comodidades_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> comandos =
        {
            { "buzon",   SEC_PLAYER, false, &HandleBuzon,   "Hace aparecer un buz\303\263n a tu lado." },
            { "subasta", SEC_PLAYER, false, &HandleSubasta, "Hace aparecer un subastador a tu lado." },
            { "salircombate", SEC_PLAYER, false, &HandleSalirCombate, "Te saca del combate si quedaste trabado (10 segundos sin pelear)." },
        };
        return comandos;
    }

    static bool HandleSalirCombate(ChatHandler* handler, char const* /*args*/)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
            return true;
        if (!player->IsInCombat())
        {
            handler->SendSysMessage("|cff00ccff[Combate]|r No est\303\241s en combate.");
            return true;
        }
        if (InstanceScript* instance = player->GetInstanceScript())
            if (instance->IsEncounterInProgress())
            {
                handler->SendSysMessage("|cffff0000Hay una pelea contra un jefe en curso.|r");
                return true;
            }
        // El temporizador vuelve a 15 s con cada golpe o accion de combate
        if (player->GetPvECombatTimer() > 5000 || player->GetVictim())
        {
            handler->SendSysMessage("|cffff0000Est\303\241s peleando. Deja de atacar y espera 10 segundos sin recibir da\303\261o.|r");
            return true;
        }
        player->ForceLeaveCombat();
        handler->SendSysMessage("|cff00ccff[Combate]|r Saliste del combate.");
        return true;
    }

    static bool HandleBuzon(ChatHandler* handler, char const* /*args*/)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player || !Comprobar(handler, player))
            return true;
        if (uint32 falta = Esperar(player, 0))
        {
            handler->PSendSysMessage("|cffff0000Debes esperar %u segundos para volver a llamar un buz\303\263n.|r", falta);
            return true;
        }

        float x, y, z;
        player->GetClosePoint(x, y, z, player->GetObjectSize(), 2.0f);
        if (GameObject* go = player->SummonGameObject(GO_BUZON, x, y, z, player->GetOrientation() + 3.14159f, { }, sDuracion))
        {
            go->SetSpawnedByDefault(false);
            handler->PSendSysMessage("|cff00ccff[Buz\303\263n]|r Un buz\303\263n apareci\303\263 a tu lado (%u segundos).", sDuracion);
        }
        else
            handler->SendSysMessage("|cffff0000No se pudo crear el buz\303\263n aqu\303\255.|r");
        return true;
    }

    static bool HandleSubasta(ChatHandler* handler, char const* /*args*/)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player || !Comprobar(handler, player))
            return true;
        if (uint32 falta = Esperar(player, 1))
        {
            handler->PSendSysMessage("|cffff0000Debes esperar %u segundos para volver a llamar al subastador.|r", falta);
            return true;
        }

        uint32 entry = NPC_SUBASTA_NEUTRAL;
        if (player->GetTeam() == ALLIANCE)
            entry = NPC_SUBASTA_ALIANZA;
        else if (player->GetTeam() == HORDE)
            entry = NPC_SUBASTA_HORDA;

        float x, y, z;
        player->GetClosePoint(x, y, z, player->GetObjectSize(), 2.0f);
        if (TempSummon* npc = player->SummonCreature(entry, x, y, z, player->GetOrientation() + 3.14159f,
            TEMPSUMMON_TIMED_DESPAWN, sDuracion * IN_MILLISECONDS, true))
        {
            npc->SetFacingToObject(player);
            npc->SetReactState(REACT_PASSIVE);
            handler->PSendSysMessage("|cff00ccff[Subasta]|r El subastador lleg\303\263 (%u segundos). H\303\241blale para comprar y vender.", sDuracion);
        }
        else
            handler->SendSysMessage("|cffff0000No se pudo llamar al subastador aqu\303\255.|r");
        return true;
    }
};

class comodidades_worldscript : public WorldScript
{
public:
    comodidades_worldscript() : WorldScript("comodidades_worldscript") { }
    void OnConfigLoad(bool /*reload*/) override { Cargar(); }
    void OnStartup() override { Cargar(); }
};

void AddSC_comodidades()
{
    Cargar();
    new comodidades_commandscript();
    new comodidades_worldscript();
}
