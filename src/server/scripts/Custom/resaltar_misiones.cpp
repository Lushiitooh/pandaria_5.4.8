/*
 * Resaltar objetivos de mision (servidor personal de Luis).
 *
 * Cada 2 segundos, los enemigos y NPC cercanos que necesitas para una mision
 * que tienes sin completar brillan en verde:
 *  - los que tienes que matar (o que dan el credito de muerte),
 *  - los que tienes que ayudar o con los que tienes que hablar,
 *  - los que sueltan un objeto que te falta para la mision.
 * Cuando completas ese objetivo dejan de brillar (a los pocos segundos).
 *
 * Los objetos del escenario de mision (cofres, plantas, etc.) ya brillan con
 * destellos por si solos.
 *
 * Configuracion opcional en worldserver.conf:
 *   ResaltarMisiones.Enable  = 1
 *   ResaltarMisiones.Rango   = 60      (metros)
 *   ResaltarMisiones.Hechizo = 22577   (brillo verde; 22576 azul, 22581 morado)
 */

#include "ScriptMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "Creature.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "SpellAuras.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ResaltarMisiones
{
    static bool   sActivo  = true;
    static float  sRango   = 60.0f;
    static uint32 sHechizo = 22577;          // Glowy (Green), aura visual sin efecto
    static int32 const DURACION = 4500;      // se renueva cada 2 s mientras haga falta

    // lootid de criatura -> objetos de mision que puede soltar
    static std::unordered_map<uint32, std::vector<uint32>> sBotin;

    static void Cargar()
    {
        sActivo  = sConfigMgr->GetBoolDefault("ResaltarMisiones.Enable", true);
        sRango   = sConfigMgr->GetFloatDefault("ResaltarMisiones.Rango", 60.0f);
        sHechizo = sConfigMgr->GetIntDefault("ResaltarMisiones.Hechizo", 22577);
        if (sRango < 10.0f) sRango = 10.0f;
        if (sRango > 100.0f) sRango = 100.0f;
    }

    static void CargarBotin()
    {
        sBotin.clear();

        // Objetos que alguna mision pide
        std::unordered_set<uint32> deMision;
        if (QueryResult r = WorldDatabase.Query("SELECT DISTINCT objectId FROM quest_objective WHERE type = 1"))
            do deMision.insert(r->Fetch()[0].GetUInt32()); while (r->NextRow());

        // Tablas de referencia -> objetos de mision que contienen
        std::unordered_map<uint32, std::vector<uint32>> referencias;
        if (QueryResult r = WorldDatabase.Query("SELECT entry, item FROM reference_loot_template"))
            do
            {
                Field* f = r->Fetch();
                if (deMision.count(f[1].GetUInt32()))
                    referencias[f[0].GetUInt32()].push_back(f[1].GetUInt32());
            } while (r->NextRow());

        // Botin de criaturas (directo o por referencia)
        uint32 n = 0;
        if (QueryResult r = WorldDatabase.Query("SELECT entry, item, mincountOrRef FROM creature_loot_template"))
            do
            {
                Field* f = r->Fetch();
                uint32 entry = f[0].GetUInt32();
                uint32 item = f[1].GetUInt32();
                int32 ref = f[2].GetInt32();
                if (ref < 0)
                {
                    auto it = referencias.find(uint32(-ref));
                    if (it != referencias.end())
                        for (uint32 i : it->second)
                            sBotin[entry].push_back(i), ++n;
                }
                else if (deMision.count(item))
                    sBotin[entry].push_back(item), ++n;
            } while (r->NextRow());

        TC_LOG_INFO("server.loading", ">> Resaltar misiones: %u objetos de mision en el botin de criaturas", n);
    }

    struct Necesidades
    {
        std::unordered_set<uint32> matar;     // entradas de criatura (o credito)
        std::unordered_set<uint32> hablar;    // entradas de criatura
        std::unordered_set<uint32> objetos;   // objetos que faltan
        bool Vacio() const { return matar.empty() && hablar.empty() && objetos.empty(); }
    };

    static void Calcular(Player* p, Necesidades& n)
    {
        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 questId = p->GetQuestSlotQuestId(slot);
            if (!questId || p->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
                continue;
            Quest const* q = sObjectMgr->GetQuestTemplate(questId);
            if (!q)
                continue;
            for (QuestObjective const* obj : q->m_questObjectives)
            {
                if (!obj || p->IsQuestObjectiveComplete(q, *obj))
                    continue;
                switch (obj->Type)
                {
                    case QUEST_OBJECTIVE_TYPE_NPC:          n.matar.insert(obj->ObjectId); break;
                    case QUEST_OBJECTIVE_TYPE_NPC_INTERACT: n.hablar.insert(obj->ObjectId); break;
                    case QUEST_OBJECTIVE_TYPE_ITEM:         n.objetos.insert(obj->ObjectId); break;
                    default: break;
                }
            }
        }
    }

    static bool LaNecesita(Creature* c, Necesidades const& n)
    {
        CreatureTemplate const* t = c->GetCreatureTemplate();
        if (!t)
            return false;
        uint32 entry = c->GetEntry();
        if (n.matar.count(entry) || n.hablar.count(entry))
            return true;
        for (uint32 i = 0; i < MAX_KILL_CREDIT; ++i)
            if (t->KillCredit[i] && n.matar.count(t->KillCredit[i]))
                return true;
        if (!n.objetos.empty() && t->lootid)
        {
            auto it = sBotin.find(t->lootid);
            if (it != sBotin.end())
                for (uint32 item : it->second)
                    if (n.objetos.count(item))
                        return true;
        }
        return false;
    }

    struct CriaturasCerca
    {
        WorldObject const* origen;
        float rango;
        CriaturasCerca(WorldObject const* o, float r) : origen(o), rango(r) { }
        bool operator()(Creature* c) const
        {
            return c->IsAlive() && !c->IsPet() && !c->IsTotem() && origen->IsWithinDistInMap(c, rango);
        }
    };
}

using namespace ResaltarMisiones;

class resaltar_misiones_worldscript : public WorldScript
{
public:
    resaltar_misiones_worldscript() : WorldScript("resaltar_misiones_worldscript") { }

    void OnConfigLoad(bool /*reload*/) override { Cargar(); }

    void OnStartup() override
    {
        Cargar();
        CargarBotin();
        TC_LOG_INFO("server.loading", ">> Resaltar misiones: %s (rango %.0f m)", sActivo ? "ACTIVO" : "desactivado", sRango);
    }
};

class resaltar_misiones_playerscript : public PlayerScript
{
public:
    resaltar_misiones_playerscript() : PlayerScript("resaltar_misiones_playerscript") { }

    void OnLogout(Player* player) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _timers.erase(player->GetGUIDLow());
    }

    void OnUpdate(Player* player, uint32 diff) override
    {
        if (!sActivo)
            return;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            uint32& timer = _timers[player->GetGUIDLow()];
            if (timer > diff)
            {
                timer -= diff;
                return;
            }
            timer = 2000;
        }

        if (!player->IsInWorld() || !player->GetSession() || player->GetSession()->PlayerLoading())
            return;

        Necesidades n;
        Calcular(player, n);
        if (n.Vacio())
            return;

        std::list<Creature*> lista;
        CriaturasCerca check(player, sRango);
        Trinity::CreatureListSearcher<CriaturasCerca> searcher(player, lista, check);
        player->VisitNearbyGridObject(sRango, searcher);

        for (Creature* c : lista)
        {
            if (!LaNecesita(c, n))
                continue;
            Aura* aura = c->GetAura(sHechizo);
            if (!aura)
                aura = c->AddAura(sHechizo, c);
            if (aura)
            {
                aura->SetMaxDuration(DURACION);
                aura->SetDuration(DURACION);
            }
        }
    }

private:
    std::unordered_map<uint32, uint32> _timers;
    std::mutex _mutex;
};

void AddSC_resaltar_misiones()
{
    Cargar();
    new resaltar_misiones_worldscript();
    new resaltar_misiones_playerscript();
}
