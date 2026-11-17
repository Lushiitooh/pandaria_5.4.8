/*
 * Maestro de Profesiones (servidor personal de Luis).
 *
 * Orden en el juego: .profesiones  -> convoca al Maestro a tu lado (5 minutos).
 *
 * Para cada profesion (las 11 principales y las 4 secundarias) ofrece:
 *  - Aprenderla al maximo (600) con TODAS sus recetas.
 *  - Tienda exclusiva con todos los materiales que piden sus recetas y las
 *    herramientas necesarias (martillo, pico, cuchillo, varas, kit, etc.),
 *    ordenados por la habilidad de la receta y repartidos en paginas.
 *  - Invocar las estaciones de trabajo que piden sus recetas (yunque, forja,
 *    fuego de cocina, etc.) a tu lado durante 5 minutos.
 *
 * Todo se calcula al arrancar el servidor a partir de los datos del juego
 * (recetas, materiales, herramientas y estaciones), asi que no falta nada.
 *
 * Configuracion opcional en worldserver.conf:
 *   Profesiones.Enable = 1
 *   Profesiones.Duracion = 300     (segundos que se queda el Maestro y las estaciones)
 */

#include "ScriptMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
#include "ScriptedGossip.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "DBCStores.h"
#include "Chat.h"
#include "Log.h"
#include "WorldSession.h"
#include "Group.h"
#include "Map.h"
#include "TemporarySummon.h"
#include <algorithm>
#include <cstdarg>
#include <map>
#include <set>
#include <vector>

namespace Profesiones
{
    enum : uint32
    {
        NPC_MAESTRO       = 500101,
        VENDEDOR_BASE     = 910000,     // entradas virtuales de tienda: base + indice*20 + pagina
        POR_PAGINA        = 140,        // el cliente acepta hasta 150 por tienda
        MAX_PAGINAS       = 20,

        SENDER_MENU       = 1,
        SENDER_PROF       = 100,        // + indice de profesion

        ACC_VOLVER        = 1,
        ACC_APRENDER      = 2,
        ACC_ESTACIONES    = 3,
        ACC_CERRAR        = 4,
        ACC_TIENDA        = 100,        // + pagina
    };

    struct Prof
    {
        uint32 skill;
        char const* nombre;
        bool principal;

        uint32 hechizoRango = 0;                         // hechizo del rango mas alto (Maestro zen)
        std::vector<uint32> recetas;
        std::vector<std::pair<uint32, uint32>> paginas;  // rango de habilidad de cada pagina
        std::vector<uint32> estaciones;                  // gameobjects de foco
    };

    static std::vector<Prof> sProfs =
    {
        { 171, "Alquimia",         true  },
        { 164, "Herrer\303\255a",  true  },
        { 333, "Encantamiento",    true  },
        { 202, "Ingenier\303\255a", true },
        { 182, "Herborister\303\255a", true },
        { 773, "Inscripci\303\263n", true },
        { 755, "Joyer\303\255a",   true  },
        { 165, "Peleter\303\255a", true  },
        { 186, "Miner\303\255a",   true  },
        { 393, "Desuello",         true  },
        { 197, "Sastrer\303\255a", true  },
        { 185, "Cocina",           false },
        { 129, "Primeros auxilios", false },
        { 356, "Pesca",            false },
        { 794, "Arqueolog\303\255a", false },
    };

    static bool   sActivo   = true;
    static uint32 sDuracion = 300;

    static std::string Formato(char const* fmt, ...)
    {
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        return buf;
    }

    static void Cargar()
    {
        sActivo   = sConfigMgr->GetBoolDefault("Profesiones.Enable", true);
        sDuracion = sConfigMgr->GetIntDefault("Profesiones.Duracion", 300);
        if (sDuracion < 60) sDuracion = 60;
    }

    static bool ItemValido(uint32 entry)
    {
        ItemTemplate const* t = sObjectMgr->GetItemTemplate(entry);
        return t && t->Quality < ITEM_QUALITY_ARTIFACT;
    }

    static void Preparar()
    {
        // Gameobject de cada foco de hechizo (yunque, forja, fuego...)
        std::map<uint32, uint32> goPorFoco;
        if (QueryResult r = WorldDatabase.Query("SELECT entry, data0 FROM gameobject_template WHERE type = 8 ORDER BY entry"))
            do
            {
                Field* f = r->Fetch();
                goPorFoco.emplace(f[1].GetUInt32(), f[0].GetUInt32());   // se queda con el primero
            } while (r->NextRow());

        // Un objeto por categoria de herramienta (el de menor nivel)
        std::map<uint32, ItemTemplate const*> herramientaPorCat;
        for (auto const& par : *sObjectMgr->GetItemTemplateStore())
        {
            ItemTemplate const& t = par.second;
            if (!t.TotemCategory || t.Quality >= ITEM_QUALITY_ARTIFACT)
                continue;
            auto it = herramientaPorCat.find(t.TotemCategory);
            if (it == herramientaPorCat.end() || t.ItemLevel < it->second->ItemLevel)
                herramientaPorCat[t.TotemCategory] = &t;
        }

        // Hechizo de rango mas alto de cada profesion (efecto "paso de habilidad")
        std::map<uint32, std::pair<int32, uint32>> mejorRango;   // skill -> (paso, hechizo)
        for (uint32 id = 1; id < sSpellMgr->GetSpellInfoStoreSize(); ++id)
        {
            SpellInfo const* si = sSpellMgr->GetSpellInfo(id);
            if (!si)
                continue;
            for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
                if (si->Effects[e].Effect == SPELL_EFFECT_SKILL_STEP)
                {
                    uint32 skill = uint32(si->Effects[e].MiscValue);
                    int32 paso = si->Effects[e].CalcValue();
                    auto& m = mejorRango[skill];
                    if (paso > m.first)
                        m = { paso, id };
                }
        }

        uint32 totalMat = 0;
        for (size_t idx = 0; idx < sProfs.size(); ++idx)
        {
            Prof& p = sProfs[idx];
            p.recetas.clear();
            p.paginas.clear();
            p.estaciones.clear();
            p.hechizoRango = mejorRango.count(p.skill) ? mejorRango[p.skill].second : 0;

            std::map<uint32, uint32> materiales;   // item -> habilidad minima de la receta
            std::set<uint32> focos;

            auto agregar = [&materiales](uint32 item, uint32 hab)
            {
                if (!item || !ItemValido(item))
                    return;
                auto it = materiales.find(item);
                if (it == materiales.end() || hab < it->second)
                    materiales[item] = hab;
            };

            for (uint32 j = 0; j < sSkillLineAbilityStore.GetNumRows(); ++j)
            {
                SkillLineAbilityEntry const* sla = sSkillLineAbilityStore.LookupEntry(j);
                if (!sla || sla->skillId != p.skill)
                    continue;
                SpellInfo const* si = sSpellMgr->GetSpellInfo(sla->spellId);
                if (!si)
                    continue;

                if (!sla->forward_spellid && !sla->racemask)
                    p.recetas.push_back(sla->spellId);

                uint32 hab = sla->req_skill_value;
                for (uint8 r = 0; r < MAX_SPELL_REAGENTS; ++r)
                    if (si->Reagent[r] > 0)
                        agregar(uint32(si->Reagent[r]), hab);
                for (uint8 t = 0; t < 2; ++t)
                {
                    if (si->Totem[t])
                        agregar(si->Totem[t], 0);
                    if (si->TotemCategory[t])
                    {
                        auto it = herramientaPorCat.find(si->TotemCategory[t]);
                        if (it != herramientaPorCat.end())
                            agregar(it->second->ItemId, 0);
                    }
                }
                if (si->RequiresSpellFocus)
                    focos.insert(si->RequiresSpellFocus);
            }

            // Pesca: una cana
            if (p.skill == 356)
                agregar(6256, 0);

            for (uint32 foco : focos)
            {
                auto it = goPorFoco.find(foco);
                if (it != goPorFoco.end())
                    p.estaciones.push_back(it->second);
            }

            // Ordenar por habilidad y repartir en paginas de tienda
            std::vector<std::pair<uint32, uint32>> orden;   // (habilidad, item)
            for (auto const& m : materiales)
                orden.emplace_back(m.second, m.first);
            std::sort(orden.begin(), orden.end());

            for (size_t i = 0; i < orden.size() && p.paginas.size() < MAX_PAGINAS; i += POR_PAGINA)
            {
                uint32 pagina = uint32(p.paginas.size());
                uint32 vendedor = VENDEDOR_BASE + uint32(idx) * MAX_PAGINAS + pagina;
                size_t fin = std::min(orden.size(), i + POR_PAGINA);
                for (size_t k = i; k < fin; ++k)
                    sObjectMgr->AddVendorItem(vendedor, orden[k].second, 0, 0, 0, ITEM_VENDOR_TYPE_ITEM, false);
                p.paginas.emplace_back(orden[i].first, orden[fin - 1].first);
                totalMat += uint32(fin - i);
            }
        }

        TC_LOG_INFO("server.loading", ">> Maestro de Profesiones: %u materiales en tiendas, %u profesiones", totalMat, uint32(sProfs.size()));
    }

    static void AprenderTodo(Player* player, Prof const& p)
    {
        ChatHandler h(player->GetSession());
        if (p.hechizoRango && !player->HasSpell(p.hechizoRango))
            player->LearnSpell(p.hechizoRango, false);

        if (!player->HasSkill(p.skill))
        {
            h.PSendSysMessage("|cffff0000[Profesiones]|r No se pudo aprender %s (\302\277l\303\255mite de profesiones?).", p.nombre);
            return;
        }

        uint32 aprendidas = 0;
        for (uint32 spell : p.recetas)
        {
            if (player->HasSpell(spell))
                continue;
            SpellInfo const* si = sSpellMgr->GetSpellInfo(spell);
            if (!si || !SpellMgr::IsSpellValid(si, player, false))
                continue;
            player->LearnSpell(spell, false);
            ++aprendidas;
        }

        uint16 maximo = player->GetPureMaxSkillValue(p.skill);
        if (maximo < 600)
            maximo = 600;
        player->SetSkill(p.skill, player->GetSkillStep(p.skill), maximo, maximo);
        h.PSendSysMessage("|cff00ccff[Profesiones]|r %s al m\303\241ximo (%u) y %u recetas nuevas aprendidas.", p.nombre, maximo, aprendidas);
    }

    static void InvocarEstaciones(Player* player, Prof const& p)
    {
        ChatHandler h(player->GetSession());
        if (p.estaciones.empty())
        {
            h.PSendSysMessage("|cff00ccff[Profesiones]|r %s no necesita estaciones de trabajo.", p.nombre);
            return;
        }
        float paso = 2.0f * float(M_PI) / float(p.estaciones.size());
        uint32 n = 0;
        for (size_t i = 0; i < p.estaciones.size(); ++i)
        {
            float ang = player->GetOrientation() + float(M_PI) / 2.0f + paso * float(i);
            float x = player->GetPositionX() + 3.0f * std::cos(ang);
            float y = player->GetPositionY() + 3.0f * std::sin(ang);
            float z = player->GetPositionZ();
            player->UpdateAllowedPositionZ(x, y, z);
            if (GameObject* go = player->SummonGameObject(p.estaciones[i], x, y, z, ang + float(M_PI), { }, sDuracion))
            {
                go->SetSpawnedByDefault(false);
                ++n;
            }
        }
        h.PSendSysMessage("|cff00ccff[Profesiones]|r %u estaciones de %s aparecieron a tu alrededor (%u segundos).", n, p.nombre, sDuracion);
    }
}

using namespace Profesiones;

class npc_maestro_profesiones : public CreatureScript
{
public:
    npc_maestro_profesiones() : CreatureScript("npc_maestro_profesiones") { }

    static void MenuPrincipal(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);
        for (size_t i = 0; i < sProfs.size(); ++i)
        {
            Prof const& p = sProfs[i];
            std::string txt = player->HasSkill(p.skill)
                ? Formato("%s (%u/%u)", p.nombre, player->GetSkillValue(p.skill), player->GetMaxSkillValue(p.skill))
                : Formato("%s", p.nombre);
            AddGossipItemFor(player, p.principal ? GOSSIP_ICON_TRAINER : GOSSIP_ICON_INTERACT_1, txt, SENDER_PROF + uint32(i), ACC_VOLVER + 1000);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Cerrar", SENDER_MENU, ACC_CERRAR);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    static void MenuProfesion(Player* player, Creature* creature, uint32 idx)
    {
        Prof const& p = sProfs[idx];
        ClearGossipMenuFor(player);
        AddGossipItemFor(player, GOSSIP_ICON_TRAINER, Formato("Aprender %s al m\303\241ximo con todas sus recetas", p.nombre), SENDER_PROF + idx, ACC_APRENDER);
        for (size_t pg = 0; pg < p.paginas.size(); ++pg)
            AddGossipItemFor(player, GOSSIP_ICON_VENDOR,
                Formato("Tienda de materiales %u/%u (recetas de habilidad %u a %u)", uint32(pg + 1), uint32(p.paginas.size()), p.paginas[pg].first, p.paginas[pg].second),
                SENDER_PROF + idx, ACC_TIENDA + uint32(pg));
        if (!p.estaciones.empty())
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, Formato("Invocar estaciones de trabajo (%u)", uint32(p.estaciones.size())), SENDER_PROF + idx, ACC_ESTACIONES);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<< Volver", SENDER_MENU, ACC_VOLVER);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        MenuPrincipal(player, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        if (sender == SENDER_MENU)
        {
            if (action == ACC_CERRAR)
                CloseGossipMenuFor(player);
            else
                MenuPrincipal(player, creature);
            return true;
        }

        if (sender < SENDER_PROF || sender >= SENDER_PROF + sProfs.size())
        {
            CloseGossipMenuFor(player);
            return true;
        }
        uint32 idx = sender - SENDER_PROF;
        Prof const& p = sProfs[idx];

        if (action >= ACC_TIENDA && action < ACC_TIENDA + MAX_PAGINAS)
        {
            uint32 pg = action - ACC_TIENDA;
            CloseGossipMenuFor(player);
            if (pg < p.paginas.size())
                player->GetSession()->SendListInventory(creature->GetGUID(), VENDEDOR_BASE + idx * MAX_PAGINAS + pg);
            return true;
        }

        switch (action)
        {
            case ACC_APRENDER:
                AprenderTodo(player, p);
                MenuProfesion(player, creature, idx);
                break;
            case ACC_ESTACIONES:
                InvocarEstaciones(player, p);
                MenuProfesion(player, creature, idx);
                break;
            default:
                MenuProfesion(player, creature, idx);
                break;
        }
        return true;
    }
};

class profesiones_worldscript : public WorldScript
{
public:
    profesiones_worldscript() : WorldScript("profesiones_worldscript") { }
    void OnConfigLoad(bool /*reload*/) override { Cargar(); }
    void OnStartup() override
    {
        Cargar();
        Preparar();
    }
};

class profesiones_commandscript : public CommandScript
{
public:
    profesiones_commandscript() : CommandScript("profesiones_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> comandos =
        {
            { "profesiones", SEC_PLAYER, false, &HandleProfesiones, "Convoca al Maestro de Profesiones." },
            { "dificultad",  SEC_PLAYER, false, &HandleDificultad,  "Elige la dificultad de banda: normal10, heroica10, normal25, heroica25, flexible." },
        };
        return comandos;
    }

    static bool HandleProfesiones(ChatHandler* handler, char const* /*args*/)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
            return false;
        if (!sActivo)
        {
            handler->SendSysMessage("El Maestro de Profesiones est\303\241 desactivado.");
            return true;
        }
        if (player->IsInCombat())
        {
            handler->SendSysMessage("|cffff0000[Profesiones]|r No puedes convocar al Maestro en combate.");
            return true;
        }
        float x, y, z;
        player->GetClosePoint(x, y, z, player->GetObjectSize(), 2.0f);
        if (TempSummon* m = player->SummonCreature(NPC_MAESTRO, x, y, z, player->GetOrientation() + float(M_PI),
            TEMPSUMMON_TIMED_DESPAWN, sDuracion * IN_MILLISECONDS, true))
        {
            m->SetFacingToObject(player);
            handler->PSendSysMessage("|cff00ccff[Profesiones]|r El Maestro de Profesiones lleg\303\263 (%u minutos).", sDuracion / 60);
        }
        else
            handler->SendSysMessage("|cffff0000[Profesiones]|r No se pudo convocar al Maestro (\302\277falta aplicar el SQL?).");
        return true;
    }

    static bool HandleDificultad(ChatHandler* handler, char const* args)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
            return false;

        std::string a = args ? args : "";
        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        while (!a.empty() && a.back() == ' ') a.pop_back();

        struct Op { char const* clave; Difficulty d; char const* nombre; };
        static Op const ops[] =
        {
            { "normal10",  RAID_DIFFICULTY_10MAN_NORMAL,  "Normal 10" },
            { "heroica10", RAID_DIFFICULTY_10MAN_HEROIC,  "Heroica 10" },
            { "normal25",  RAID_DIFFICULTY_25MAN_NORMAL,  "Normal 25" },
            { "heroica25", RAID_DIFFICULTY_25MAN_HEROIC,  "Heroica 25" },
            { "flexible",  RAID_DIFFICULTY_1025MAN_FLEX,  "Flexible (solo Asedio de Orgrimmar)" },
        };

        Op const* elegido = nullptr;
        for (Op const& o : ops)
            if (a == o.clave)
                elegido = &o;

        if (!elegido)
        {
            handler->SendSysMessage("|cff00ccff[Dificultad]|r Usa: .dificultad normal10 | heroica10 | normal25 | heroica25 | flexible");
            return true;
        }

        Map* map = player->FindMap();
        if (map && map->IsDungeon())
        {
            handler->SendSysMessage("|cffff0000[Dificultad]|r Sal de la mazmorra o banda para cambiar la dificultad.");
            return true;
        }

        Group* group = player->GetGroup();
        if (group)
        {
            if (!group->IsLeader(player->GetGUID()))
            {
                handler->SendSysMessage("|cffff0000[Dificultad]|r Solo el l\303\255der del grupo puede cambiarla.");
                return true;
            }
            group->ResetInstances(INSTANCE_RESET_CHANGE_DIFFICULTY, true, player);
            group->SetRaidDifficulty(elegido->d);
        }
        else
        {
            player->ResetInstances(INSTANCE_RESET_CHANGE_DIFFICULTY, true);
            player->SetRaidDifficulty(elegido->d);
            player->SendRaidDifficulty(elegido->d);
        }
        handler->PSendSysMessage("|cff00ccff[Dificultad]|r Dificultad de banda: %s. Entra caminando a la banda.", elegido->nombre);
        return true;
    }
};

void AddSC_profesiones()
{
    Cargar();
    new npc_maestro_profesiones();
    new profesiones_worldscript();
    new profesiones_commandscript();
}
