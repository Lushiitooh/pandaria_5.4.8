/*
 * Botin de mazmorras de maximo grado (servidor personal de Luis).
 *
 * Dentro de cualquier mazmorra, banda o escenario:
 *  - Cada pieza de equipo (arma o armadura) que suelte un enemigo se cambia por
 *    la pieza morada (epica) de mayor nivel de objeto que exista para ese mismo
 *    hueco y que el jugador pueda usar a su nivel actual.
 *  - Los jefes sueltan ademas piezas moradas extra de maximo grado.
 *  - Los enemigos normales tienen una probabilidad de soltar una pieza extra.
 *
 * Se elige equipo del tipo de armadura de la clase (tela, cuero, malla, placas)
 * y con el atributo principal del personaje (fuerza, agilidad o intelecto).
 *
 * Configuracion opcional en worldserver.conf:
 *   BotinMazmorras.Enable      = 1
 *   BotinMazmorras.ExtraJefe   = 2     (piezas extra por jefe)
 *   BotinMazmorras.ProbNormal  = 10    (% de pieza extra en enemigos normales)
 *   BotinMazmorras.Margen      = 6     (niveles de objeto bajo el maximo que aun se aceptan)
 */

#include "ScriptMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "Creature.h"
#include "Log.h"
#include "LootMgr.h"
#include "ObjectMgr.h"
#include "Item.h"
#include "ItemEnchantmentMgr.h"
#include <algorithm>
#include <unordered_set>
#include <vector>

namespace BotinMazmorras
{
    enum : uint32 { NIVEL_MAX = 90, MAX_TIPO = 29 };

    static bool  sActivo     = true;
    static uint32 sExtraJefe = 2;
    static float sProbNormal = 10.0f;
    static uint32 sMargen    = 6;

    // Piezas moradas por tipo de hueco, ordenadas de mayor a menor nivel de objeto
    static std::vector<ItemTemplate const*> sPorTipo[MAX_TIPO + 1];

    static uint32 Grupo(uint32 inv)
    {
        return inv == INVTYPE_ROBE ? uint32(INVTYPE_CHEST) : inv;
    }

    static bool EsArmadura(uint32 inv)
    {
        switch (inv)
        {
            case INVTYPE_HEAD: case INVTYPE_SHOULDERS: case INVTYPE_CHEST: case INVTYPE_ROBE:
            case INVTYPE_WAIST: case INVTYPE_LEGS: case INVTYPE_FEET: case INVTYPE_WRISTS:
            case INVTYPE_HANDS:
                return true;
            default:
                return false;
        }
    }

    static bool Valido(ItemTemplate const& t)
    {
        if (t.Quality != ITEM_QUALITY_EPIC)
            return false;
        if (t.Class != ITEM_CLASS_WEAPON && t.Class != ITEM_CLASS_ARMOR)
            return false;
        if (t.InventoryType == INVTYPE_NON_EQUIP || t.InventoryType == INVTYPE_BODY || t.InventoryType == INVTYPE_TABARD
            || t.InventoryType == INVTYPE_BAG || t.InventoryType == INVTYPE_AMMO || t.InventoryType == INVTYPE_QUIVER
            || t.InventoryType > MAX_TIPO)
            return false;
        if (t.RequiredLevel > NIVEL_MAX || t.ItemLevel == 0)
            return false;
        if (t.Bonding != BIND_ON_ACQUIRE && t.Bonding != BIND_ON_EQUIP)
            return false;
        // Sin equipo JcJ (temple o poder JcJ)
        for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
            if (t.ItemStat[i].ItemStatType == ITEM_MOD_RESILIENCE_RATING || t.ItemStat[i].ItemStatType == ITEM_MOD_PVP_POWER)
                return false;
        std::string const& n = t.Name1;
        if (n.empty() || n.find("Monster") == 0 || n.find("Test") == 0 || n.find("TEST") != std::string::npos
            || n.find("QA") == 0 || n.find("zz") == 0 || n.find("Deprecated") != std::string::npos || n.find("[") != std::string::npos
            || n.find("PH") == 0)
            return false;
        return true;
    }

    static void Cargar()
    {
        sActivo     = sConfigMgr->GetBoolDefault("BotinMazmorras.Enable", true);
        sExtraJefe  = sConfigMgr->GetIntDefault("BotinMazmorras.ExtraJefe", 2);
        sProbNormal = sConfigMgr->GetFloatDefault("BotinMazmorras.ProbNormal", 10.0f);
        sMargen     = sConfigMgr->GetIntDefault("BotinMazmorras.Margen", 6);
        if (sExtraJefe > 6)
            sExtraJefe = 6;
    }

    static void CargarObjetos()
    {
        for (auto& v : sPorTipo)
            v.clear();

        // Solo objetos que de verdad caen de algun sitio (evita objetos de prueba)
        std::unordered_set<uint32> enBotin;
        char const* consultas[] = {
            "SELECT DISTINCT item FROM reference_loot_template",
            "SELECT DISTINCT item FROM creature_loot_template",
            "SELECT DISTINCT item FROM gameobject_loot_template",
            "SELECT DISTINCT item FROM item_loot_template",
        };
        for (char const* q : consultas)
            if (QueryResult r = WorldDatabase.Query(q))
                do enBotin.insert(r->Fetch()[0].GetUInt32()); while (r->NextRow());

        uint32 total = 0;
        for (uint32 entry : enBotin)
        {
            ItemTemplate const* t = sObjectMgr->GetItemTemplate(entry);
            if (!t || !Valido(*t))
                continue;
            sPorTipo[Grupo(t->InventoryType)].push_back(t);
            ++total;
        }
        for (auto& v : sPorTipo)
            std::sort(v.begin(), v.end(), [](ItemTemplate const* a, ItemTemplate const* b) { return a->ItemLevel > b->ItemLevel; });

        TC_LOG_INFO("server.loading", ">> Botin de mazmorras: %u piezas moradas disponibles", total);
    }

    static uint32 ArmaduraDeClase(Player* p)
    {
        bool alto = p->GetLevel() >= 40;
        switch (p->GetClass())
        {
            case CLASS_WARRIOR: case CLASS_PALADIN:
                return alto ? ITEM_SUBCLASS_ARMOR_PLATE : ITEM_SUBCLASS_ARMOR_MAIL;
            case CLASS_DEATH_KNIGHT:
                return ITEM_SUBCLASS_ARMOR_PLATE;
            case CLASS_HUNTER: case CLASS_SHAMAN:
                return alto ? ITEM_SUBCLASS_ARMOR_MAIL : ITEM_SUBCLASS_ARMOR_LEATHER;
            case CLASS_ROGUE: case CLASS_DRUID: case CLASS_MONK:
                return ITEM_SUBCLASS_ARMOR_LEATHER;
            default:
                return ITEM_SUBCLASS_ARMOR_CLOTH;
        }
    }

    // Atributo principal del personaje: el mas alto entre fuerza, agilidad e intelecto
    static uint32 AtributoPrincipal(Player* p)
    {
        float fue = p->GetStat(STAT_STRENGTH), agi = p->GetStat(STAT_AGILITY), intel = p->GetStat(STAT_INTELLECT);
        if (fue >= agi && fue >= intel)
            return ITEM_MOD_STRENGTH;
        if (agi >= intel)
            return ITEM_MOD_AGILITY;
        return ITEM_MOD_INTELLECT;
    }

    // 0 = sin atributo principal, 1 = coincide, -1 = otro atributo
    static int Coincide(ItemTemplate const* t, uint32 atributo)
    {
        bool tiene = false;
        for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
        {
            uint32 s = t->ItemStat[i].ItemStatType;
            if (s == atributo)
                return 1;
            if (s == ITEM_MOD_STRENGTH || s == ITEM_MOD_AGILITY || s == ITEM_MOD_INTELLECT)
                tiene = true;
        }
        return tiene ? -1 : 0;
    }

    // Mejor pieza morada para ese hueco. 0 si no hay ninguna.
    static uint32 Elegir(uint32 inv, Player* p)
    {
        auto const& lista = sPorTipo[Grupo(inv)];
        if (lista.empty() || !p)
            return 0;

        uint32 nivel = p->GetLevel();
        uint32 armadura = ArmaduraDeClase(p);
        uint32 atributo = AtributoPrincipal(p);

        // 1: armadura de la clase + atributo correcto, 2: atributo correcto, 3: cualquiera usable
        for (int estricto = 3; estricto >= 1; --estricto)
        {
            std::vector<uint32> candidatos;
            uint32 mejor = 0;
            for (ItemTemplate const* t : lista)
            {
                if (t->RequiredLevel > nivel)
                    continue;
                if (mejor && t->ItemLevel + sMargen < mejor)
                    break;
                if (p->CanUseItem(t) != EQUIP_ERR_OK)
                    continue;
                if (estricto >= 2 && Coincide(t, atributo) < 0)
                    continue;
                if (estricto == 3 && t->Class == ITEM_CLASS_ARMOR && EsArmadura(t->InventoryType) && t->SubClass != armadura)
                    continue;
                if (!mejor)
                    mejor = t->ItemLevel;
                candidatos.push_back(t->ItemId);
            }
            if (!candidatos.empty())
                return candidatos[urand(0, candidatos.size() - 1)];
        }
        return 0;
    }

    static uint32 TipoAlAzar(Player* p)
    {
        static uint32 const tipos[] = {
            INVTYPE_HEAD, INVTYPE_NECK, INVTYPE_SHOULDERS, INVTYPE_CHEST, INVTYPE_WAIST, INVTYPE_LEGS,
            INVTYPE_FEET, INVTYPE_WRISTS, INVTYPE_HANDS, INVTYPE_FINGER, INVTYPE_TRINKET, INVTYPE_CLOAK,
            INVTYPE_WEAPON, INVTYPE_2HWEAPON, INVTYPE_WEAPONMAINHAND, INVTYPE_HOLDABLE, INVTYPE_SHIELD, INVTYPE_RANGEDRIGHT,
        };
        for (uint8 i = 0; i < 10; ++i)
        {
            uint32 inv = tipos[urand(0, sizeof(tipos) / sizeof(tipos[0]) - 1)];
            if (uint32 entry = Elegir(inv, p))
                return entry;
        }
        return 0;
    }

    static void Mejorar(LootItem& li, uint32 entry)
    {
        li.itemid = entry;
        li.count = 1;
        li.randomSuffix = GenerateEnchSuffixFactor(entry);
        li.randomPropertyId = Item::GenerateItemRandomPropertyId(entry);
        li.is_underthreshold = false;
        li.needs_quest = false;
    }
}

using namespace BotinMazmorras;

class botin_mazmorras_worldscript : public WorldScript
{
public:
    botin_mazmorras_worldscript() : WorldScript("botin_mazmorras_worldscript") { }

    void OnConfigLoad(bool /*reload*/) override { Cargar(); }

    void OnStartup() override
    {
        Cargar();
        CargarObjetos();
        TC_LOG_INFO("server.loading", ">> Botin de mazmorras: %s (jefes +%u piezas, normales %.0f%%)",
            sActivo ? "ACTIVO" : "desactivado", sExtraJefe, sProbNormal);
    }

    void OnCreatureLootGenerated(Player* looter, Creature* c) override
    {
        if (!sActivo || !looter || !c || !c->GetMap() || !c->GetMap()->IsDungeon())
            return;

        // 1. Cambia cada pieza de equipo por la morada de mayor grado del mismo hueco
        for (LootItem& li : c->loot.items)
        {
            if (li.currency || li.needs_quest || li.is_looted)
                continue;
            ItemTemplate const* t = sObjectMgr->GetItemTemplate(li.itemid);
            if (!t || (t->Class != ITEM_CLASS_WEAPON && t->Class != ITEM_CLASS_ARMOR))
                continue;
            if (t->InventoryType == INVTYPE_NON_EQUIP || t->InventoryType == INVTYPE_BAG
                || t->InventoryType == INVTYPE_BODY || t->InventoryType == INVTYPE_TABARD)
                continue;
            if (uint32 entry = Elegir(t->InventoryType, looter))
                Mejorar(li, entry);
        }

        // 2. Piezas extra: jefes siempre, enemigos normales con probabilidad
        bool jefe = c->IsDungeonBoss() || c->isWorldBoss()
            || (c->GetCreatureTemplate() && c->GetCreatureTemplate()->rank == CREATURE_ELITE_WORLDBOSS);
        uint32 extra = jefe ? sExtraJefe : (roll_chance_f(sProbNormal) ? 1 : 0);
        for (uint32 i = 0; i < extra; ++i)
        {
            if (uint32 entry = TipoAlAzar(looter))
            {
                LootStoreItem item(entry, 0, 100.0f, 0, 0, 1, 1);
                c->loot.AddItem(item, 1u);
            }
        }

        if (!c->loot.empty())
            c->SetFlag(OBJECT_FIELD_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);
    }
};

void AddSC_botin_mazmorras()
{
    Cargar();
    new botin_mazmorras_worldscript();
}
