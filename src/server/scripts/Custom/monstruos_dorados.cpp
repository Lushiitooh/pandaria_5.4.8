/*
 * Monstruos dorados (servidor personal de Luis).
 *
 * Al aparecer, un monstruo normal del mundo abierto tiene una pequena
 * probabilidad de ser "dorado": brilla, es mas grande, tiene mas vida, pega
 * mas fuerte, da el doble de experiencia y de oro, y suelta siempre un
 * objeto azul de su nivel (a veces morado).
 *
 * Configuracion opcional en worldserver.conf:
 *   Dorados.Enable       = 1
 *   Dorados.Probabilidad = 2.0    (% de los monstruos que aparecen)
 *   Dorados.Vida         = 3.0    (multiplicador de vida)
 *   Dorados.Dano         = 1.5    (multiplicador de dano)
 *   Dorados.Escala       = 1.3    (tamano)
 *   Dorados.ProbEpico    = 10     (% de que el objeto sea morado)
 */

#include "ScriptMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "Creature.h"
#include "Chat.h"
#include "Log.h"
#include "LootMgr.h"
#include "ObjectMgr.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Dorados
{
    enum : uint32
    {
        SPELL_BRILLO_DORADO = 22580,     // "Glowy (Yellow)", aura visual sin efecto
        NIVEL_MAX           = 90,
    };

    struct Config
    {
        bool activo = true;
        float probabilidad = 2.0f;
        float vida = 3.0f;
        float dano = 1.5f;
        float escala = 1.3f;
        float probEpico = 10.0f;

        void Cargar()
        {
            activo       = sConfigMgr->GetBoolDefault("Dorados.Enable", true);
            probabilidad = sConfigMgr->GetFloatDefault("Dorados.Probabilidad", 2.0f);
            vida         = sConfigMgr->GetFloatDefault("Dorados.Vida", 3.0f);
            dano         = sConfigMgr->GetFloatDefault("Dorados.Dano", 1.5f);
            escala       = sConfigMgr->GetFloatDefault("Dorados.Escala", 1.3f);
            probEpico    = sConfigMgr->GetFloatDefault("Dorados.ProbEpico", 10.0f);
            if (vida < 1.0f) vida = 1.0f;
            if (dano < 1.0f) dano = 1.0f;
            if (escala < 0.5f) escala = 0.5f;
        }
    };

    static Config sCfg;

    // Objetos azules / morados que pueden caer en el mundo, por nivel requerido
    static std::vector<uint32> sAzules[NIVEL_MAX + 1];
    static std::vector<uint32> sMorados[NIVEL_MAX + 1];

    // Avisos ya dados a cada jugador (guid jugador -> guids de monstruos)
    static std::mutex sMutex;
    static std::unordered_map<uint32, std::unordered_set<uint64>> sAvisados;

    static void CargarObjetos()
    {
        for (uint32 i = 0; i <= NIVEL_MAX; ++i)
        {
            sAzules[i].clear();
            sMorados[i].clear();
        }

        std::unordered_set<uint32> enBotin;
        if (QueryResult r = WorldDatabase.Query("SELECT DISTINCT item FROM reference_loot_template"))
            do enBotin.insert(r->Fetch()[0].GetUInt32()); while (r->NextRow());
        if (QueryResult r = WorldDatabase.Query("SELECT DISTINCT item FROM creature_loot_template"))
            do enBotin.insert(r->Fetch()[0].GetUInt32()); while (r->NextRow());

        uint32 azules = 0, morados = 0;
        for (uint32 entry : enBotin)
        {
            ItemTemplate const* t = sObjectMgr->GetItemTemplate(entry);
            if (!t)
                continue;
            if (t->Class != ITEM_CLASS_WEAPON && t->Class != ITEM_CLASS_ARMOR)
                continue;
            if (t->Quality != ITEM_QUALITY_RARE && t->Quality != ITEM_QUALITY_EPIC)
                continue;
            if (t->Bonding != BIND_ON_EQUIP || t->InventoryType == INVTYPE_NON_EQUIP)
                continue;
            uint32 nivel = t->RequiredLevel;
            if (nivel > NIVEL_MAX)
                continue;
            if (t->Quality == ITEM_QUALITY_RARE)
                sAzules[nivel].push_back(entry), ++azules;
            else
                sMorados[nivel].push_back(entry), ++morados;
        }
        TC_LOG_INFO("server.loading", ">> Monstruos dorados: %u objetos azules y %u morados disponibles como botin", azules, morados);
    }

    // Busca objetos cerca del nivel: primero hacia abajo, luego hacia arriba
    static uint32 ElegirObjeto(std::vector<uint32> const (&tabla)[NIVEL_MAX + 1], uint32 nivel, Player* jugador)
    {
        if (nivel > NIVEL_MAX)
            nivel = NIVEL_MAX;
        std::vector<uint32> candidatos;
        for (uint32 rango = 0; rango <= NIVEL_MAX && candidatos.size() < 8; rango += 3)
        {
            candidatos.clear();
            uint32 desde = nivel > rango + 3 ? nivel - rango - 3 : 0;
            uint32 hasta = std::min<uint32>(nivel + (rango > 6 ? rango - 6 : 0), NIVEL_MAX);
            for (uint32 n = desde; n <= hasta; ++n)
                candidatos.insert(candidatos.end(), tabla[n].begin(), tabla[n].end());
        }
        if (candidatos.empty())
            return 0;

        // Preferimos algo que el jugador pueda usar
        for (uint8 intento = 0; intento < 12; ++intento)
        {
            uint32 entry = candidatos[urand(0, candidatos.size() - 1)];
            ItemTemplate const* t = sObjectMgr->GetItemTemplate(entry);
            if (jugador && t && jugador->CanUseItem(t) == EQUIP_ERR_OK)
                return entry;
        }
        return candidatos[urand(0, candidatos.size() - 1)];
    }

    static bool PuedeSerDorado(Creature* c)
    {
        if (!c->IsAlive() || !c->GetMap() || c->GetMap()->Instanceable())
            return false;
        if (c->IsSummon() || c->IsPet() || c->IsTotem() || c->IsGuardian() || c->IsVehicle())
            return false;
        if (c->IsCivilian() || c->IsTrigger() || c->isWorldBoss())
            return false;
        CreatureTemplate const* t = c->GetCreatureTemplate();
        if (!t || t->rank != CREATURE_ELITE_NORMAL || !t->lootid || t->npcflag)
            return false;
        if (t->type == CREATURE_TYPE_CRITTER || t->type == CREATURE_TYPE_NON_COMBAT_PET || t->type == CREATURE_TYPE_TOTEM)
            return false;
        if (c->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PC))
            return false;
        return c->GetLevel() >= 2;
    }

    static void Quitar(Creature* c)
    {
        if (!c->m_esDorado)
            return;
        c->HandleStatModifier(UNIT_MOD_HEALTH, TOTAL_PCT, c->m_doradoVidaPct, false);
        c->HandleStatModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_PCT, c->m_doradoDanoPct, false);
        c->HandleStatModifier(UNIT_MOD_DAMAGE_OFFHAND, TOTAL_PCT, c->m_doradoDanoPct, false);
        c->HandleStatModifier(UNIT_MOD_DAMAGE_RANGED, TOTAL_PCT, c->m_doradoDanoPct, false);
        c->SetObjectScale(c->m_doradoEscala);
        c->RemoveAurasDueToSpell(SPELL_BRILLO_DORADO);
        c->m_esDorado = false;
    }

    static void Convertir(Creature* c)
    {
        c->m_esDorado = true;
        c->m_doradoEscala = c->GetObjectScale();
        c->m_doradoVidaPct = (sCfg.vida - 1.0f) * 100.0f;
        c->m_doradoDanoPct = (sCfg.dano - 1.0f) * 100.0f;

        c->SetObjectScale(c->m_doradoEscala * sCfg.escala);
        c->HandleStatModifier(UNIT_MOD_HEALTH, TOTAL_PCT, c->m_doradoVidaPct, true);
        c->HandleStatModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_PCT, c->m_doradoDanoPct, true);
        c->HandleStatModifier(UNIT_MOD_DAMAGE_OFFHAND, TOTAL_PCT, c->m_doradoDanoPct, true);
        c->HandleStatModifier(UNIT_MOD_DAMAGE_RANGED, TOTAL_PCT, c->m_doradoDanoPct, true);
        c->UpdateMaxHealth();
        c->UpdateDamagePhysical(BASE_ATTACK);
        c->UpdateDamagePhysical(OFF_ATTACK);
        c->UpdateDamagePhysical(RANGED_ATTACK);
        c->SetFullHealth();
        c->AddAura(SPELL_BRILLO_DORADO, c);
    }

    // Busca monstruos dorados vivos cerca de un jugador
    struct BuscarDorados
    {
        WorldObject const* origen;
        float rango;
        BuscarDorados(WorldObject const* o, float r) : origen(o), rango(r) { }
        bool operator()(Creature* c) const
        {
            return c->m_esDorado && c->IsAlive() && origen->IsWithinDistInMap(c, rango);
        }
    };
}

using namespace Dorados;

class dorados_worldscript : public WorldScript
{
public:
    dorados_worldscript() : WorldScript("dorados_worldscript") { }

    void OnConfigLoad(bool /*reload*/) override { sCfg.Cargar(); }

    void OnStartup() override
    {
        sCfg.Cargar();
        CargarObjetos();
        TC_LOG_INFO("server.loading", ">> Monstruos dorados: %s (%.1f%%, vida x%.1f, dano x%.1f)",
            sCfg.activo ? "ACTIVO" : "desactivado", sCfg.probabilidad, sCfg.vida, sCfg.dano);
    }

    void OnCreatureSpawn(Creature* c) override
    {
        if (!c)
            return;
        Quitar(c);   // si reaparece, vuelve a ser normal antes de tirar el dado
        if (!sCfg.activo || !PuedeSerDorado(c))
            return;
        if (roll_chance_f(sCfg.probabilidad))
            Convertir(c);
    }

    void OnCreatureLootGenerated(Player* looter, Creature* c) override
    {
        if (!c || !c->m_esDorado)
            return;

        bool epico = roll_chance_f(sCfg.probEpico);
        uint32 entry = ElegirObjeto(epico ? sMorados : sAzules, c->GetLevel(), looter);
        if (!entry && epico)
            entry = ElegirObjeto(sAzules, c->GetLevel(), looter);
        if (entry)
        {
            LootStoreItem item(entry, 0, 100.0f, 0, 0, 1, 1);
            c->loot.AddItem(item, 1u);
        }

        // Doble de oro (o algo de oro si no tenia)
        if (c->loot.gold)
            c->loot.gold *= 2;
        else
            c->loot.gold = c->GetLevel() * 100;

        c->SetFlag(OBJECT_FIELD_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);
    }
};

class dorados_playerscript : public PlayerScript
{
public:
    dorados_playerscript() : PlayerScript("dorados_playerscript") { }

    void OnGiveXP(Player* /*player*/, uint32& amount, Unit* victim) override
    {
        if (victim && victim->GetTypeId() == TYPEID_UNIT && victim->ToCreature()->m_esDorado)
            amount *= 2;
    }

    void OnCreatureKill(Player* player, Creature* c) override
    {
        if (c && c->m_esDorado)
            ChatHandler(player->GetSession()).PSendSysMessage("|cffffd700[Monstruo dorado]|r \302\241Derrotaste a %s! Revisa su bot\303\255n.", c->GetName().c_str());
    }

    void OnLogout(Player* player) override
    {
        std::lock_guard<std::mutex> lock(sMutex);
        sAvisados.erase(player->GetGUIDLow());
    }

    // Cada 3 segundos: avisa de monstruos dorados cercanos y les repone el brillo
    // (lo pierden si se reinician al perseguir a alguien).
    void OnUpdate(Player* player, uint32 diff) override
    {
        if (!sCfg.activo)
            return;
        {
            std::lock_guard<std::mutex> lock(_timersMutex);
            uint32& timer = _timers[player->GetGUIDLow()];
            if (timer > diff)
            {
                timer -= diff;
                return;
            }
            timer = 3000;
        }

        if (!player->IsInWorld() || player->GetMap()->Instanceable())
            return;

        std::list<Creature*> lista;
        BuscarDorados check(player, 80.0f);
        Trinity::CreatureListSearcher<BuscarDorados> searcher(player, lista, check);
        player->VisitNearbyGridObject(80.0f, searcher);

        for (Creature* c : lista)
        {
            if (!c->HasAura(SPELL_BRILLO_DORADO) && !c->IsInCombat())
                c->AddAura(SPELL_BRILLO_DORADO, c);

            bool nuevo;
            {
                std::lock_guard<std::mutex> lock(sMutex);
                auto& avisados = sAvisados[player->GetGUIDLow()];
                if (avisados.size() > 500)
                    avisados.clear();
                nuevo = avisados.insert(c->GetGUID()).second;
            }
            if (nuevo)
            {
                ChatHandler(player->GetSession()).PSendSysMessage("|cffffd700[Monstruo dorado]|r \302\241%s (nivel %u) brilla con oro cerca de ti! Bot\303\255n azul o morado garantizado.",
                    c->GetName().c_str(), c->GetLevel());
                player->GetSession()->SendNotification("|cffffd700\302\241Monstruo dorado cerca: %s!|r", c->GetName().c_str());
            }
        }
    }

private:
    std::unordered_map<uint32, uint32> _timers;   // cada jugador se actualiza en el hilo de su mapa
    std::mutex _timersMutex;
};

void AddSC_monstruos_dorados()
{
    sCfg.Cargar();
    new dorados_worldscript();
    new dorados_playerscript();
}
